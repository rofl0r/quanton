#include "quanton.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define Q_TEXT_COLOR 0x000000FFu

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

    q_paint_box(child);
    if (child->tile == NULL) {
        return;
    }

    dx = (int) lroundf(child->x - parent->x);
    dy = (int) lroundf(child->y - parent->y);
    q_paint_composite(parent->tile, parent->tile_w, parent->tile_h,
                      child->tile, child->tile_w, child->tile_h,
                      dx, dy);
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

    free(entries);
}
