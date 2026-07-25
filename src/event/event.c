#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Define QUANTON_DEBUG_EVENTS (e.g. -DQUANTON_DEBUG_EVENTS in CFLAGS) to get
 * per-event diagnostic output on stderr.  The output shows:
 *
 *   WHEEL  x,y  → hit:<ptr>  scroll_box:<ptr>  old_scroll→new_scroll
 *   CLICK  x,y  → hit:<ptr>  href:<href>  (anchor navigation details)
 *
 * This is invaluable for diagnosing why scrolling or anchor links do not work
 * in layouts that use overflow:auto / overflow:scroll panels.
 */
#ifdef QUANTON_DEBUG_EVENTS
static const char *q_dbg_box_type_name(q_box_type_t t)
{
    switch (t) {
    case Q_BOX_BLOCK:             return "BLOCK";
    case Q_BOX_IMAGE:             return "IMAGE";
    case Q_BOX_TEXT:              return "TEXT";
    case Q_BOX_LINE_BREAK:        return "BR";
    case Q_BOX_INLINE_CONTAINER:  return "INLINE_CTR";
    case Q_BOX_LINE:              return "LINE";
    case Q_BOX_TABLE:             return "TABLE";
    case Q_BOX_TABLE_SECTION:     return "TSECTION";
    case Q_BOX_TABLE_ROW:         return "TROW";
    case Q_BOX_TABLE_CELL:        return "TCELL";
    case Q_BOX_TABLE_CAPTION:     return "TCAPTION";
    default:                      return "?";
    }
}

static void q_dbg_print_box(const char *label, const q_box_t *box)
{
    if (box == NULL) {
        fprintf(stderr, "  %s: NULL\n", label);
        return;
    }
    fprintf(stderr, "  %s: type=%s x=%.0f y=%.0f w=%.0f h=%.0f"
                    " scroll_x=%.0f scroll_y=%.0f"
                    " overflow_x=%d overflow_y=%d\n",
            label,
            q_dbg_box_type_name(box->type),
            (double) box->x, (double) box->y,
            (double) box->width, (double) box->height,
            (double) box->scroll_x, (double) box->scroll_y,
            (int) box->overflow_x, (int) box->overflow_y);
}
#endif /* QUANTON_DEBUG_EVENTS */

#define Q_EVENT_WHEEL_SCROLL_PX 40.0f
#define Q_WIDGET_TEXT_INPUT_PRINTABLE_MIN 0x20u
#define Q_WIDGET_TEXT_INPUT_PRINTABLE_MAX 0x7Eu

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

static q_box_t *q_find_text_descendant_at_point(q_box_t *box, int x, int y)
{
    q_box_t *child;
    int child_x = x;
    int child_y = y;

    if (box == NULL || !q_box_contains_point(box, x, y)) {
        return NULL;
    }
    if (box->type == Q_BOX_TEXT) {
        return box;
    }
    if (q_box_scrolls_x(box)) {
        child_x += (int) lroundf(box->scroll_x);
    }
    if (q_box_scrolls_y(box)) {
        child_y += (int) lroundf(box->scroll_y);
    }

    for (child = box->last_child; child != NULL; child = child->prev_sibling) {
        q_box_t *hit = q_find_text_descendant_at_point(child, child_x, child_y);
        if (hit != NULL) {
            return hit;
        }
    }
    return NULL;
}

static q_box_t *q_widget_box_from_target(q_box_t *box)
{
    while (box != NULL && box->widget_type == Q_WIDGET_NONE) {
        box = box->parent;
    }
    return box;
}

