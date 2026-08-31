#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/character_data.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/tag/const.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q_TEXT_COLOR 0x000000FFu
#define Q_MARKER_GUTTER_X 4
#define Q_SCROLLBAR_THICKNESS 14
#define Q_SCROLLBAR_MIN_THUMB 16
#define Q_SCROLLBAR_TRACK_COLOR 0xA0A0A0FFu
#define Q_SCROLLBAR_THUMB_COLOR 0x707070FFu

static uint8_t q_color_r(uint32_t color) { return (uint8_t) ((color >> 24) & 0xFFu); }
static uint8_t q_color_g(uint32_t color) { return (uint8_t) ((color >> 16) & 0xFFu); }
static uint8_t q_color_b(uint32_t color) { return (uint8_t) ((color >> 8) & 0xFFu); }
static uint8_t q_color_a(uint32_t color) { return (uint8_t) (color & 0xFFu); }

static uint32_t q_paint_resolve_text_color(const q_box_t *box)
{
    const q_box_t *cur = box;

    while (cur != NULL) {
        if (cur->has_text_color) {
            return cur->text_color;
        }
        cur = cur->parent;
    }

    return Q_TEXT_COLOR;
}

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

static q_table_t *q_paint_find_cell_table(q_box_t *cell)
{
    q_box_t *cur;

    if (cell == NULL || cell->type != Q_BOX_TABLE_CELL) {
        return NULL;
    }

    for (cur = cell->parent; cur != NULL; cur = cur->parent) {
        if (cur->type == Q_BOX_TABLE && cur->table != NULL) {
            return cur->table;
        }
    }

    return NULL;
}

static q_table_span_t *q_paint_find_cell_span(q_table_t *table, q_box_t *cell)
{
    int i;

    if (table == NULL || cell == NULL) {
        return NULL;
    }

    for (i = 0; i < table->span_count; i++) {
        if (table->spans[i].cell_box == cell) {
            return &table->spans[i];
        }
    }

    return NULL;
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

static void q_paint_box_child_cached(q_box_t *parent, q_box_t *child)
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

    if (child == NULL) {
        return;
    }

    if (child->tile == NULL || q_box_scrolls_x(child) || q_box_scrolls_y(child)) {
        q_paint_box(child);
    }
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

static void q_paint_background_image(q_box_t *box)
{
    const uint8_t *src;
    int src_w;
    int src_h;
    int tile_x;
    int tile_y;

    if (box == NULL || box->tile == NULL || box->background_image == NULL) {
        return;
    }

    src = q_image_pixels(box->background_image);
    src_w = q_image_width(box->background_image);
    src_h = q_image_height(box->background_image);
    if (src == NULL || src_w <= 0 || src_h <= 0) {
        return;
    }

    if (box->background_repeat == Q_BACKGROUND_REPEAT_NO_REPEAT) {
        q_paint_composite(box->tile, box->tile_w, box->tile_h, src, src_w, src_h, 0, 0);
        return;
    }
    if (box->background_repeat == Q_BACKGROUND_REPEAT_REPEAT_X) {
        for (tile_x = 0; tile_x < box->tile_w; tile_x += src_w) {
            q_paint_composite(box->tile, box->tile_w, box->tile_h, src, src_w, src_h, tile_x, 0);
        }
        return;
    }
    if (box->background_repeat == Q_BACKGROUND_REPEAT_REPEAT_Y) {
        for (tile_y = 0; tile_y < box->tile_h; tile_y += src_h) {
            q_paint_composite(box->tile, box->tile_w, box->tile_h, src, src_w, src_h, 0, tile_y);
        }
        return;
    }

    for (tile_y = 0; tile_y < box->tile_h; tile_y += src_h) {
        for (tile_x = 0; tile_x < box->tile_w; tile_x += src_w) {
            q_paint_composite(box->tile, box->tile_w, box->tile_h, src, src_w, src_h, tile_x, tile_y);
        }
    }
}

static void q_paint_text_decoration(q_box_t *box, uint32_t color)
{
    int baseline;
    int underline_y;
    int overline_y;
    int strike_y;

    if (box == NULL || box->tile == NULL || box->text_decoration == 0u) {
        return;
    }

    baseline = (box->run != NULL) ? (int) lroundf(box->run->ascender) : (int) lroundf(box->height * 0.8f);
    underline_y = baseline + 1;
    overline_y = 0;
    strike_y = (box->run != NULL) ? (int) lroundf(baseline - (box->run->ascender * 0.5f))
                                  : (int) lroundf(box->height * 0.5f);

    if (box->text_decoration & Q_TEXT_DECORATION_UNDERLINE) {
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, 0, underline_y, box->tile_w, 1, color);
    }
    if (box->text_decoration & Q_TEXT_DECORATION_OVERLINE) {
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, 0, overline_y, box->tile_w, 1, color);
    }
    if (box->text_decoration & Q_TEXT_DECORATION_LINE_THROUGH) {
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, 0, strike_y, box->tile_w, 1, color);
    }
}

