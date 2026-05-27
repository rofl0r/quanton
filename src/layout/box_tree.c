#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/character_data.h"
#include "lexbor/html/interfaces/document.h"
#include "lexbor/tag/const.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define Q_DEFAULT_BACKGROUND 0xF2F2F2FFu
#define Q_DEFAULT_BORDER 0x303030FFu
#define Q_DEFAULT_BORDER_WIDTH 1.0f
#define Q_CSS_INT_PARSE_BUF_SIZE 64u

/* ─── Inline CSS style attribute parser ─────────────────────────────────── */

/* Case-insensitive compare of a lxb_char_t span against a lowercase literal.
 * The span is [s, s+slen); the literal is a NUL-terminated lowercase string.
 * Returns 1 if equal. */
static int css_name_eq(const lxb_char_t *s, size_t slen, const char *name)
{
    size_t i;
    for (i = 0; i < slen; ++i) {
        if (name[i] == '\0') {
            return 0; /* name is shorter */
        }
        if (tolower((unsigned char) s[i]) != (unsigned char) name[i]) {
            return 0;
        }
    }
    return name[slen] == '\0';
}

/* Case-insensitive compare of the value region [val, val+vlen) (with leading
 * whitespace stripped) against a lowercase keyword.  Trailing whitespace in
 * the value is also ignored. */
static int css_value_is(const lxb_char_t *val, size_t vlen, const char *kw)
{
    size_t kwlen = strlen(kw);
    size_t i = 0;

    /* skip leading space */
    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    if (vlen - i < kwlen) {
        return 0;
    }
    if (!css_name_eq(val + i, kwlen, kw)) {
        return 0;
    }
    i += kwlen;
    /* only trailing space is allowed after the keyword */
    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    return i >= vlen;
}

/* Parse a CSS length value (e.g. "10px", "  20 ", "0") and return it as a
 * float in pixels.  Units other than px are accepted but treated as px.
 * Returns 0.0f on parse failure. */
static float css_parse_length(const lxb_char_t *val, size_t vlen)
{
    char buf[32];
    size_t i = 0;
    size_t n;

    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    n = vlen - i;
    if (n == 0) {
        return 0.0f;
    }
    if (n > sizeof(buf) - 1) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, val + i, n);
    buf[n] = '\0';
    return strtof(buf, NULL);
}

static int css_parse_int(const lxb_char_t *val, size_t vlen, int *out)
{
    char buf[Q_CSS_INT_PARSE_BUF_SIZE];
    size_t i = 0;
    size_t n;
    char *endp;
    long z;

    if (out == NULL) {
        return 0;
    }

    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    n = vlen - i;
    if (n == 0 || n > sizeof(buf) - 1u) {
        return 0;
    }

    memcpy(buf, val + i, n);
    buf[n] = '\0';

    z = strtol(buf, &endp, 10);
    if (endp == buf) {
        return 0;
    }
    while (*endp != '\0') {
        if (!isspace((unsigned char) *endp)) {
            return 0;
        }
        ++endp;
    }
    if (z < INT_MIN || z > INT_MAX) {
        return 0;
    }

    *out = (int) z;
    return 1;
}

/* Parse relevant CSS properties from a style attribute string and apply them
 * to *box.  Handles: display, position, z-index, top/right/bottom/left, width, height. */
