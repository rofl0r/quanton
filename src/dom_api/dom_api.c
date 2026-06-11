#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include "lexbor/html/interfaces/document.h"
#include "lexbor/dom/interfaces/document.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interface.h"
#include "lexbor/css/parser.h"
#include "lexbor/css/selectors/selectors.h"
#include "lexbor/selectors/selectors.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ── Dirty-flag marking ────────────────────────────────────────────────── */

void q_dom_mark_dirty(quanton_view_t *view,
                      lxb_dom_node_t *node,
                      q_dirty_flags_t flags)
{
    (void) node; /* per-node tracking not needed for v1; whole-tree relayout */
    if (view == NULL) {
        return;
    }
    view->dirty_flags |= flags;
}

/* ── Incremental relayout ──────────────────────────────────────────────── */

void q_view_update(quanton_view_t *view)
{
    if (view == NULL || view->dirty_flags == 0) {
        return;
    }

    if (view->dirty_flags & (Q_DIRTY_STYLE | Q_DIRTY_LAYOUT)) {
        q_box_t *new_root;

        q_layout_free_tree(view->layout_root);
        view->layout_root = NULL;

        new_root = q_layout_build_tree(view->document);
        if (new_root != NULL) {
            if (view->ctx != NULL && view->ctx->backend != NULL
                && view->ctx->backend->set_title != NULL
                && new_root->document_title != NULL
                && new_root->document_title[0] != '\0')
            {
                view->ctx->backend->set_title(view, new_root->document_title);
            }
            q_layout_measure(new_root, (float) view->vp_width,
                             (float) view->vp_height);
            q_layout_position(new_root, 0.0f, 0.0f);
            q_layout_position_absolute(new_root);
            view->layout_root = new_root;
        }
        /* layout implies paint */
        view->dirty_flags |= Q_DIRTY_PAINT;
    }

    if (view->dirty_flags & Q_DIRTY_PAINT) {
        if (view->layout_root != NULL) {
            q_paint_box(view->layout_root);
        }
    }

    if (view->dirty_flags & (Q_DIRTY_PAINT | Q_DIRTY_SCROLL)) {
        if (view->layout_root != NULL) {
            q_composite_frame(view);
            if (view->ctx != NULL && view->ctx->backend != NULL &&
                    view->ctx->backend->blit != NULL) {
                view->ctx->backend->blit(view);
            }
        }
    }

    view->dirty_flags = 0;
}

void q_view_refresh(quanton_view_t *view)
{
    if (view == NULL) {
        return;
    }
    q_dom_mark_dirty(view, NULL,
                     (q_dirty_flags_t)(Q_DIRTY_STYLE | Q_DIRTY_LAYOUT |
                                       Q_DIRTY_PAINT));
    q_view_update(view);
}

/* ── DOM mutation helpers ──────────────────────────────────────────────── */

int q_dom_set_attr(quanton_view_t *view,
                   lxb_dom_element_t *el,
                   const char *name, const char *value)
{
    lxb_dom_attr_t *attr;

    if (el == NULL || name == NULL || value == NULL) {
        return -1;
    }

    attr = lxb_dom_element_set_attribute(el,
                                         (const lxb_char_t *) name,
                                         strlen(name),
                                         (const lxb_char_t *) value,
                                         strlen(value));
    if (attr == NULL) {
        return -1;
    }

    q_dom_mark_dirty(view, lxb_dom_interface_node(el), Q_DIRTY_LAYOUT);
    return 0;
}

int q_dom_remove_attr(quanton_view_t *view,
                      lxb_dom_element_t *el,
                      const char *name)
{
    lxb_status_t status;

    if (el == NULL || name == NULL) {
        return -1;
    }

    status = lxb_dom_element_remove_attribute(el,
                                               (const lxb_char_t *) name,
                                               strlen(name));
    if (status != LXB_STATUS_OK) {
        return -1;
    }

    q_dom_mark_dirty(view, lxb_dom_interface_node(el), Q_DIRTY_LAYOUT);
    return 0;
}