static void q_paint_list_marker(q_box_t *box)
{
    static q_font_cache_t *cache;
    q_font_t *font;
    int marker_y;
    uint32_t text_color = q_paint_resolve_text_color(box);

    if (box == NULL
        || box->tile == NULL
        || box->list_style_type == Q_LIST_STYLE_NONE
        || box->list_item_index <= 0)
    {
        return;
    }

    marker_y = box->tile_h / 2;
    if (box->list_style_type == Q_LIST_STYLE_DISC) {
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                          Q_MARKER_GUTTER_X, marker_y - 2, 5, 5, text_color);
        return;
    }

    if (box->list_style_type != Q_LIST_STYLE_DECIMAL) {
        return;
    }

    if (cache == NULL) {
        cache = q_font_cache_create();
    }
    if (cache == NULL) {
        return;
    }

    font = q_font_match(cache, "sans-serif", 16.0f, 400, Q_FONT_STYLE_NORMAL);
    if (font != NULL) {
        char marker[24];
        q_shaped_run_t *run;
        int marker_x = Q_MARKER_GUTTER_X;
        int marker_run_y;
        int n = snprintf(marker, sizeof(marker), "%d.", box->list_item_index);
        if (n <= 0) {
            return;
        }
        run = q_font_shape_run(font, marker, (size_t) n);
        if (run == NULL) {
            return;
        }
        run->font = font;
        marker_run_y = 0;
        q_font_render_run(run, text_color, box->tile, box->tile_w, box->tile_h,
                          marker_x, marker_run_y);
        q_shaped_run_free(run);
    }
}

static lxb_tag_id_t q_paint_box_tag_id(const q_box_t *box)
{
    if (box == NULL || box->dom_node == NULL
        || lxb_dom_node_type(box->dom_node) != LXB_DOM_NODE_TYPE_ELEMENT)
    {
        return LXB_TAG__UNDEF;
    }
    return lxb_dom_node_tag_id(box->dom_node);
}

static const lxb_char_t *q_paint_get_attr(const q_box_t *box,
                                          const char *name,
                                          size_t *out_len)
{
    lxb_dom_element_t *el;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (box == NULL || box->dom_node == NULL
        || lxb_dom_node_type(box->dom_node) != LXB_DOM_NODE_TYPE_ELEMENT)
    {
        return NULL;
    }

    if (strcmp(name, "value") == 0
        && (box->widget_type == Q_WIDGET_INPUT_TEXT
            || box->widget_type == Q_WIDGET_INPUT_SUBMIT
            || box->widget_type == Q_WIDGET_BUTTON
            || box->widget_type == Q_WIDGET_SELECT
            || box->widget_type == Q_WIDGET_TEXTAREA))
    {
        if (out_len != NULL) {
            *out_len = box->widget_value_len;
        }
        if (box->widget_value != NULL) {
            return (const lxb_char_t *) box->widget_value;
        }
        return (const lxb_char_t *) "";
    }

    if (strcmp(name, "checked") == 0
        && (box->widget_type == Q_WIDGET_INPUT_CHECK
            || box->widget_type == Q_WIDGET_INPUT_RADIO))
    {
        if (box->widget_checked) {
            if (out_len != NULL) {
                *out_len = sizeof("checked") - 1u;
            }
            return (const lxb_char_t *) "checked";
        }
        return NULL;
    }

    el = lxb_dom_interface_element(box->dom_node);
    return lxb_dom_element_get_attribute(el, (const lxb_char_t *) name, strlen(name), out_len);
}

