#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"

#include <math.h>
#include <string.h>

#define Q_EVENT_WHEEL_SCROLL_PX 40.0f

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

static int q_box_scrolls_x(const q_box_t *box)
{
    return box != NULL
        && (box->overflow_x == Q_OVERFLOW_SCROLL || box->overflow_x == Q_OVERFLOW_AUTO);
}

static int q_box_scrolls_y(const q_box_t *box)
{
    return box != NULL
        && (box->overflow_y == Q_OVERFLOW_SCROLL || box->overflow_y == Q_OVERFLOW_AUTO);
}

static void q_box_content_extent(const q_box_t *box, float *out_w, float *out_h)
{
    q_box_t *child;
    float max_w = 0.0f;
    float max_h = 0.0f;

    if (box == NULL || out_w == NULL || out_h == NULL) {
        return;
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        float local_right = (child->x - box->x) + child->width;
        float local_bottom = (child->y - box->y) + child->height;
        if (local_right > max_w) {
            max_w = local_right;
        }
        if (local_bottom > max_h) {
            max_h = local_bottom;
        }
    }

    if (max_w < 0.0f) {
        max_w = 0.0f;
    }
    if (max_h < 0.0f) {
        max_h = 0.0f;
    }
    *out_w = max_w;
    *out_h = max_h;
}

static float q_box_scroll_max_x(const q_box_t *box)
{
    float content_w = 0.0f;
    float content_h_dummy = 0.0f;
    float border_left;
    float border_right;
    float viewport_w;
    float max_x;

    if (!q_box_scrolls_x(box)) {
        return 0.0f;
    }

    q_box_content_extent(box, &content_w, &content_h_dummy);
    border_left = ceilf(box->border_width[3]);
    border_right = ceilf(box->border_width[1]);
    viewport_w = box->width - border_left - border_right;
    if (viewport_w < 0.0f) {
        viewport_w = 0.0f;
    }
    max_x = content_w - viewport_w;
    return (max_x > 0.0f) ? max_x : 0.0f;
}

static float q_box_scroll_max_y(const q_box_t *box)
{
    float content_w_dummy = 0.0f;
    float content_h = 0.0f;
    float border_top;
    float border_bottom;
    float viewport_h;
    float max_y;

    if (!q_box_scrolls_y(box)) {
        return 0.0f;
    }

    q_box_content_extent(box, &content_w_dummy, &content_h);
    border_top = ceilf(box->border_width[0]);
    border_bottom = ceilf(box->border_width[2]);
    viewport_h = box->height - border_top - border_bottom;
    if (viewport_h < 0.0f) {
        viewport_h = 0.0f;
    }
    max_y = content_h - viewport_h;
    return (max_y > 0.0f) ? max_y : 0.0f;
}

static float q_clamp_scroll(float value, float max_value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static q_box_t *q_find_deepest_text_descendant(q_box_t *box)
{
    q_box_t *child;
    q_box_t *match;

    if (box == NULL) {
        return NULL;
    }

    if (box->type == Q_BOX_TEXT) {
        return box;
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        match = q_find_deepest_text_descendant(child);
        if (match != NULL) {
            return match;
        }
    }

    return NULL;
}

static q_box_t *q_hit_test_deepest(q_box_t *box, int x, int y)
{
    q_box_t *child;
    int child_x = x;
    int child_y = y;

    if (!q_box_contains_point(box, x, y)) {
        return NULL;
    }

    if (q_box_scrolls_x(box)) {
        child_x += (int) lroundf(box->scroll_x);
    }
    if (q_box_scrolls_y(box)) {
        child_y += (int) lroundf(box->scroll_y);
    }

    /* Reverse order so the most recently painted (topmost) child wins. */
    for (child = box->last_child; child != NULL; child = child->prev_sibling) {
        q_box_t *hit = q_hit_test_deepest(child, child_x, child_y);
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
    int hit_x;
    int hit_y;
    q_box_t *scroll_box;
    q_box_t *text_target;
    float max_scroll;
    float old_scroll;
    int scrolled;

    if (view == NULL || event == NULL) {
        return;
    }

    scroll_box = NULL;
    scrolled = 0;

    if (q_event_is_mouse_event(event->type)) {
        hit_x = event->mouse_x + (int) lroundf(view->scroll_x);
        hit_y = event->mouse_y + (int) lroundf(view->scroll_y);
        event->target_box = q_hit_test(view->layout_root, hit_x, hit_y);
        text_target = q_find_deepest_text_descendant(event->target_box);
        if (text_target != NULL) {
            event->target_box = text_target;
        }
        event->target = (event->target_box != NULL) ? event->target_box->dom_node : NULL;
    }

    if (event->type == Q_EVENT_MOUSE_WHEEL) {
        for (scroll_box = event->target_box; scroll_box != NULL; scroll_box = scroll_box->parent) {
            if (q_box_scrolls_y(scroll_box) || q_box_scrolls_x(scroll_box)) {
                break;
            }
        }

        if (scroll_box != NULL && q_box_scrolls_y(scroll_box)) {
            max_scroll = q_box_scroll_max_y(scroll_box);
            old_scroll = scroll_box->scroll_y;
            scroll_box->scroll_y = q_clamp_scroll(scroll_box->scroll_y
                                                  + ((float) (-event->wheel_delta) * Q_EVENT_WHEEL_SCROLL_PX),
                                                  max_scroll);
            if (scroll_box->scroll_y != old_scroll) {
                view->dirty_flags |= Q_DIRTY_SCROLL;
                q_view_update(view);
                scrolled = 1;
            }
        }

        if (!scrolled && scroll_box != NULL && q_box_scrolls_x(scroll_box)) {
            max_scroll = q_box_scroll_max_x(scroll_box);
            old_scroll = scroll_box->scroll_x;
            scroll_box->scroll_x = q_clamp_scroll(scroll_box->scroll_x
                                                  + ((float) (-event->wheel_delta) * Q_EVENT_WHEEL_SCROLL_PX),
                                                  max_scroll);
            if (scroll_box->scroll_x != old_scroll) {
                view->dirty_flags |= Q_DIRTY_SCROLL;
                q_view_update(view);
                scrolled = 1;
            }
        }

        if (!scrolled) {
            q_view_scroll_by(view, 0.0f,
                             (float) (-event->wheel_delta) * Q_EVENT_WHEEL_SCROLL_PX);
        }
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
