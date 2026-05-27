#include "quanton.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define Q_TEXT_COLOR 0x000000FFu
#define Q_SCROLLBAR_THICKNESS 12
#define Q_SCROLLBAR_MIN_THUMB 16
#define Q_SCROLLBAR_TRACK_COLOR 0xA0A0A0FFu
#define Q_SCROLLBAR_THUMB_COLOR 0x707070FFu

static uint8_t q_color_r(uint32_t color) { return (uint8_t) ((color >> 24) & 0xFFu); }
static uint8_t q_color_g(uint32_t color) { return (uint8_t) ((color >> 16) & 0xFFu); }
static uint8_t q_color_b(uint32_t color) { return (uint8_t) ((color >> 8) & 0xFFu); }
static uint8_t q_color_a(uint32_t color) { return (uint8_t) (color & 0xFFu); }

static int q_paint_box_width(const q_box_t *box)
{
    int w = (int) ceilf(box->width);
    return (w > 0) ? w : 1;
}

static int q_paint_box_height(const q_box_t *box)
{
    int h = (int) ceilf(box->height);
    return (h > 0) ? h : 1;
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

static int q_box_overflow_clips(q_overflow_type_t overflow)
{
    return overflow == Q_OVERFLOW_HIDDEN
        || overflow == Q_OVERFLOW_CLIP
        || overflow == Q_OVERFLOW_SCROLL
        || overflow == Q_OVERFLOW_AUTO;
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

static int q_box_has_vertical_scrollbar(const q_box_t *box, float content_h, float viewport_h)
{
    if (box == NULL) {
        return 0;
    }

    if (box->overflow_y == Q_OVERFLOW_SCROLL) {
        return 1;
    }
    if (box->overflow_y == Q_OVERFLOW_AUTO && content_h > viewport_h) {
        return 1;
    }
    return 0;
}

static int q_box_has_horizontal_scrollbar(const q_box_t *box, float content_w, float viewport_w)
{
    if (box == NULL) {
        return 0;
    }

    if (box->overflow_x == Q_OVERFLOW_SCROLL) {
        return 1;
    }
    if (box->overflow_x == Q_OVERFLOW_AUTO && content_w > viewport_w) {
        return 1;
    }
    return 0;
}

static int q_paint_clampf_int(float value, int min_value, int max_value)
{
    if (value < (float) min_value) {
        return min_value;
    }
    if (value > (float) max_value) {
        return max_value;
    }
    return (int) lroundf(value);
}

static void q_paint_scrollbar(q_box_t *box, int vertical)
{
    float content_w;
    float content_h;
    int top;
    int right;
    int bottom;
    int left;
    int viewport_w;
    int viewport_h;
    int show_vertical;
    int show_horizontal;
    int track_x;
    int track_y;
    int track_w;
    int track_h;
    int thumb_x;
    int thumb_y;
    int thumb_w;
    int thumb_h;
    float max_scroll;
    float scroll;
    float ratio;

    if (box == NULL || box->tile == NULL) {
        return;
    }

    q_box_content_extent(box, &content_w, &content_h);
    top = (int) ceilf(box->border_width[0]);
    right = (int) ceilf(box->border_width[1]);
    bottom = (int) ceilf(box->border_width[2]);
    left = (int) ceilf(box->border_width[3]);
    viewport_w = box->tile_w - left - right;
    viewport_h = box->tile_h - top - bottom;
    if (viewport_w <= 0 || viewport_h <= 0) {
        return;
    }

    show_vertical = q_box_has_vertical_scrollbar(box, content_h, (float) viewport_h);
    show_horizontal = q_box_has_horizontal_scrollbar(box, content_w, (float) viewport_w);

    if (vertical) {
        if (!show_vertical) {
            return;
        }

        track_w = Q_SCROLLBAR_THICKNESS;
        if (track_w > viewport_w) {
            track_w = viewport_w;
        }
        track_h = viewport_h - (show_horizontal ? Q_SCROLLBAR_THICKNESS : 0);
        if (track_h <= 0) {
            return;
        }
        track_x = box->tile_w - right - track_w;
        track_y = top;

        thumb_w = track_w;
        thumb_h = q_paint_clampf_int(((float) viewport_h / (content_h > 0.0f ? content_h : 1.0f)) * track_h,
                                     Q_SCROLLBAR_MIN_THUMB, track_h);
        max_scroll = content_h - (float) viewport_h;
        if (max_scroll < 0.0f) {
            max_scroll = 0.0f;
        }
        scroll = box->scroll_y;
        if (scroll < 0.0f) {
            scroll = 0.0f;
        }
        if (scroll > max_scroll) {
            scroll = max_scroll;
        }
        ratio = (max_scroll > 0.0f) ? (scroll / max_scroll) : 0.0f;
        thumb_x = track_x;
        thumb_y = track_y + q_paint_clampf_int(ratio * (float) (track_h - thumb_h), 0, track_h - thumb_h);
    } else {
        if (!show_horizontal) {
            return;
        }

        track_h = Q_SCROLLBAR_THICKNESS;
        if (track_h > viewport_h) {
            track_h = viewport_h;
        }
        track_w = viewport_w - (show_vertical ? Q_SCROLLBAR_THICKNESS : 0);
        if (track_w <= 0) {
            return;
        }
        track_x = left;
        track_y = box->tile_h - bottom - track_h;

        thumb_h = track_h;
        thumb_w = q_paint_clampf_int(((float) viewport_w / (content_w > 0.0f ? content_w : 1.0f)) * track_w,
                                     Q_SCROLLBAR_MIN_THUMB, track_w);
        max_scroll = content_w - (float) viewport_w;
        if (max_scroll < 0.0f) {
            max_scroll = 0.0f;
        }
        scroll = box->scroll_x;
        if (scroll < 0.0f) {
            scroll = 0.0f;
        }
        if (scroll > max_scroll) {
            scroll = max_scroll;
        }
        ratio = (max_scroll > 0.0f) ? (scroll / max_scroll) : 0.0f;
        thumb_x = track_x + q_paint_clampf_int(ratio * (float) (track_w - thumb_w), 0, track_w - thumb_w);
        thumb_y = track_y;
    }

    q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                      track_x, track_y, track_w, track_h, Q_SCROLLBAR_TRACK_COLOR);
    q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                      thumb_x, thumb_y, thumb_w, thumb_h, Q_SCROLLBAR_THUMB_COLOR);
}