static int q_paint_attr_is(const q_box_t *box, const char *name, const char *value)
{
    const lxb_char_t *attr;
    size_t attr_len = 0;
    size_t value_len = strlen(value);

    attr = q_paint_get_attr(box, name, &attr_len);
    if (attr == NULL || attr_len != value_len) {
        return 0;
    }
    return memcmp(attr, value, value_len) == 0;
}

static q_font_t *q_paint_widget_font(const q_box_t *box)
{
    static q_font_cache_t *cache;

    if (box == NULL) {
        return NULL;
    }
    if (cache == NULL) {
        cache = q_font_cache_create();
    }
    if (cache == NULL) {
        return NULL;
    }

    return q_font_match(cache, "sans-serif",
                        (!isnan(box->font_size) && box->font_size > 0.0f) ? box->font_size : 16.0f,
                        (box->font_weight > 0) ? box->font_weight : 400,
                        (int) box->font_style);
}

static void q_paint_render_widget_text(q_box_t *box, const char *text, size_t text_len,
                                       int x, int y, uint32_t color)
{
    q_font_t *font;
    q_shaped_run_t *run;

    if (box == NULL || box->tile == NULL || text == NULL || text_len == 0u) {
        return;
    }

    font = q_paint_widget_font(box);
    if (font == NULL) {
        return;
    }
    run = q_font_shape_run(font, text, text_len);
    if (run == NULL) {
        return;
    }
    run->font = font;
    q_font_render_run(run, color, box->tile, box->tile_w, box->tile_h, x, y);
    q_shaped_run_free(run);
}

