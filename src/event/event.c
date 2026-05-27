#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"

#include <string.h>

static int q_event_is_mouse_event(q_event_type_t type)
{
    return type == Q_EVENT_MOUSE_MOVE
        || type == Q_EVENT_MOUSE_DOWN
        || type == Q_EVENT_MOUSE_UP
        || type == Q_EVENT_MOUSE_CLICK
        || type == Q_EVENT_MOUSE_WHEEL;
}

static int q_box_contains_point(const q_box_t *box, int x, int y)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if (box == NULL) {
        return 0;
    }

    x0 = (int) box->x;
    y0 = (int) box->y;
    x1 = x0 + (int) box->width;
    y1 = y0 + (int) box->height;

    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

static q_box_t *q_hit_test_deepest(q_box_t *box, int x, int y)
{
    q_box_t *child;

    if (!q_box_contains_point(box, x, y)) {
        return NULL;
    }

    /* Reverse order so the most recently painted (topmost) child wins. */
    for (child = box->last_child; child != NULL; child = child->prev_sibling) {
        q_box_t *hit = q_hit_test_deepest(child, x, y);
        if (hit != NULL) {
            return hit;
        }
    }

    return box;
}

q_box_t *q_hit_test(q_box_t *root, int x, int y)
{
    if (root == NULL) {
        return NULL;
    }

    return q_hit_test_deepest(root, x, y);
}

void q_event_dispatch(quanton_view_t *view, q_event_t *event)
{
    if (view == NULL || event == NULL) {
        return;
    }

    if (q_event_is_mouse_event(event->type)) {
        event->target_box = q_hit_test(view->layout_root, event->mouse_x, event->mouse_y);
        event->target = (event->target_box != NULL) ? event->target_box->dom_node : NULL;
    }

    if (view->on_event != NULL) {
        view->on_event(view, event, view->on_event_userdata);
    }
}

lxb_dom_node_t *q_event_find_delegate(lxb_dom_node_t *node, const char *attr_name)
{
    size_t name_len;

    if (node == NULL || attr_name == NULL || attr_name[0] == '\0') {
        return NULL;
    }

    name_len = strlen(attr_name);

    while (node != NULL) {
        if (lxb_dom_node_type(node) == LXB_DOM_NODE_TYPE_ELEMENT
            && lxb_dom_element_attr_by_name(lxb_dom_interface_element(node),
                                            (const lxb_char_t *) attr_name,
                                            name_len) != NULL)
        {
            return node;
        }
        node = lxb_dom_node_parent(node);
    }

    return NULL;
}