static void q_widget_set_focus(quanton_view_t *view, q_box_t *box)
{
    q_box_t *old_focus;

    if (view == NULL) {
        return;
    }

    old_focus = view->focused_widget;
    if (old_focus != NULL && old_focus != box) {
        old_focus->widget_focused = 0;
        if (view->on_event != NULL) {
            q_event_t blur_event;
            memset(&blur_event, 0, sizeof(blur_event));
            blur_event.type = Q_EVENT_BLUR;
            blur_event.target_box = old_focus;
            blur_event.target = old_focus->dom_node;
            view->on_event(view, &blur_event, view->on_event_userdata);
        }
    }

    view->focused_widget = box;
    if (box != NULL) {
        box->widget_focused = 1;
        if (old_focus != box && view->on_event != NULL) {
            q_event_t focus_event;
            memset(&focus_event, 0, sizeof(focus_event));
            focus_event.type = Q_EVENT_FOCUS;
            focus_event.target_box = box;
            focus_event.target = box->dom_node;
            view->on_event(view, &focus_event, view->on_event_userdata);
        }
    }
}

static void q_widget_insert_char(q_box_t *box, uint32_t ch)
{
    char *buf;
    size_t caret;
    size_t len;

    if (box == NULL || ch < Q_WIDGET_TEXT_INPUT_PRINTABLE_MIN
        || ch > Q_WIDGET_TEXT_INPUT_PRINTABLE_MAX)
    {
        return;
    }

    len = box->widget_value_len;
    caret = (box->widget_caret > len) ? len : box->widget_caret;
    buf = (char *) malloc(len + 2u);
    if (buf == NULL) {
        return;
    }

    if (box->widget_value != NULL && caret > 0u) {
        memcpy(buf, box->widget_value, caret);
    }
    buf[caret] = (char) ch;
    if (box->widget_value != NULL && len > caret) {
        memcpy(buf + caret + 1, box->widget_value + caret, len - caret);
    }
    buf[len + 1u] = '\0';

    free(box->widget_value);
    box->widget_value = buf;
    box->widget_value_len = len + 1u;
    box->widget_caret = caret + 1u;
}

static void q_widget_delete_char(q_box_t *box)
{
    size_t caret;
    size_t len;
    char *buf;

    if (box == NULL) {
        return;
    }

    len = box->widget_value_len;
    caret = (box->widget_caret > len) ? len : box->widget_caret;
    if (len == 0u || caret == 0u) {
        return;
    }

    buf = (char *) malloc(len);
    if (buf == NULL) {
        return;
    }

    if (caret > 1u) {
        memcpy(buf, box->widget_value, caret - 1u);
    }
    if (box->widget_value != NULL && len > caret) {
        memcpy(buf + caret - 1u, box->widget_value + caret, len - caret);
    }
    buf[len - 1u] = '\0';

    free(box->widget_value);
    box->widget_value = buf;
    box->widget_value_len = len - 1u;
    box->widget_caret = caret - 1u;
}

