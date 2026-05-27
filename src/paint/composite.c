#include "quanton.h"

#include <stdlib.h>
#include <string.h>

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

    q_paint_composite(view->framebuffer, view->vp_width, view->vp_height,
                      root->tile, root->tile_w, root->tile_h, 0, 0);
}