static size_t q_paint_textarea_line_count(const q_box_t *box)
{
    size_t i;
    size_t lines = 1u;
    if (box == NULL || box->widget_value == NULL || box->widget_value_len == 0u) {
        return 1u;
    }
    for (i = 0u; i < box->widget_value_len; ++i) {
        if (box->widget_value[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

static void q_paint_render_textarea(q_box_t *box, uint32_t text_color)
{
    size_t i;
    size_t line_start = 0u;
    size_t line_index = 0u;
    size_t caret_line = 0u;
    size_t caret_col = 0u;
    float font_px;
    int line_h;
    int base_y = 4;
    float scroll_y = 0.0f;
    int show_scrollbar = 0;
    float total_h = 0.0f;
    q_font_t *font;
    int caret_x = 4;
    int caret_y = base_y;

    if (box == NULL) {
        return;
    }

    if (box->widget_caret > box->widget_value_len) {
        box->widget_caret = box->widget_value_len;
    }
    if (box->widget_scroll_y > 0.0f) {
        scroll_y = box->widget_scroll_y;
    }
    total_h = ((float) q_paint_textarea_line_count(box)) * 18.0f;
    if (total_h > (float) (box->tile_h - 8)) {
        show_scrollbar = 1;
    }

    for (i = 0u; i < box->widget_caret; ++i) {
        if (box->widget_value != NULL && box->widget_value[i] == '\n') {
            caret_line++;
            caret_col = 0u;
        } else {
            caret_col++;
        }
    }

    if (box->widget_value != NULL && box->widget_value_len > 0u) {
        for (i = 0u; i <= box->widget_value_len; ++i) {
            if (i == box->widget_value_len || box->widget_value[i] == '\n') {
                size_t seg_len = i - line_start;
                if (seg_len > 0u) {
                    q_paint_render_widget_text(box, box->widget_value + line_start, seg_len,
                                               4, base_y + (int) line_index * 18
                                               - (int) lroundf(scroll_y), text_color);
                }
                line_start = i + 1u;
                line_index++;
            }
        }
    }

    if (box->widget_focused) {
        font = q_paint_widget_font(box);
        if (font != NULL && box->widget_value != NULL && caret_col > 0u) {
            size_t line_off = 0u;
            size_t cur_line = 0u;
            for (i = 0u; i < box->widget_caret; ++i) {
                if (box->widget_value[i] == '\n') {
                    cur_line++;
                    line_off = i + 1u;
                }
            }
            if (cur_line == caret_line && box->widget_caret >= line_off) {
                caret_x += (int) lroundf(q_font_measure(font,
                                                        box->widget_value + line_off,
                                                        box->widget_caret - line_off));
            }
        }
        caret_y = base_y + (int) caret_line * 18 - (int) lroundf(scroll_y);
        font_px = (!isnan(box->font_size) && box->font_size > 0.0f) ? box->font_size : 16.0f;
        line_h = (int) lroundf(font_px);
        if (line_h < 8) {
            line_h = 8;
        }
        if (caret_y + line_h > box->tile_h - 2) {
            line_h = box->tile_h - caret_y - 2;
        }
        if (line_h > 0) {
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                              caret_x, caret_y, 1, line_h, text_color);
        }
    }
    if (show_scrollbar) {
        int track_w = Q_SCROLLBAR_VISUAL_THICKNESS;
        int track_h = box->tile_h - 2;
        int track_x = box->tile_w - track_w - 1;
        int track_y = 1;
        float inner_h = (float) (box->tile_h - 8);
        float max_scroll = total_h - inner_h;
        int thumb_h;
        int thumb_y;
        if (max_scroll < 1.0f) {
            max_scroll = 1.0f;
        }
        thumb_h = (int) lroundf(inner_h * (inner_h / total_h));
        if (thumb_h < 12) {
            thumb_h = 12;
        }
        if (thumb_h > track_h) {
            thumb_h = track_h;
        }
        thumb_y = track_y + (int) lroundf((scroll_y / max_scroll) * (float) (track_h - thumb_h));
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                          track_x, track_y, track_w, track_h, 0xE5E7EBFFu);
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                          track_x, thumb_y, track_w, thumb_h, 0x9CA3AFFFu);
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                          track_x, track_y, 1, track_h, 0x707070FFu);
    }
}