typedef struct q_paint_child_entry {
    q_box_t *box;
    int dom_order;
    int category;
    int z_index;
} q_paint_child_entry_t;

static int q_paint_z_category(const q_box_t *box)
{
    if (box->position != Q_POSITION_STATIC && box->has_z_index) {
        return (box->z_index < 0) ? 0 : 2;
    }

    return 1;
}

static int q_paint_child_cmp(const void *a, const void *b)
{
    const q_paint_child_entry_t *ea = (const q_paint_child_entry_t *) a;
    const q_paint_child_entry_t *eb = (const q_paint_child_entry_t *) b;

    if (ea->category != eb->category) {
        return ea->category - eb->category;
    }

    if ((ea->category == 0 || ea->category == 2) && ea->z_index != eb->z_index) {
        return (ea->z_index < eb->z_index) ? -1 : 1;
    }

    return ea->dom_order - eb->dom_order;
}

static void q_paint_box_child(q_box_t *parent, q_box_t *child)
{
    int dx;
    int dy;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    int should_clip;
    float content_w;
    float content_h;
    int show_vertical;
    int show_horizontal;

    q_paint_box(child);
    if (child->tile == NULL) {
        return;
    }

    dx = (int) lroundf(child->x - parent->x);
    dy = (int) lroundf(child->y - parent->y);
    if (q_box_scrolls_x(parent)) {
        dx -= (int) lroundf(parent->scroll_x);
    }
    if (q_box_scrolls_y(parent)) {
        dy -= (int) lroundf(parent->scroll_y);
    }

    should_clip = q_box_overflow_clips(parent->overflow_x) || q_box_overflow_clips(parent->overflow_y);

    if (should_clip) {
        /* Clip to the parent's content area (inside its borders). */
        int bleft  = (int) ceilf(parent->border_width[3]);
        int btop   = (int) ceilf(parent->border_width[0]);
        int bright = (int) ceilf(parent->border_width[1]);
        int bbottom = (int) ceilf(parent->border_width[2]);

        clip_x = bleft;
        clip_y = btop;
        clip_w = parent->tile_w - bleft - bright;
        clip_h = parent->tile_h - btop  - bbottom;

        q_box_content_extent(parent, &content_w, &content_h);
        show_vertical = q_box_has_vertical_scrollbar(parent, content_h, (float) clip_h);
        show_horizontal = q_box_has_horizontal_scrollbar(parent, content_w, (float) clip_w);
        if (show_vertical) {
            clip_w -= Q_SCROLLBAR_THICKNESS;
        }
        if (show_horizontal) {
            clip_h -= Q_SCROLLBAR_THICKNESS;
        }

        /* If one axis allows overflow (VISIBLE), expand the clip region to
         * cover the full tile on that axis so only the other axis is clipped. */
        if (parent->overflow_x == Q_OVERFLOW_VISIBLE) {
            clip_x = 0;
            clip_w = parent->tile_w;
        }
        if (parent->overflow_y == Q_OVERFLOW_VISIBLE) {
            clip_y = 0;
            clip_h = parent->tile_h;
        }

        q_paint_composite_clipped(parent->tile, parent->tile_w, parent->tile_h,
                                  child->tile, child->tile_w, child->tile_h,
                                  dx, dy,
                                  clip_x, clip_y, clip_w, clip_h);
    } else {
        q_paint_composite(parent->tile, parent->tile_w, parent->tile_h,
                          child->tile, child->tile_w, child->tile_h,
                          dx, dy);
    }
}