int q_dom_set_text_content(quanton_view_t *view,
                            lxb_dom_element_t *el,
                            const char *text, size_t len)
{
    lxb_status_t status;

    if (el == NULL || text == NULL) {
        return -1;
    }

    status = lxb_dom_node_text_content_set(lxb_dom_interface_node(el),
                                            (const lxb_char_t *) text, len);
    if (status != LXB_STATUS_OK) {
        return -1;
    }

    q_dom_mark_dirty(view, lxb_dom_interface_node(el), Q_DIRTY_LAYOUT);
    return 0;
}

lxb_dom_element_t *q_dom_append_element(quanton_view_t *view,
                                         lxb_dom_element_t *parent,
                                         const char *tag_name)
{
    lxb_dom_document_t *dom_doc;
    lxb_dom_element_t  *new_el;

    if (parent == NULL || tag_name == NULL) {
        return NULL;
    }

    dom_doc = lxb_dom_interface_node(parent)->owner_document;
    if (dom_doc == NULL) {
        return NULL;
    }

    new_el = lxb_dom_document_create_element(dom_doc,
                                              (const lxb_char_t *) tag_name,
                                              strlen(tag_name),
                                              NULL);
    if (new_el == NULL) {
        return NULL;
    }

    lxb_dom_node_insert_child(lxb_dom_interface_node(parent),
                               lxb_dom_interface_node(new_el));

    q_dom_mark_dirty(view, lxb_dom_interface_node(parent), Q_DIRTY_LAYOUT);
    return new_el;
}

int q_dom_remove_node(quanton_view_t *view, lxb_dom_node_t *node)
{
    if (node == NULL) {
        return -1;
    }

    lxb_dom_node_remove(node);
    q_dom_mark_dirty(view, node, Q_DIRTY_LAYOUT);
    return 0;
}

/* ── CSS class helpers ─────────────────────────────────────────────────── */

bool q_dom_has_class(lxb_dom_element_t *el, const char *cls)
{
    const lxb_char_t *cur;
    size_t class_len;
    size_t cls_len;
    size_t i;

    if (el == NULL || cls == NULL) {
        return false;
    }

    cls_len = strlen(cls);

    cur = lxb_dom_element_get_attribute(el,
                                         (const lxb_char_t *) "class", 5,
                                         &class_len);
    if (cur == NULL || class_len == 0) {
        return false;
    }

    i = 0;
    while (i < class_len) {
        size_t start;
        size_t tok_len;

        /* skip whitespace */
        while (i < class_len && (cur[i] == ' ' || cur[i] == '\t' ||
                                  cur[i] == '\n' || cur[i] == '\r')) {
            ++i;
        }
        start = i;
        /* scan token */
        while (i < class_len && cur[i] != ' ' && cur[i] != '\t' &&
               cur[i] != '\n' && cur[i] != '\r') {
            ++i;
        }
        tok_len = i - start;
        if (tok_len == cls_len &&
                memcmp(cur + start, cls, cls_len) == 0) {
            return true;
        }
    }
    return false;
}

void q_dom_add_class(quanton_view_t *view,
                     lxb_dom_element_t *el, const char *cls)
{
    const lxb_char_t *cur;
    size_t class_len;
    size_t cls_len;
    char *buf;

    if (el == NULL || cls == NULL) {
        return;
    }

    if (q_dom_has_class(el, cls)) {
        return;
    }

    cls_len = strlen(cls);
    cur = lxb_dom_element_get_attribute(el,
                                         (const lxb_char_t *) "class", 5,
                                         &class_len);

    if (cur == NULL || class_len == 0) {
        /* No existing class attribute — just set it. */
        q_dom_set_attr(view, el, "class", cls);
        return;
    }

    /* Append " cls" to the existing value. */
    buf = (char *) malloc(class_len + 1 + cls_len + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, cur, class_len);
    buf[class_len] = ' ';
    memcpy(buf + class_len + 1, cls, cls_len);
    buf[class_len + 1 + cls_len] = '\0';

    q_dom_set_attr(view, el, "class", buf);
    free(buf);
}