static void q_paint_form_widget(q_box_t *box)
{
    lxb_tag_id_t tag;
    uint32_t text_color;

    if (box == NULL || box->tile == NULL) {
        return;
    }
    tag = q_paint_box_tag_id(box);
    if (tag != LXB_TAG_INPUT && tag != LXB_TAG_BUTTON
        && tag != LXB_TAG_SELECT && tag != LXB_TAG_TEXTAREA)
    {
        return;
    }

    text_color = q_paint_resolve_text_color(box);

    if (tag == LXB_TAG_INPUT) {
        if (q_paint_attr_is(box, "type", "checkbox")) {
            if (q_paint_get_attr(box, "checked", NULL) != NULL) {
                int mark_w = 6;
                int mark_h = 6;
                int cx = box->tile_w / 2 - mark_w / 2;
                int cy = box->tile_h / 2 - mark_h / 2;
                q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                                  cx, cy, mark_w, mark_h, text_color);
            }
            return;
        }
        if (q_paint_attr_is(box, "type", "radio")) {
            if (q_paint_get_attr(box, "checked", NULL) != NULL) {
                int cx = box->tile_w / 2 - 2;
                int cy = box->tile_h / 2 - 2;
                q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, cx, cy, 4, 4, text_color);
            }
            return;
        }
        if (q_paint_attr_is(box, "type", "submit")
            || q_paint_attr_is(box, "type", "button")
            || q_paint_attr_is(box, "type", "reset"))
        {
            const lxb_char_t *value;
            size_t value_len = 0;
            const char *fallback = "Submit";
            value = q_paint_get_attr(box, "value", &value_len);
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                              1, 1, box->tile_w - 2, box->tile_h / 2, 0xF4F4F4FFu);
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                              1, box->tile_h / 2, box->tile_w - 2, box->tile_h - box->tile_h / 2 - 1,
                              0xD8D8D8FFu);
            if (value != NULL && value_len > 0u) {
                q_paint_render_widget_text(box, (const char *) value, value_len, 6, 4, text_color);
            } else {
                q_paint_render_widget_text(box, fallback, strlen(fallback), 6, 4, text_color);
            }
            return;
        }

        {
            const lxb_char_t *value;
            size_t value_len = 0;
            int text_x = 4 - (int) lroundf((box->widget_scroll_x > 0.0f) ? box->widget_scroll_x : 0.0f);
            value = q_paint_get_attr(box, "value", &value_len);
            if (value != NULL && value_len > 0u) {
                q_paint_render_widget_text(box, (const char *) value, value_len, text_x, 4, text_color);
            }
            if (box->widget_focused && box->widget_type == Q_WIDGET_INPUT_TEXT
                && box->widget_caret <= box->widget_value_len)
            {
                int caret_x = text_x;
                if (box->widget_caret > 0u && box->widget_value != NULL) {
                    q_font_t *font = q_paint_widget_font(box);
                    if (font != NULL) {
                        caret_x += (int) lroundf(q_font_measure(font, box->widget_value,
                                                                box->widget_caret));
                    }
                }
                q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                                  caret_x, 2, 1, box->tile_h - 4, text_color);
            }
        }
        return;
    }

    if (tag == LXB_TAG_BUTTON) {
        uint32_t top_color = 0xF4F4F4FFu;
        uint32_t bottom_color = 0xD8D8D8FFu;
        if (box->widget_pressed) {
            top_color = 0xD8D8D8FFu;
            bottom_color = 0xF4F4F4FFu;
        }
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                          1, 1, box->tile_w - 2, box->tile_h / 2, top_color);
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h,
                          1, box->tile_h / 2, box->tile_w - 2, box->tile_h - box->tile_h / 2 - 1,
                          bottom_color);
        /*
         * Unlike <input type=button>, a <button> element's label comes from
         * its DOM child nodes (rendered separately by the normal box paint
         * recursion), not its "value" attribute. Rendering "value" here as
         * well would overlay a second, unrelated text string on the button.
         */
        return;
    }

    if (tag == LXB_TAG_SELECT) {
        int mid_y = box->tile_h / 2;
        int x0 = box->tile_w - 11;
        const lxb_char_t *value;
        size_t value_len = 0;
        uint32_t frame_color = box->widget_open ? 0x3B82F6FFu : 0x707070FFu;
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, 0, 0, box->tile_w, 1, frame_color);
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, 0, box->tile_h - 1, box->tile_w, 1, frame_color);
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, 0, 0, 1, box->tile_h, frame_color);
        q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, box->tile_w - 1, 0, 1, box->tile_h, frame_color);
        if (box->widget_value != NULL && box->widget_value_len > 0u) {
            value = (const lxb_char_t *) box->widget_value;
            value_len = box->widget_value_len;
        } else {
            value = q_paint_get_attr(box, "value", &value_len);
        }
        if (value != NULL && value_len > 0u) {
            q_paint_render_widget_text(box, (const char *) value, value_len, 4, 4, text_color);
        }
        if (box->widget_open) {
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, x0 + 2, mid_y - 1, 2, 1, text_color);
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, x0 + 1, mid_y, 4, 1, text_color);
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, x0, mid_y + 1, 6, 1, text_color);
        } else {
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, x0, mid_y - 1, 6, 1, text_color);
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, x0 + 1, mid_y, 4, 1, text_color);
            q_paint_fill_rect(box->tile, box->tile_w, box->tile_h, x0 + 2, mid_y + 1, 2, 1, text_color);
        }
        return;
    }

    if (tag == LXB_TAG_TEXTAREA) {
        q_paint_render_textarea(box, text_color);
        return;
    }
}