static void parse_style_attribute(const lxb_char_t *style, size_t style_len,
                                  q_box_t *box)
{
    size_t i = 0;

    while (i < style_len) {
        size_t prop_start;
        size_t prop_end;
        size_t val_start;
        size_t val_end;
        size_t prop_len;
        size_t val_len;
        const lxb_char_t *prop;
        const lxb_char_t *val;

        /* skip leading whitespace */
        while (i < style_len && isspace((unsigned char) style[i])) {
            ++i;
        }
        if (i >= style_len) {
            break;
        }

        prop_start = i;
        /* scan property name up to ':' or ';' */
        while (i < style_len && style[i] != ':' && style[i] != ';') {
            ++i;
        }
        if (i >= style_len || style[i] != ':') {
            /* no colon — skip to next ';' */
            while (i < style_len && style[i] != ';') {
                ++i;
            }
            if (i < style_len) {
                ++i; /* skip ';' */
            }
            continue;
        }
        prop_end = i;
        ++i; /* skip ':' */

        val_start = i;
        while (i < style_len && style[i] != ';') {
            ++i;
        }
        val_end = i;
        if (i < style_len) {
            ++i; /* skip ';' */
        }

        /* trim trailing whitespace from property name */
        while (prop_end > prop_start
               && isspace((unsigned char) style[prop_end - 1])) {
            --prop_end;
        }
        prop_len = prop_end - prop_start;
        val_len  = val_end  - val_start;
        prop = style + prop_start;
        val  = style + val_start;

        if (css_name_eq(prop, prop_len, "display")) {
            if (css_value_is(val, val_len, "flex")) {
                box->is_flex_container = 1;
            }
        } else if (css_name_eq(prop, prop_len, "position")) {
            if (css_value_is(val, val_len, "absolute")) {
                box->position = Q_POSITION_ABSOLUTE;
            } else if (css_value_is(val, val_len, "fixed")) {
                box->position = Q_POSITION_FIXED;
            } else if (css_value_is(val, val_len, "relative")) {
                box->position = Q_POSITION_RELATIVE;
            }
        } else if (css_name_eq(prop, prop_len, "z-index")) {
            int z;
            if (css_parse_int(val, val_len, &z)) {
                box->has_z_index = 1;
                box->z_index = z;
            }
        } else if (css_name_eq(prop, prop_len, "top")) {
            box->style_top = css_parse_length(val, val_len);
        } else if (css_name_eq(prop, prop_len, "right")) {
            box->style_right = css_parse_length(val, val_len);
        } else if (css_name_eq(prop, prop_len, "bottom")) {
            box->style_bottom = css_parse_length(val, val_len);
        } else if (css_name_eq(prop, prop_len, "left")) {
            box->style_left = css_parse_length(val, val_len);
        } else if (css_name_eq(prop, prop_len, "width")) {
            box->style_width = css_parse_length(val, val_len);
        } else if (css_name_eq(prop, prop_len, "height")) {
            box->style_height = css_parse_length(val, val_len);
        } else if (css_name_eq(prop, prop_len, "overflow-x")) {
            if (css_value_is(val, val_len, "hidden")) {
                box->overflow_x = Q_OVERFLOW_HIDDEN;
            } else if (css_value_is(val, val_len, "clip")) {
                box->overflow_x = Q_OVERFLOW_CLIP;
            } else if (css_value_is(val, val_len, "scroll")) {
                box->overflow_x = Q_OVERFLOW_SCROLL;
            } else if (css_value_is(val, val_len, "auto")) {
                box->overflow_x = Q_OVERFLOW_AUTO;
            } else {
                box->overflow_x = Q_OVERFLOW_VISIBLE;
            }
        } else if (css_name_eq(prop, prop_len, "overflow-y")) {
            if (css_value_is(val, val_len, "hidden")) {
                box->overflow_y = Q_OVERFLOW_HIDDEN;
            } else if (css_value_is(val, val_len, "clip")) {
                box->overflow_y = Q_OVERFLOW_CLIP;
            } else if (css_value_is(val, val_len, "scroll")) {
                box->overflow_y = Q_OVERFLOW_SCROLL;
            } else if (css_value_is(val, val_len, "auto")) {
                box->overflow_y = Q_OVERFLOW_AUTO;
            } else {
                box->overflow_y = Q_OVERFLOW_VISIBLE;
            }
        } else if (css_name_eq(prop, prop_len, "overflow")) {
            q_overflow_type_t ov = Q_OVERFLOW_VISIBLE;
            if (css_value_is(val, val_len, "hidden")) {
                ov = Q_OVERFLOW_HIDDEN;
            } else if (css_value_is(val, val_len, "clip")) {
                ov = Q_OVERFLOW_CLIP;
            } else if (css_value_is(val, val_len, "scroll")) {
                ov = Q_OVERFLOW_SCROLL;
            } else if (css_value_is(val, val_len, "auto")) {
                ov = Q_OVERFLOW_AUTO;
            }
            box->overflow_x = ov;
            box->overflow_y = ov;
        }
    }
}

/* ─── Box tree ────────────────────────────────────────────────────────────── */

static q_box_t *q_box_create(q_box_type_t type, lxb_dom_node_t *dom_node,
                              const char *text, size_t text_len)
{
    q_box_t *box = (q_box_t *) calloc(1, sizeof(*box));
    if (box == NULL) {
        return NULL;
    }

    box->type = type;
    box->dom_node = dom_node;
    box->text = text;
    box->text_len = text_len;
    box->background_color = Q_DEFAULT_BACKGROUND;
    box->border_color[0] = Q_DEFAULT_BORDER;
    box->border_color[1] = Q_DEFAULT_BORDER;
    box->border_color[2] = Q_DEFAULT_BORDER;
    box->border_color[3] = Q_DEFAULT_BORDER;
    box->border_width[0] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[1] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[2] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[3] = Q_DEFAULT_BORDER_WIDTH;

    if (type == Q_BOX_IMAGE) {
        box->background_color = 0x00000000u;
        box->border_width[0] = 0.0f;
        box->border_width[1] = 0.0f;
        box->border_width[2] = 0.0f;
        box->border_width[3] = 0.0f;
    }

    /* NaN sentinel = "not set" for all explicit style dimensions / offsets */
    box->style_top    = (float) NAN;
    box->style_right  = (float) NAN;
    box->style_bottom = (float) NAN;
    box->style_left   = (float) NAN;
    box->style_width  = (float) NAN;
    box->style_height = (float) NAN;

    return box;
}

static int q_box_append_child(q_box_t *parent, q_box_t *child)
{
    child->parent = parent;

    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
    } else {
        parent->first_child = child;
    }

    parent->last_child = child;
    return 0;
}

