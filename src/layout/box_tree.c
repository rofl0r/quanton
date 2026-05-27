#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/character_data.h"
#include "lexbor/html/interfaces/document.h"

#include <ctype.h>
#include <stdlib.h>

#define Q_DEFAULT_BACKGROUND 0xF2F2F2FFu
#define Q_DEFAULT_BORDER 0x303030FFu
#define Q_DEFAULT_BORDER_WIDTH 1.0f

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

static int q_layout_walk_node(lxb_dom_node_t *node, q_box_t *parent)
{
    q_box_t *current = NULL;
    q_box_t *child_parent;
    lxb_dom_node_t *child;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT
        || node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
    {
        current = q_box_create(Q_BOX_BLOCK, node, NULL, 0);
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
                ic = q_box_create(Q_BOX_INLINE_CONTAINER, node->parent, NULL, 0);
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
        if (q_layout_walk_node(child, child_parent) != 0) {
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
        if (q_layout_walk_node(root_node, root) != 0) {
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
    free(root->tile);
    free(root);
}