static int q_paint_box_has_radius(const q_box_t *box)
{
    return box != NULL
        && (box->border_radius[0] > 0.0f
            || box->border_radius[1] > 0.0f
            || box->border_radius[2] > 0.0f
            || box->border_radius[3] > 0.0f);
}

static void q_paint_radius_corner_clip(q_box_t *box, int corner, float radius)
{
    int x_start;
    int y_start;
    int x_end;
    int y_end;
    float cx;
    float cy;
    int x;
    int y;

    if (box == NULL || box->tile == NULL || radius <= 0.0f) {
        return;
    }

    if (corner == 0) {
        x_start = 0;
        y_start = 0;
        x_end = (int) ceilf(radius);
        y_end = (int) ceilf(radius);
        cx = radius;
        cy = radius;
    } else if (corner == 1) {
        x_start = box->tile_w - (int) ceilf(radius);
        y_start = 0;
        x_end = box->tile_w;
        y_end = (int) ceilf(radius);
        cx = (float) box->tile_w - radius;
        cy = radius;
    } else if (corner == 2) {
        x_start = box->tile_w - (int) ceilf(radius);
        y_start = box->tile_h - (int) ceilf(radius);
        x_end = box->tile_w;
        y_end = box->tile_h;
        cx = (float) box->tile_w - radius;
        cy = (float) box->tile_h - radius;
    } else {
        x_start = 0;
        y_start = box->tile_h - (int) ceilf(radius);
        x_end = (int) ceilf(radius);
        y_end = box->tile_h;
        cx = radius;
        cy = (float) box->tile_h - radius;
    }

    if (x_start < 0) {
        x_start = 0;
    }
    if (y_start < 0) {
        y_start = 0;
    }
    if (x_end > box->tile_w) {
        x_end = box->tile_w;
    }
    if (y_end > box->tile_h) {
        y_end = box->tile_h;
    }

    for (y = y_start; y < y_end; ++y) {
        for (x = x_start; x < x_end; ++x) {
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            float dx = px - cx;
            float dy = py - cy;
            if (dx * dx + dy * dy > radius * radius) {
                size_t idx = (size_t) (y * box->tile_w + x) * 4u;
                box->tile[idx + 0] = 0u;
                box->tile[idx + 1] = 0u;
                box->tile[idx + 2] = 0u;
                box->tile[idx + 3] = 0u;
            }
        }
    }
}