static void q_box_load_image(q_document_t *doc, q_box_t *box, lxb_dom_node_t *node)
{
    lxb_dom_element_t *element;
    const lxb_char_t *src;
    size_t src_len = 0;
    char *src_buf;
    char *resolved;

    if (doc == NULL || box == NULL || node == NULL
        || node->type != LXB_DOM_NODE_TYPE_ELEMENT
        || lxb_dom_node_tag_id(node) != LXB_TAG_IMG)
    {
        return;
    }

    element = lxb_dom_interface_element(node);
    src = lxb_dom_element_get_attribute(element, (const lxb_char_t *) "src", 3, &src_len);
    if (src == NULL || src_len == 0u) {
        return;
    }

    src_buf = (char *) malloc(src_len + 1u);
    if (src_buf == NULL) {
        return;
    }

    memcpy(src_buf, src, src_len);
    src_buf[src_len] = '\0';

    resolved = q_url_resolve(q_document_base_url(doc), src_buf);
    free(src_buf);
    if (resolved == NULL) {
        return;
    }

    box->image = q_image_load_url(resolved);
    free(resolved);
}

static int q_text_is_whitespace(const lxb_char_t *text, size_t len)
{
    size_t i;

    for (i = 0; i < len; ++i) {
        if (!isspace((unsigned char) text[i])) {
            return 0;
        }
    }

    return 1;
}

static int q_layout_walk_node(q_document_t *doc, lxb_dom_node_t *node, q_box_t *parent)
{
    q_box_t *current = NULL;
    q_box_t *child_parent;
    lxb_dom_node_t *child;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT
        || node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
    {
        q_box_type_t type = Q_BOX_BLOCK;

        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT
            && lxb_dom_node_tag_id(node) == LXB_TAG_IMG)
        {
            type = Q_BOX_IMAGE;
        }

        current = q_box_create(type, node, NULL, 0);
        if (current != NULL && lxb_dom_node_type(node) == LXB_DOM_NODE_TYPE_ELEMENT) {
            size_t style_len = 0;
            const lxb_char_t *style =
                lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                              (const lxb_char_t *) "style",
                                              sizeof("style") - 1,
                                              &style_len);
            if (style != NULL && style_len > 0) {
                parse_style_attribute(style, style_len, current);
            }
            if (type == Q_BOX_IMAGE) {
                q_box_load_image(doc, current, node);
            }
        }
    }
    else if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_character_data_t *ch_data = (lxb_dom_character_data_t *) node;

        if (ch_data->data.length != 0
            && !q_text_is_whitespace(ch_data->data.data, ch_data->data.length))
        {
            q_box_t *ic;
            q_box_t *text_box;

            /* Reuse or create anonymous inline container */
            if (parent != NULL && parent->last_child != NULL
                && parent->last_child->type == Q_BOX_INLINE_CONTAINER)
            {
                ic = parent->last_child;
            } else {
                /* Anonymous box — no direct DOM node correspondence */
                ic = q_box_create(Q_BOX_INLINE_CONTAINER, NULL, NULL, 0);
                if (ic == NULL) {
                    return -1;
                }
                if (parent != NULL && q_box_append_child(parent, ic) != 0) {
                    free(ic);
                    return -1;
                }
            }

            text_box = q_box_create(Q_BOX_TEXT, node,
                                    (const char *) ch_data->data.data,
                                    ch_data->data.length);
            if (text_box == NULL) {
                return -1;
            }
            if (q_box_append_child(ic, text_box) != 0) {
                free(text_box);
                return -1;
            }
        }
        /* Text nodes have no DOM children; nothing more to do */
        return 0;
    }

    if (current != NULL && parent != NULL && q_box_append_child(parent, current) != 0) {
        free(current);
        return -1;
    }

    child_parent = (current != NULL) ? current : parent;

    for (child = node->first_child; child != NULL; child = child->next) {
        if (q_layout_walk_node(doc, child, child_parent) != 0) {
            return -1;
        }
    }

    return 0;
}

q_box_t *q_layout_build_tree(q_document_t *doc)
{
    lxb_html_document_t *document;
    lxb_html_body_element_t *body;
    lxb_dom_node_t *root_node;
    q_box_t *root;

    if (doc == NULL) {
        return NULL;
    }

    document = q_document_handle(doc);
    if (document == NULL) {
        return NULL;
    }

    body = lxb_html_document_body_element(document);
    if (body != NULL) {
        root_node = lxb_dom_interface_node(body);
    } else {
        root_node = lxb_dom_interface_node(document);
    }

    root = q_box_create(Q_BOX_BLOCK, root_node, NULL, 0);
    if (root == NULL) {
        return NULL;
    }

    for (root_node = root_node->first_child; root_node != NULL; root_node = root_node->next) {
        if (q_layout_walk_node(doc, root_node, root) != 0) {
            q_layout_free_tree(root);
            return NULL;
        }
    }

    return root;
}

void q_layout_free_tree(q_box_t *root)
{
    q_box_t *child;
    q_box_t *next;

    if (root == NULL) {
        return;
    }

    child = root->first_child;
    while (child != NULL) {
        next = child->next_sibling;
        q_layout_free_tree(child);
        child = next;
    }

    q_shaped_run_free(root->run);
    q_image_release(root->image);
    free(root->tile);
    free(root);
}
