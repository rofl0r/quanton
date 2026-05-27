#include "quanton.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void q_view_update_doc_size(quanton_view_t *view)
{
    q_box_t *root;

    if (view == NULL) {
        return;
    }

    root = view->layout_root;
    if (root == NULL) {
        view->doc_width = 0.0f;
        view->doc_height = 0.0f;
        return;
    }

    view->doc_width = ceilf(root->width);
    view->doc_height = ceilf(root->height);
    if (root->tile_w > 0 && view->doc_width < (float) root->tile_w) {
        view->doc_width = (float) root->tile_w;
    }
    if (root->tile_h > 0 && view->doc_height < (float) root->tile_h) {
        view->doc_height = (float) root->tile_h;
    }
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

static void q_view_clamp_scroll(quanton_view_t *view)
{
    float max_x;
    float max_y;

    if (view == NULL) {
        return;
    }

    q_view_update_doc_size(view);
    max_x = view->doc_width - (float) view->vp_width;
    max_y = view->doc_height - (float) view->vp_height;
    if (max_x < 0.0f) {
        max_x = 0.0f;
    }
    if (max_y < 0.0f) {
        max_y = 0.0f;
    }

    view->scroll_x = q_clamp_scroll(view->scroll_x, max_x);
    view->scroll_y = q_clamp_scroll(view->scroll_y, max_y);
}

/*
 * q_composite_frame — flatten the painted box tree into view->framebuffer.
 *
 * q_paint_box() already composites child tiles onto each parent tile
 * recursively, so after it returns the root tile contains the fully
 * rendered image.  We just need to copy that tile into the viewport
 * framebuffer (clearing to opaque white first so any uncovered pixels
 * are white rather than garbage).
 */
void q_composite_frame(quanton_view_t *view)
{
    q_box_t *root;
    size_t   fb_size;

    if (view == NULL) {
        return;
    }
    if (view->vp_width <= 0 || view->vp_height <= 0) {
        return;
    }

    fb_size = (size_t) view->vp_width * (size_t) view->vp_height * 4u;

    if (view->framebuffer == NULL) {
        view->framebuffer = (uint8_t *) malloc(fb_size);
        if (view->framebuffer == NULL) {
            return;
        }
    }

    /* fill with opaque white */
    memset(view->framebuffer, 0xFF, fb_size);

    root = view->layout_root;
    if (root == NULL || root->tile == NULL) {
        return;
    }

    q_view_clamp_scroll(view);

    q_paint_composite(view->framebuffer, view->vp_width, view->vp_height,
                      root->tile, root->tile_w, root->tile_h,
                      -(int) lroundf(view->scroll_x),
                      -(int) lroundf(view->scroll_y));
}

void q_view_scroll_to(quanton_view_t *view, float x, float y)
{
    float old_x;
    float old_y;

    if (view == NULL) {
        return;
    }

    q_view_update_doc_size(view);

    old_x = view->scroll_x;
    old_y = view->scroll_y;
    view->scroll_x = x;
    view->scroll_y = y;
    q_view_clamp_scroll(view);

    if (view->scroll_x == old_x && view->scroll_y == old_y) {
        return;
    }

    view->dirty_flags |= Q_DIRTY_SCROLL;
    q_view_update(view);
}

void q_view_scroll_by(quanton_view_t *view, float dx, float dy)
{
    if (view == NULL) {
        return;
    }

    q_view_scroll_to(view, view->scroll_x + dx, view->scroll_y + dy);
}

void q_view_scroll_into_view(quanton_view_t *view, const q_box_t *box)
{
    float target_x;
    float target_y;
    float box_right;
    float box_bottom;

    if (view == NULL || box == NULL) {
        return;
    }

    target_x = view->scroll_x;
    target_y = view->scroll_y;
    box_right = box->x + box->width;
    box_bottom = box->y + box->height;

    if (box->x < target_x) {
        target_x = box->x;
    } else if (box_right > target_x + (float) view->vp_width) {
        target_x = box_right - (float) view->vp_width;
    }

    if (box->y < target_y) {
        target_y = box->y;
    } else if (box_bottom > target_y + (float) view->vp_height) {
        target_y = box_bottom - (float) view->vp_height;
    }

    q_view_scroll_to(view, target_x, target_y);
}