static void q_paint_image(q_box_t *box)
{
    const uint8_t *src;
    int src_w;
    int src_h;
    int dst_w;
    int dst_h;
    int y;
    int x;

    if (box == NULL || box->tile == NULL || box->image == NULL) {
        return;
    }

    src = q_image_pixels(box->image);
    src_w = q_image_width(box->image);
    src_h = q_image_height(box->image);
    dst_w = box->tile_w;
    dst_h = box->tile_h;

    if (src == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    for (y = 0; y < dst_h; ++y) {
        int sy = (y * src_h) / dst_h;
        if (sy >= src_h) {
            sy = src_h - 1;
        }

        for (x = 0; x < dst_w; ++x) {
            int sx = (x * src_w) / dst_w;
            size_t sidx;
            size_t didx;

            if (sx >= src_w) {
                sx = src_w - 1;
            }

            sidx = (size_t) (sy * src_w + sx) * 4u;
            didx = (size_t) (y * dst_w + x) * 4u;
            box->tile[didx + 0] = src[sidx + 0];
            box->tile[didx + 1] = src[sidx + 1];
            box->tile[didx + 2] = src[sidx + 2];
            box->tile[didx + 3] = src[sidx + 3];
        }
    }
}

void q_paint_fill_rect(uint8_t *pixels, int buf_w, int buf_h,
                       int x, int y, int w, int h, uint32_t color)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int yy;
    int xx;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;

    if (pixels == NULL || buf_w <= 0 || buf_h <= 0 || w <= 0 || h <= 0) {
        return;
    }

    x0 = (x < 0) ? 0 : x;
    y0 = (y < 0) ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > buf_w) {
        x1 = buf_w;
    }
    if (y1 > buf_h) {
        y1 = buf_h;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    r = q_color_r(color);
    g = q_color_g(color);
    b = q_color_b(color);
    a = q_color_a(color);

    for (yy = y0; yy < y1; ++yy) {
        for (xx = x0; xx < x1; ++xx) {
            size_t idx = (size_t) (yy * buf_w + xx) * 4u;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = a;
        }
    }
}

void q_paint_borders(q_box_t *box)
{
    int w;
    int h;
    int top;
    int right;
    int bottom;
    int left;

    if (box == NULL || box->tile == NULL) {
        return;
    }

    w = box->tile_w;
    h = box->tile_h;
    top = (int) ceilf(box->border_width[0]);
    right = (int) ceilf(box->border_width[1]);
    bottom = (int) ceilf(box->border_width[2]);
    left = (int) ceilf(box->border_width[3]);

    if (top > 0) {
        q_paint_fill_rect(box->tile, w, h, 0, 0, w, top, box->border_color[0]);
    }
    if (right > 0) {
        q_paint_fill_rect(box->tile, w, h, w - right, 0, right, h, box->border_color[1]);
    }
    if (bottom > 0) {
        q_paint_fill_rect(box->tile, w, h, 0, h - bottom, w, bottom, box->border_color[2]);
    }
    if (left > 0) {
        q_paint_fill_rect(box->tile, w, h, 0, 0, left, h, box->border_color[3]);
    }
}

void q_paint_composite(uint8_t *dst, int dst_w, int dst_h,
                       const uint8_t *src, int src_w, int src_h,
                       int dx, int dy)
{
    int sy;
    int sx;

    if (dst == NULL || src == NULL || dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0) {
        return;
    }

    for (sy = 0; sy < src_h; ++sy) {
        int dy_pos = dy + sy;
        if (dy_pos < 0 || dy_pos >= dst_h) {
            continue;
        }

        for (sx = 0; sx < src_w; ++sx) {
            int dx_pos = dx + sx;
            size_t sidx;
            size_t didx;
            unsigned int sa;
            unsigned int inv_sa;

            if (dx_pos < 0 || dx_pos >= dst_w) {
                continue;
            }

            sidx = (size_t) (sy * src_w + sx) * 4u;
            didx = (size_t) (dy_pos * dst_w + dx_pos) * 4u;

            sa = src[sidx + 3];
            if (sa == 0u) {
                continue;
            }

            inv_sa = 255u - sa;
            dst[didx + 0] = (uint8_t) ((src[sidx + 0] * sa + dst[didx + 0] * inv_sa) / 255u);
            dst[didx + 1] = (uint8_t) ((src[sidx + 1] * sa + dst[didx + 1] * inv_sa) / 255u);
            dst[didx + 2] = (uint8_t) ((src[sidx + 2] * sa + dst[didx + 2] * inv_sa) / 255u);
            dst[didx + 3] = (uint8_t) (sa + (dst[didx + 3] * inv_sa) / 255u);
        }
    }
}