void q_dom_remove_class(quanton_view_t *view,
                        lxb_dom_element_t *el, const char *cls)
{
    const lxb_char_t *cur;
    size_t class_len;
    size_t cls_len;
    char *buf;
    size_t out_pos;
    size_t i;

    if (el == NULL || cls == NULL) {
        return;
    }

    cls_len = strlen(cls);
    cur = lxb_dom_element_get_attribute(el,
                                         (const lxb_char_t *) "class", 5,
                                         &class_len);
    if (cur == NULL || class_len == 0) {
        return;
    }

    buf = (char *) malloc(class_len + 1);
    if (buf == NULL) {
        return;
    }

    out_pos = 0;
    i = 0;
    while (i < class_len) {
        size_t start;
        size_t tok_len;

        /* skip leading whitespace, preserving a single separator space in output */
        while (i < class_len && (cur[i] == ' ' || cur[i] == '\t' ||
                                  cur[i] == '\n' || cur[i] == '\r')) {
            ++i;
        }
        start = i;
        while (i < class_len && cur[i] != ' ' && cur[i] != '\t' &&
               cur[i] != '\n' && cur[i] != '\r') {
            ++i;
        }
        tok_len = i - start;
        if (tok_len == 0) {
            continue;
        }
        if (tok_len == cls_len &&
                memcmp(cur + start, cls, cls_len) == 0) {
            /* skip this token */
            continue;
        }
        if (out_pos > 0) {
            buf[out_pos++] = ' ';
        }
        memcpy(buf + out_pos, cur + start, tok_len);
        out_pos += tok_len;
    }
    buf[out_pos] = '\0';

    q_dom_set_attr(view, el, "class", buf);
    free(buf);
}

/* ── querySelector ─────────────────────────────────────────────────────── */

typedef struct {
    lxb_dom_element_t **out;
    size_t              out_max;
    size_t              count;
} q_sel_ctx_t;

static lxb_status_t q_selector_cb(lxb_dom_node_t *node,
                                   lxb_css_selector_specificity_t spec,
                                   void *ctx)
{
    q_sel_ctx_t *sctx = (q_sel_ctx_t *) ctx;

    (void) spec;

    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return LXB_STATUS_OK;
    }

    if (sctx->count < sctx->out_max) {
        sctx->out[sctx->count] = lxb_dom_interface_element(node);
    }
    sctx->count++;

    if (sctx->out_max == 1) {
        /* querySelector: stop after first match */
        return LXB_STATUS_STOP;
    }

    return LXB_STATUS_OK;
}

static size_t q_run_selector(quanton_view_t *view,
                              const char *selector,
                              lxb_dom_element_t **out,
                              size_t out_max)
{
    lxb_html_document_t      *html_doc;
    lxb_css_parser_t         *parser;
    lxb_selectors_t          *selectors;
    lxb_css_selector_list_t  *list;
    q_sel_ctx_t               sctx;
    size_t                    sel_len;

    if (view == NULL || view->document == NULL || selector == NULL) {
        return 0;
    }

    html_doc = q_document_handle(view->document);
    if (html_doc == NULL) {
        return 0;
    }

    parser = lxb_css_parser_create();
    if (parser == NULL) {
        return 0;
    }
    if (lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK) {
        (void) lxb_css_parser_destroy(parser, true);
        return 0;
    }

    selectors = lxb_selectors_create();
    if (selectors == NULL) {
        (void) lxb_css_parser_destroy(parser, true);
        return 0;
    }
    if (lxb_selectors_init(selectors) != LXB_STATUS_OK) {
        (void) lxb_selectors_destroy(selectors, true);
        (void) lxb_css_parser_destroy(parser, true);
        return 0;
    }

    sel_len = strlen(selector);
    list = lxb_css_selectors_parse(parser,
                                    (const lxb_char_t *) selector, sel_len);
    if (list == NULL) {
        (void) lxb_selectors_destroy(selectors, true);
        (void) lxb_css_parser_destroy(parser, true);
        return 0;
    }

    sctx.out     = out;
    sctx.out_max = out_max;
    sctx.count   = 0;

    (void) lxb_selectors_find(selectors,
                               lxb_dom_interface_node(html_doc),
                               list,
                               q_selector_cb,
                               &sctx);

    lxb_css_selector_list_destroy_memory(list);
    (void) lxb_selectors_destroy(selectors, true);
    (void) lxb_css_parser_destroy(parser, true);

    return sctx.count;
}