static void q_widget_clear_radio_group(q_box_t *box, const lxb_char_t *name, size_t name_len,
                                       q_box_t *skip_box)
{
    q_box_t *child;

    if (box == NULL || box->dom_node == NULL) {
        return;
    }

    if (box->widget_type == Q_WIDGET_INPUT_RADIO && box != skip_box) {
        lxb_dom_element_t *el = lxb_dom_interface_element(box->dom_node);
        size_t attr_len = 0;
        const lxb_char_t *attr = lxb_dom_element_get_attribute(el,
                                                                 (const lxb_char_t *) "name",
                                                                 4,
                                                                 &attr_len);
        if (attr != NULL && attr_len == name_len && memcmp(attr, name, name_len) == 0) {
            box->widget_checked = 0;
        }
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        q_widget_clear_radio_group(child, name, name_len, skip_box);
    }
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

/* Walk the layout tree (depth-first) to find the first box whose dom_node
 * matches the given node.  Used by the named-anchor scroll handler. */
static q_box_t *q_find_box_for_node(q_box_t *root, const lxb_dom_node_t *node)
{
    q_box_t *child;
    q_box_t *match;

    if (root == NULL) {
        return NULL;
    }
    if (root->dom_node == node) {
        return root;
    }
    for (child = root->first_child; child != NULL; child = child->next_sibling) {
        match = q_find_box_for_node(child, node);
        if (match != NULL) {
            return match;
        }
    }
    return NULL;
}

static int q_scroll_target_in_box(quanton_view_t *view, q_box_t *scroll_box, q_box_t *target)
{
    float border_left;
    float border_right;
    float border_top;
    float border_bottom;
    float viewport_w;
    float viewport_h;
    float target_x;
    float target_y;
    float target_right;
    float target_bottom;
    float max_scroll_x;
    float max_scroll_y;
    float old_x;
    float old_y;

    if (view == NULL || scroll_box == NULL || target == NULL) {
        return 0;
    }

    border_left = ceilf(scroll_box->border_width[3]);
    border_right = ceilf(scroll_box->border_width[1]);
    border_top = ceilf(scroll_box->border_width[0]);
    border_bottom = ceilf(scroll_box->border_width[2]);
    viewport_w = scroll_box->width - border_left - border_right;
    viewport_h = scroll_box->height - border_top - border_bottom;
    if (viewport_w < 0.0f) {
        viewport_w = 0.0f;
    }
    if (viewport_h < 0.0f) {
        viewport_h = 0.0f;
    }

    target_x = target->x - scroll_box->x;
    target_y = target->y - scroll_box->y;
    target_right = target_x + target->width;
    target_bottom = target_y + target->height;

    old_x = scroll_box->scroll_x;
    old_y = scroll_box->scroll_y;

    if (q_box_scrolls_x(scroll_box)) {
        if (target_x < scroll_box->scroll_x) {
            scroll_box->scroll_x = target_x;
        } else if (target_right > scroll_box->scroll_x + viewport_w) {
            scroll_box->scroll_x = target_right - viewport_w;
        }
        max_scroll_x = q_box_scroll_max_x(scroll_box);
        scroll_box->scroll_x = q_clamp_scroll(scroll_box->scroll_x, max_scroll_x);
    }

    if (q_box_scrolls_y(scroll_box)) {
        if (target_y < scroll_box->scroll_y) {
            scroll_box->scroll_y = target_y;
        } else if (target_bottom > scroll_box->scroll_y + viewport_h) {
            scroll_box->scroll_y = target_bottom - viewport_h;
        }
        max_scroll_y = q_box_scroll_max_y(scroll_box);
        scroll_box->scroll_y = q_clamp_scroll(scroll_box->scroll_y, max_scroll_y);
    }

    if (scroll_box->scroll_x != old_x || scroll_box->scroll_y != old_y) {
        /*
         * An inner scroll box changed position.  Q_DIRTY_SCROLL is only
         * correct for the root view (it merely re-composites the pre-painted
         * root tile at a new offset).  For inner panels, we reuse the cached
         * child tiles and recompose the ancestor tiles so the new
         * scroll_x/scroll_y is baked in without re-rendering text and glyphs.
         */
        view->dirty_flags |= Q_DIRTY_RECOMPOSE;
        q_view_update(view);
        return 1;
    }

    return 0;
}

static void q_scroll_target_into_view(quanton_view_t *view, q_box_t *target)
{
    q_box_t *ancestor;

    if (view == NULL || target == NULL) {
        return;
    }

    for (ancestor = target->parent; ancestor != NULL; ancestor = ancestor->parent) {
        if (q_box_scrolls_x(ancestor) || q_box_scrolls_y(ancestor)) {
            if (q_scroll_target_in_box(view, ancestor, target)) {
                return;
            }
        }
    }

    q_view_scroll_into_view(view, target);
}

/* Handle a hyperlink click.  Named anchors (href="#id") scroll to the target
 * in the nearest scrollable ancestor (falling back to view scroll).  All
 * other hrefs are delegated to view->on_navigate if the callback is set. */
static void q_handle_anchor_click(quanton_view_t *view, q_box_t *source_box, const char *href)
{
    if (href == NULL || href[0] == '\0') {
        return;
    }

    if (href[0] == '#') {
        /* Named anchor: find the target element by its id and scroll to it. */
        const char *id = href + 1;
        lxb_dom_element_t *target_el;
        q_box_t *target_box;

        if (id[0] == '\0') {
            return;
        }
        target_el = q_dom_get_element_by_id(view, id);
        if (target_el == NULL) {
#ifdef QUANTON_DEBUG_EVENTS
            fprintf(stderr, "  anchor \"#%s\": element not found\n", id);
#endif
            return;
        }
        target_box = q_find_box_for_node(view->layout_root,
                                         lxb_dom_interface_node(target_el));
        if (target_box == NULL) {
#ifdef QUANTON_DEBUG_EVENTS
            fprintf(stderr, "  anchor \"#%s\": box not found in layout tree\n", id);
#endif
            return;
        }
#ifdef QUANTON_DEBUG_EVENTS
        fprintf(stderr, "  anchor \"#%s\": target_box x=%.0f y=%.0f w=%.0f h=%.0f\n",
                id,
                (double) target_box->x, (double) target_box->y,
                (double) target_box->width, (double) target_box->height);
#endif
        if (source_box != NULL) {
            q_box_t *ancestor;
            for (ancestor = source_box->parent; ancestor != NULL; ancestor = ancestor->parent) {
                if ((q_box_scrolls_x(ancestor) || q_box_scrolls_y(ancestor))
                    && q_scroll_target_in_box(view, ancestor, target_box))
                {
#ifdef QUANTON_DEBUG_EVENTS
                    fprintf(stderr, "  anchor \"#%s\": scrolled inner box"
                                    " (box x=%.0f y=%.0f scroll_y=%.0f)\n",
                            id,
                            (double) ancestor->x, (double) ancestor->y,
                            (double) ancestor->scroll_y);
#endif
                    return;
                }
            }
        }
#ifdef QUANTON_DEBUG_EVENTS
        fprintf(stderr, "  anchor \"#%s\": no inner scroll box scrolled;"
                        " falling back to root scroll\n", id);
#endif
        q_scroll_target_into_view(view, target_box);
    } else if (view->on_navigate != NULL) {
        /* External or app link: delegate to the host application. */
        view->on_navigate(view, href, view->on_navigate_userdata);
    }
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
        if (event->type != Q_EVENT_MOUSE_WHEEL) {
            text_target = q_find_text_descendant_at_point(event->target_box, hit_x, hit_y);
            if (text_target == NULL) {
                text_target = q_find_deepest_text_descendant(event->target_box);
            }
            if (text_target != NULL) {
                event->target_box = text_target;
            }
        }
        event->target = (event->target_box != NULL) ? event->target_box->dom_node : NULL;
    }

    if (event->type == Q_EVENT_MOUSE_DOWN && event->mouse_button == 0) {
        q_box_t *widget_box = q_widget_box_from_target(event->target_box);
        if (widget_box != NULL) {
            if (widget_box->widget_type == Q_WIDGET_INPUT_TEXT
                || widget_box->widget_type == Q_WIDGET_TEXTAREA)
            {
                q_widget_set_focus(view, widget_box);
                widget_box->widget_caret = widget_box->widget_value_len;
                view->dirty_flags |= Q_DIRTY_PAINT;
                q_view_update(view);
            } else if (widget_box->widget_type == Q_WIDGET_BUTTON) {
                widget_box->widget_pressed = 1;
                view->dirty_flags |= Q_DIRTY_PAINT;
                q_view_update(view);
            }
        }
    }

    if (event->type == Q_EVENT_MOUSE_UP && event->mouse_button == 0) {
        q_box_t *widget_box = q_widget_box_from_target(event->target_box);
        if (widget_box != NULL) {
            if (widget_box->widget_type == Q_WIDGET_BUTTON && widget_box->widget_pressed) {
                widget_box->widget_pressed = 0;
                view->dirty_flags |= Q_DIRTY_PAINT;
                q_view_update(view);
                if (view->on_event != NULL) {
                    q_event_t click_event;
                    memset(&click_event, 0, sizeof(click_event));
                    click_event.type = Q_EVENT_MOUSE_CLICK;
                    click_event.mouse_button = event->mouse_button;
                    click_event.target_box = widget_box;
                    click_event.target = widget_box->dom_node;
                    view->on_event(view, &click_event, view->on_event_userdata);
                }
            } else if (widget_box->widget_type == Q_WIDGET_INPUT_CHECK
                       || widget_box->widget_type == Q_WIDGET_INPUT_RADIO)
            {
                size_t name_len = 0;
                const lxb_char_t *name_attr;
                q_box_t *root = view->layout_root;
                if (widget_box->widget_checked) {
                    widget_box->widget_checked = 0;
                } else {
                    widget_box->widget_checked = 1;
                }
                if (widget_box->widget_type == Q_WIDGET_INPUT_RADIO) {
                    name_attr = lxb_dom_element_get_attribute(
                        lxb_dom_interface_element(widget_box->dom_node),
                        (const lxb_char_t *) "name", 4, &name_len);
                    if (name_attr != NULL && name_len > 0u) {
                        q_widget_clear_radio_group(root, name_attr, name_len, widget_box);
                    }
                }
                view->dirty_flags |= Q_DIRTY_PAINT;
                q_view_update(view);
                if (view->on_event != NULL) {
                    q_event_t change_event;
                    memset(&change_event, 0, sizeof(change_event));
                    change_event.type = Q_EVENT_CHANGE;
                    change_event.target_box = widget_box;
                    change_event.target = widget_box->dom_node;
                    view->on_event(view, &change_event, view->on_event_userdata);
                }
            }
        }
    }

    if (event->type == Q_EVENT_MOUSE_WHEEL) {
        for (scroll_box = event->target_box; scroll_box != NULL; scroll_box = scroll_box->parent) {
            if (q_box_scrolls_y(scroll_box) || q_box_scrolls_x(scroll_box)) {
                break;
            }
        }

#ifdef QUANTON_DEBUG_EVENTS
        fprintf(stderr, "[event] WHEEL delta=%d mouse=(%d,%d) hit_xy=(%d,%d)\n",
                event->wheel_delta, event->mouse_x, event->mouse_y, hit_x, hit_y);
        q_dbg_print_box("  hit_box", event->target_box);
        q_dbg_print_box("  scroll_box", scroll_box);
        if (scroll_box != NULL) {
            float dbg_max = q_box_scroll_max_y(scroll_box);
            fprintf(stderr, "  scroll_max_y=%.0f current_scroll_y=%.0f\n",
                    (double) dbg_max, (double) scroll_box->scroll_y);
        }
#endif

        if (scroll_box != NULL && q_box_scrolls_y(scroll_box)) {
            max_scroll = q_box_scroll_max_y(scroll_box);
            old_scroll = scroll_box->scroll_y;
            scroll_box->scroll_y = q_clamp_scroll(scroll_box->scroll_y
                                                  + ((float) (-event->wheel_delta) * Q_EVENT_WHEEL_SCROLL_PX),
                                                  max_scroll);
            if (scroll_box->scroll_y != old_scroll) {
                /*
                 * Inner scroll box changed: reuse the cached child tiles and
                 * recompose the ancestor tiles so the new scroll_y is baked
                 * in without re-rendering text and glyphs.
                 */
                view->dirty_flags |= Q_DIRTY_RECOMPOSE;
                q_view_update(view);
                scrolled = 1;
#ifdef QUANTON_DEBUG_EVENTS
                fprintf(stderr, "  scroll_y: %.0f → %.0f (repaint)\n",
                        (double) old_scroll, (double) scroll_box->scroll_y);
#endif
            }
        }

        if (!scrolled && scroll_box != NULL && q_box_scrolls_x(scroll_box)) {
            max_scroll = q_box_scroll_max_x(scroll_box);
            old_scroll = scroll_box->scroll_x;
            scroll_box->scroll_x = q_clamp_scroll(scroll_box->scroll_x
                                                  + ((float) (-event->wheel_delta) * Q_EVENT_WHEEL_SCROLL_PX),
                                                  max_scroll);
            if (scroll_box->scroll_x != old_scroll) {
                /* Same rationale as above. */
                view->dirty_flags |= Q_DIRTY_RECOMPOSE;
                q_view_update(view);
                scrolled = 1;
#ifdef QUANTON_DEBUG_EVENTS
                fprintf(stderr, "  scroll_x: %.0f → %.0f (repaint)\n",
                        (double) old_scroll, (double) scroll_box->scroll_x);
#endif
            }
        }

        if (!scrolled) {
#ifdef QUANTON_DEBUG_EVENTS
            fprintf(stderr, "  no inner scroll box found; falling back to root scroll\n");
#endif
            q_view_scroll_by(view, 0.0f,
                             (float) (-event->wheel_delta) * Q_EVENT_WHEEL_SCROLL_PX);
        }
    }

    if (event->type == Q_EVENT_KEY_DOWN) {
        q_box_t *focused_widget = view->focused_widget;
        int changed = 0;
        if (focused_widget != NULL
            && (focused_widget->widget_type == Q_WIDGET_INPUT_TEXT
                || focused_widget->widget_type == Q_WIDGET_TEXTAREA))
        {
            if (event->key_sym == Q_KEY_BACKSPACE
                || event->key_sym == Q_KEY_DELETE)
            {
                q_widget_delete_char(focused_widget);
                changed = 1;
            } else if (event->key_sym == Q_KEY_LEFT) {
                if (focused_widget->widget_caret > 0u) {
                    focused_widget->widget_caret--;
                }
                changed = 1;
            } else if (event->key_sym == Q_KEY_RIGHT) {
                if (focused_widget->widget_caret < focused_widget->widget_value_len) {
                    focused_widget->widget_caret++;
                }
                changed = 1;
            } else if (event->key_sym == Q_KEY_HOME) {
                focused_widget->widget_caret = 0u;
                changed = 1;
            } else if (event->key_sym == Q_KEY_END) {
                focused_widget->widget_caret = focused_widget->widget_value_len;
                changed = 1;
            } else if (event->key_sym >= Q_WIDGET_TEXT_INPUT_PRINTABLE_MIN
                       && event->key_sym <= Q_WIDGET_TEXT_INPUT_PRINTABLE_MAX)
            {
                q_widget_insert_char(focused_widget, event->key_sym);
                changed = 1;
            }

            if (changed) {
                view->dirty_flags |= Q_DIRTY_PAINT;
                q_view_update(view);
            }
        }
    }

    /* Hyperlink navigation: on a left-click, walk up the box tree from the
     * hit-tested target to find the nearest ancestor <a> box with an href.
     * Named anchors are scrolled internally; all other hrefs are forwarded
     * to the host application via view->on_navigate. */
    if (event->type == Q_EVENT_MOUSE_CLICK && event->mouse_button == 0) {
        q_box_t *box;
#ifdef QUANTON_DEBUG_EVENTS
        fprintf(stderr, "[event] CLICK mouse=(%d,%d) hit_xy=(%d,%d)\n",
                event->mouse_x, event->mouse_y, hit_x, hit_y);
        q_dbg_print_box("  hit_box", event->target_box);
#endif
        for (box = event->target_box; box != NULL; box = box->parent) {
            if (box->href != NULL) {
#ifdef QUANTON_DEBUG_EVENTS
                fprintf(stderr, "  href found: \"%s\"\n", box->href);
#endif
                q_handle_anchor_click(view, box, box->href);
                break;
            }
        }
#ifdef QUANTON_DEBUG_EVENTS
        if (box == NULL) {
            fprintf(stderr, "  no href in ancestor chain\n");
        }
#endif
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