void q_paint_composite_clipped(uint8_t *dst, int dst_w, int dst_h,
                                const uint8_t *src, int src_w, int src_h,
                                int dx, int dy,
                                int clip_x, int clip_y, int clip_w, int clip_h)
{
    int sy;
    int sx;
    int cx1;
    int cy1;

    if (dst == NULL || src == NULL || dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0) {
        return;
    }
    if (clip_w <= 0 || clip_h <= 0) {
        return;
    }

    cx1 = clip_x + clip_w;
    cy1 = clip_y + clip_h;

    for (sy = 0; sy < src_h; ++sy) {
        int dy_pos = dy + sy;
        if (dy_pos < 0 || dy_pos >= dst_h) {
            continue;
        }
        if (dy_pos < clip_y || dy_pos >= cy1) {
            continue;
        }

        for (sx = 0; sx < src_w; ++sx) {
            int dx_pos = dx + sx;
            size_t sidx;
            size_t didx;
            unsigned int sa;
            unsigned int inv_sa;

            if (dx_pos < 0 || dx_pos >= dst_w) {
                continue;
            }
            if (dx_pos < clip_x || dx_pos >= cx1) {
                continue;
            }

            sidx = (size_t) (sy * src_w + sx) * 4u;
            didx = (size_t) (dy_pos * dst_w + dx_pos) * 4u;

            sa = src[sidx + 3];
            if (sa == 0u) {
                continue;
            }

            inv_sa = 255u - sa;
            dst[didx + 0] = (uint8_t) ((src[sidx + 0] * sa + dst[didx + 0] * inv_sa) / 255u);
            dst[didx + 1] = (uint8_t) ((src[sidx + 1] * sa + dst[didx + 1] * inv_sa) / 255u);
            dst[didx + 2] = (uint8_t) ((src[sidx + 2] * sa + dst[didx + 2] * inv_sa) / 255u);
            dst[didx + 3] = (uint8_t) (sa + (dst[didx + 3] * inv_sa) / 255u);
        }
    }
}

void q_paint_box(q_box_t *box)
{
    q_box_t *child;
    q_paint_child_entry_t *entries = NULL;
    size_t child_count = 0;
    size_t i = 0;
    int w;
    int h;

    if (box == NULL) {
        return;
    }

    w = q_paint_box_width(box);
    h = q_paint_box_height(box);

    if (box->tile_w != w || box->tile_h != h) {
        free(box->tile);
        box->tile = NULL;
    }

    if (box->tile == NULL) {
        box->tile = (uint8_t *) calloc((size_t) w * (size_t) h * 4u, 1);
    } else {
        memset(box->tile, 0, (size_t) w * (size_t) h * 4u);
    }

    if (box->tile == NULL) {
        box->tile_w = 0;
        box->tile_h = 0;
        return;
    }

    box->tile_w = w;
    box->tile_h = h;

    if (box->type == Q_BOX_BLOCK) {
        q_paint_fill_rect(box->tile, w, h, 0, 0, w, h, box->background_color);
        q_paint_borders(box);
    } else if (box->type == Q_BOX_IMAGE) {
        q_paint_image(box);
    } else if (box->type == Q_BOX_TEXT && box->run != NULL) {
        q_font_render_run(box->run, Q_TEXT_COLOR, box->tile, w, h, 0, 0);
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        ++child_count;
    }

    if (child_count != 0) {
        int order = 0;
        entries = (q_paint_child_entry_t *) malloc(child_count * sizeof(*entries));
        if (entries != NULL) {
            for (child = box->first_child; child != NULL; child = child->next_sibling) {
                entries[i].box = child;
                entries[i].dom_order = order++;
                entries[i].category = q_paint_z_category(child);
                entries[i].z_index = child->z_index;
                ++i;
            }

            qsort(entries, child_count, sizeof(*entries), q_paint_child_cmp);
        }
    }

    if (entries != NULL) {
        for (i = 0; i < child_count; ++i) {
            q_paint_box_child(box, entries[i].box);
        }
    } else {
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            q_paint_box_child(box, child);
        }
    }

    q_paint_scrollbar(box, 1);
    q_paint_scrollbar(box, 0);

    free(entries);
}
