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
    } else if (box->type == Q_BOX_TEXT && box->run != NULL) {
        q_font_render_run(box->run, Q_TEXT_COLOR, box->tile, w, h, 0, 0);
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        int dx;
        int dy;

        q_paint_box(child);
        if (child->tile == NULL) {
            continue;
        }

        dx = (int) lroundf(child->x - box->x);
        dy = (int) lroundf(child->y - box->y);
        q_paint_composite(box->tile, box->tile_w, box->tile_h,
                          child->tile, child->tile_w, child->tile_h,
                          dx, dy);
    }
}