lxb_dom_element_t *q_dom_query_selector(quanton_view_t *view,
                                         const char *selector)
{
    lxb_dom_element_t *result = NULL;

    q_run_selector(view, selector, &result, 1);
    return result;
}

size_t q_dom_query_selector_all(quanton_view_t *view,
                                 const char *selector,
                                 lxb_dom_element_t **out, size_t out_max)
{
    return q_run_selector(view, selector, out, out_max);
}

/* ── getElementById ──────────────────────────────────────────────────────── */

lxb_dom_element_t *q_dom_get_element_by_id(quanton_view_t *view,
                                             const char *id)
{
    lxb_dom_element_t *result = NULL;
    char               selector[256];
    size_t             id_len;
    size_t             sel_len;

    if (id == NULL) {
        return NULL;
    }

    id_len  = strlen(id);
    sel_len = id_len + 1; /* '#' + id */

    if (sel_len >= sizeof(selector)) {
        return NULL;
    }

    selector[0] = '#';
    memcpy(selector + 1, id, id_len + 1); /* includes NUL */

    q_run_selector(view, selector, &result, 1);
    return result;
}

/* ── innerHTML setter ─────────────────────────────────────────────────────── */

/*
 * q_dom_set_inner_html — parse html as a fragment in el's context, replace
 * el's children with the result, and schedule Q_DIRTY_LAYOUT on view.
 *
 * The fragment is parsed using lxb_html_document_parse_fragment (the same
 * mechanism lexbor uses internally for element.innerHTML = "…").
 * Parsed nodes are moved into el; the temporary fragment root is destroyed.
 *
 * Repaint is left to the caller: call q_view_update(view) afterwards.
 *
 * Returns 0 on success, -1 on failure.
 */
int q_dom_set_inner_html(quanton_view_t    *view,
                          lxb_dom_element_t *el,
                          const char        *html,
                          size_t             len)
{
    lxb_html_document_t *doc;
    lxb_dom_node_t      *el_node;
    lxb_dom_node_t      *frag;
    lxb_dom_node_t      *child;

    if (view == NULL || el == NULL || html == NULL) {
        return -1;
    }

    doc = q_document_handle(view->document);
    if (doc == NULL) {
        return -1;
    }

    el_node = lxb_dom_interface_node(el);

    frag = lxb_html_document_parse_fragment(
               doc, el, (const lxb_char_t *) html, len);
    if (frag == NULL) {
        return -1;
    }

    /* Remove all existing children */
    while (el_node->first_child != NULL) {
        lxb_dom_node_destroy_deep(el_node->first_child);
    }

    /* Move parsed nodes from fragment root into el */
    while (frag->first_child != NULL) {
        child = frag->first_child;
        lxb_dom_node_remove(child);
        lxb_dom_node_insert_child(el_node, child);
    }
    lxb_dom_node_destroy(frag);

    q_dom_mark_dirty(view, el_node, Q_DIRTY_LAYOUT);
    return 0;
}