static void q_paint_apply_border_radius(q_box_t *box)
{
    if (!q_paint_box_has_radius(box)) {
        return;
    }

    q_paint_radius_corner_clip(box, 0, box->border_radius[0]);
    q_paint_radius_corner_clip(box, 1, box->border_radius[1]);
    q_paint_radius_corner_clip(box, 2, box->border_radius[2]);
    q_paint_radius_corner_clip(box, 3, box->border_radius[3]);
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

    if (box->type == Q_BOX_TABLE_CELL) {
        q_table_t *table = q_paint_find_cell_table(box);
        if (table != NULL && table->border_collapse) {
            q_table_span_t *span = q_paint_find_cell_span(table, box);
            if (span != NULL) {
                if (span->col + span->colspan < table->col_count) {
                    right = 0;
                }
                if (span->row + span->rowspan < table->row_count) {
                    bottom = 0;
                }
            }
        }
    }

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

static void q_paint_box_internal(q_box_t *box, int repaint_children)
{
    q_box_t *child;
    q_paint_child_entry_t *entries = NULL;
    size_t child_count = 0;
    size_t i = 0;
    int w;
    int h;
    uint32_t text_color;

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
    text_color = q_paint_resolve_text_color(box);

    if (box->type == Q_BOX_BLOCK
            || box->type == Q_BOX_TABLE
            || box->type == Q_BOX_TABLE_CELL
            || box->type == Q_BOX_TABLE_CAPTION) {
        q_paint_fill_rect(box->tile, w, h, 0, 0, w, h, box->background_color);
        q_paint_background_image(box);
        q_paint_borders(box);
        q_paint_list_marker(box);
        q_paint_form_widget(box);
    } else if (box->type == Q_BOX_IMAGE) {
        q_paint_image(box);
    } else if (box->type == Q_BOX_TEXT && box->run != NULL) {
        q_font_render_run(box->run, text_color, box->tile, w, h, 0, 0);
        q_paint_text_decoration(box, text_color);
    } else if (box->type == Q_BOX_TEXT) {
        q_paint_text_decoration(box, text_color);
    }

    /*
     * Scrollbars must be part of this box's own ("self") content, since the
     * SDL2 backend composites self_tile and children separately and only
     * ever uploads self_tile as a texture. Painting scrollbars later (after
     * children, directly onto box->tile) would leave them out of self_tile
     * entirely, making them invisible under that backend. Painting them here
     * -- before the self_tile snapshot below -- keeps the previous overlay
     * look for backends that composite the full box->tile subtree, while
     * also making them visible for the self_tile based renderer.
     */
    q_paint_scrollbar(box, 1);
    q_paint_scrollbar(box, 0);

    if (box->self_tile_w != w || box->self_tile_h != h) {
        free(box->self_tile);
        box->self_tile = NULL;
    }
    if (box->self_tile == NULL) {
        box->self_tile = (uint8_t *) malloc((size_t) w * (size_t) h * 4u);
    }
    if (box->self_tile != NULL) {
        memcpy(box->self_tile, box->tile, (size_t) w * (size_t) h * 4u);
        box->self_tile_w = w;
        box->self_tile_h = h;
        box->self_tile_revision++;
        if (box->self_tile_revision == 0u) {
            box->self_tile_revision = 1u;
        }
    } else {
        box->self_tile_w = 0;
        box->self_tile_h = 0;
        box->self_tile_revision = 0;
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
            if (repaint_children) {
                q_paint_box_child(box, entries[i].box);
            } else {
                q_paint_box_child_cached(box, entries[i].box);
            }
        }
    } else {
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            if (repaint_children) {
                q_paint_box_child(box, child);
            } else {
                q_paint_box_child_cached(box, child);
            }
        }
    }

    q_paint_apply_border_radius(box);

#ifdef Q_DEBUG_BOXES
    {
        /* Draw 1px magenta outline around every painted box */
        uint32_t dbg_color = 0xFF00FFFFu;
        int dbg_w = box->tile_w, dbg_h = box->tile_h;
        q_paint_fill_rect(box->tile, dbg_w, dbg_h, 0, 0, dbg_w, 1, dbg_color);
        q_paint_fill_rect(box->tile, dbg_w, dbg_h, 0, dbg_h - 1, dbg_w, 1, dbg_color);
        q_paint_fill_rect(box->tile, dbg_w, dbg_h, 0, 0, 1, dbg_h, dbg_color);
        q_paint_fill_rect(box->tile, dbg_w, dbg_h, dbg_w - 1, 0, 1, dbg_h, dbg_color);
    }
#endif

    free(entries);
}

void q_paint_box(q_box_t *box)
{
    q_paint_box_internal(box, 1);
}

void q_paint_box_cached(q_box_t *box)
{
    q_paint_box_internal(box, 0);
}
