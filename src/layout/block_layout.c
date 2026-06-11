#include "quanton.h"

#include <math.h>
#include <string.h>

#define Q_LAYOUT_DEFAULT_FONT_SIZE 16.0f
#define Q_LAYOUT_DEFAULT_FONT_WEIGHT 400
#define Q_LAYOUT_WORD_SPACING 0.0f

static float q_layout_resolve_font_size(const q_box_t *box)
{
    const q_box_t *cur = box;

    while (cur != NULL) {
        if (!isnan(cur->font_size) && cur->font_size > 0.0f) {
            return cur->font_size;
        }
        cur = cur->parent;
    }
    return Q_LAYOUT_DEFAULT_FONT_SIZE;
}

static int q_layout_resolve_font_weight(const q_box_t *box)
{
    const q_box_t *cur = box;

    while (cur != NULL) {
        if (cur->font_weight > 0) {
            return cur->font_weight;
        }
        cur = cur->parent;
    }
    return Q_LAYOUT_DEFAULT_FONT_WEIGHT;
}

static int q_layout_resolve_font_style(const q_box_t *box)
{
    const q_box_t *cur = box;

    while (cur != NULL) {
        if (cur->font_style != Q_FONT_STYLE_NORMAL) {
            return (int) cur->font_style;
        }
        cur = cur->parent;
    }
    return (int) Q_FONT_STYLE_NORMAL;
}

static const char *q_layout_resolve_font_family(const q_box_t *box)
{
    const q_box_t *cur = box;

    while (cur != NULL) {
        if (cur->font_family != NULL && cur->font_family[0] != '\0') {
            return cur->font_family;
        }
        cur = cur->parent;
    }
    return "sans-serif";
}

static void q_layout_measure_text(q_box_t *box)
{
    static q_font_cache_t *cache;
    q_font_t *font = NULL;
    q_shaped_run_t *run = NULL;
    float font_size = q_layout_resolve_font_size(box);
    int font_weight = q_layout_resolve_font_weight(box);
    int font_style  = q_layout_resolve_font_style(box);
    const char *font_family = q_layout_resolve_font_family(box);

    q_shaped_run_free(box->run);
    box->run = NULL;

    if (cache == NULL) {
        cache = q_font_cache_create();
    }

    if (cache != NULL) {
        font = q_font_match(cache,
                            font_family,
                            font_size,
                            font_weight,
                            font_style);
    }

    if (font != NULL) {
        run = q_font_shape_run(font, box->text, box->text_len);
        if (run != NULL) {
            run->font = font;
        }
    }
    else {
        /* Clear run explicitly when shaping is unavailable */
        box->run = NULL;
    }

    if (run != NULL) {
        /* Keep a live font reference for paint-time glyph rasterization. */
        box->run = run;
        box->width = run->total_advance;
        box->height = run->ascender + fabsf(run->descender);
        if (run->line_gap > 0.0f) {
            box->height += run->line_gap;
        }
    } else {
        box->width = (float) box->text_len * (font_size * 0.6f);
        box->height = font_size;
    }

}

static void q_layout_measure_image(q_box_t *box)
{
    float intrinsic_w = 0.0f;
    float intrinsic_h = 0.0f;

    if (box->image != NULL) {
        intrinsic_w = (float) q_image_width(box->image);
        intrinsic_h = (float) q_image_height(box->image);
    }

    if (!isnan(box->style_width) && !isnan(box->style_height)) {
        box->width = box->style_width;
        box->height = box->style_height;
        return;
    }

    if (!isnan(box->style_width)) {
        box->width = box->style_width;
        if (intrinsic_w > 0.0f && intrinsic_h > 0.0f) {
            box->height = box->style_width * (intrinsic_h / intrinsic_w);
        } else {
            box->height = 0.0f;
        }
        return;
    }

    if (!isnan(box->style_height)) {
        box->height = box->style_height;
        if (intrinsic_w > 0.0f && intrinsic_h > 0.0f) {
            box->width = box->style_height * (intrinsic_w / intrinsic_h);
        } else {
            box->width = 0.0f;
        }
        return;
    }

    box->width = intrinsic_w;
    box->height = intrinsic_h;
}

/* Returns 1 if this box is taken out of normal flow */
static int q_is_out_of_flow(const q_box_t *box)
{
    return box->position == Q_POSITION_ABSOLUTE
        || box->position == Q_POSITION_FIXED;
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

static float q_layout_clampf(float value, float max_value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void q_layout_clamp_box_scroll(q_box_t *box, float content_w, float content_h)
{
    float viewport_w;
    float viewport_h;
    float max_x;
    float max_y;
    float border_left;
    float border_right;
    float border_top;
    float border_bottom;

    if (box == NULL) {
        return;
    }

    border_left = ceilf(box->border_width[3]);
    border_right = ceilf(box->border_width[1]);
    border_top = ceilf(box->border_width[0]);
    border_bottom = ceilf(box->border_width[2]);
    viewport_w = box->width - border_left - border_right;
    viewport_h = box->height - border_top - border_bottom;
    if (viewport_w < 0.0f) {
        viewport_w = 0.0f;
    }
    if (viewport_h < 0.0f) {
        viewport_h = 0.0f;
    }

    max_x = content_w - viewport_w;
    max_y = content_h - viewport_h;
    if (max_x < 0.0f) {
        max_x = 0.0f;
    }
    if (max_y < 0.0f) {
        max_y = 0.0f;
    }

    if (q_box_scrolls_x(box)) {
        box->scroll_x = q_layout_clampf(box->scroll_x, max_x);
    } else {
        box->scroll_x = 0.0f;
    }

    if (q_box_scrolls_y(box)) {
        box->scroll_y = q_layout_clampf(box->scroll_y, max_y);
    } else {
        box->scroll_y = 0.0f;
    }
}

static float q_layout_maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static void q_layout_apply_minmax(q_box_t *box)
{
    if (box == NULL) {
        return;
    }

    if (!isnan(box->style_max_width) && box->width > box->style_max_width) {
        box->width = box->style_max_width;
    }
    if (!isnan(box->style_min_width) && box->width < box->style_min_width) {
        box->width = box->style_min_width;
    }
    if (!isnan(box->style_max_height) && box->height > box->style_max_height) {
        box->height = box->style_max_height;
    }
    if (!isnan(box->style_min_height) && box->height < box->style_min_height) {
        box->height = box->style_min_height;
    }
}

static float q_layout_resolve_clear_y(const q_float_ctx_t *ctx, float base_y, q_clear_type_t clear_type)
{
    if (clear_type == Q_CLEAR_NONE) {
        return base_y;
    }

    return q_layout_maxf(base_y, q_float_ctx_clear_y(ctx, clear_type));
}

static float q_layout_block_place_float(q_float_ctx_t *ctx, q_box_t *child,
                                        float containing_w, float start_y)
{
    float y = start_y;
    size_t guard = 0;

    while (guard < 4096u) {
        float line_h = (child->height > 0.0f) ? child->height : 1.0f;
        float left = q_float_ctx_left_edge(ctx, y, line_h);
        float right = q_float_ctx_right_edge(ctx, y, line_h, containing_w);
        float avail = right - left;

        if (avail >= child->width) {
            if (child->float_type == Q_FLOAT_RIGHT) {
                child->x = right - child->width;
            } else {
                child->x = left;
            }
            child->y = y;
            return y + child->height;
        }

        {
            float next_y = q_float_ctx_next_y(ctx, y, line_h);
            if (next_y <= y) {
                y += line_h;
            } else {
                y = next_y;
            }
        }
        ++guard;
    }

    child->x = 0.0f;
    child->y = y;
    return y + child->height;
}

void q_layout_measure(q_box_t *box, float containing_w, float containing_h)
{
    q_box_t *child;
    float used_h = 0.0f;
    float max_w = 0.0f;

    (void) containing_h;

    if (box == NULL) {
        return;
    }

    if (box->type == Q_BOX_TEXT) {
        q_layout_measure_text(box);
        q_layout_apply_minmax(box);
        return;
    }

    if (box->type == Q_BOX_IMAGE) {
        q_layout_measure_image(box);
        q_layout_apply_minmax(box);
        return;
    }

    if (box->type == Q_BOX_LINE) {
        /* Line width spans the container; height = tallest child.
         * Inline-block children are already measured shrink-to-fit by
         * q_layout_line_wrap; re-measuring them with the line width would
         * overwrite their natural width, so we skip them here.
         * When containing_w is 0 (shrink-to-fit context) the LINE itself
         * reports the sum of its children's widths so the value propagates
         * up to the surrounding inline-block box. */
        float max_h = 0.0f;
        float used_w = 0.0f;

        box->width = (containing_w > 0.0f) ? containing_w : 0.0f;
        box->height = 0.0f;
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            if (!child->is_inline_block) {
                q_layout_measure(child, box->width, containing_h);
            }
            used_w += child->width;
            if (child->height > max_h) {
                max_h = child->height;
            }
        }
        if (containing_w <= 0.0f) {
            box->width = used_w;
        }
        box->height = max_h;
        q_layout_apply_minmax(box);
        return;
    }

    if (box->type == Q_BOX_TABLE) {
        q_table_measure(box, containing_w);
        q_layout_apply_minmax(box);
        return;
    }

    /* Apply explicit CSS width override before measuring children */
    if (!isnan(box->style_width)) {
        box->width = box->style_width;
    } else if (!isnan(box->style_width_pct) && containing_w > 0.0f) {
        box->width = containing_w * box->style_width_pct / 100.0f;
    } else {
        box->width = (containing_w > 0.0f) ? containing_w : 0.0f;
    }
    q_layout_apply_minmax(box);
    box->height = 0.0f;

    if (box->is_flex_container) {
        size_t child_count = 0;
        float item_w = 0.0f;
        float max_h = 0.0f;
        float used_w = 0.0f;

        /* Count only in-flow flex children */
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            if (!q_is_out_of_flow(child)) {
                ++child_count;
            }
        }

        if (child_count > 0 && box->width > 0.0f) {
            item_w = box->width / (float) child_count;
        }

        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            q_layout_measure(child, item_w, containing_h);
            if (!q_is_out_of_flow(child)) {
                used_w += child->width;
                if (child->height > max_h) {
                    max_h = child->height;
                }
            }
        }

        if (isnan(box->style_width) && box->width <= 0.0f) {
            box->width = used_w;
        }
        box->height = max_h;
        if (!isnan(box->style_height)) {
            box->height = box->style_height;
        }
        q_layout_clamp_box_scroll(box, used_w, max_h);
        return;
    }

    if (box->type == Q_BOX_INLINE_CONTAINER) {
        /* Split text children into word-level line boxes first */
        q_layout_line_wrap(box);
    }

    {
        q_float_ctx_t float_ctx;
        float pad_top    = box->padding_top;
        float pad_right  = box->padding_right;
        float pad_bottom = box->padding_bottom;
        float pad_left   = box->padding_left;
        float flow_y = pad_top;

        if (box->parent == NULL) {
            pad_top += box->margin_top;
            pad_right += box->margin_right;
            pad_bottom += box->margin_bottom;
            pad_left += box->margin_left;
            flow_y = pad_top;
        }

        memset(&float_ctx, 0, sizeof(float_ctx));
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            if (q_is_out_of_flow(child)) {
                continue;
            }

            if (child->float_type != Q_FLOAT_NONE) {
                float float_start_y = flow_y + child->margin_top;
                float placed_bottom;
                float inner_w = box->width - pad_left - pad_right;
                if (inner_w < 0.0f) inner_w = 0.0f;

                float_start_y = q_layout_resolve_clear_y(&float_ctx, float_start_y,
                                                         child->clear_type);

                q_layout_measure(child, inner_w - child->margin_left - child->margin_right, containing_h);
                placed_bottom = q_layout_block_place_float(&float_ctx, child, inner_w, float_start_y);
                if (q_float_ctx_add(&float_ctx, child, child->float_type) != 0) {
                    continue;
                }
                child->x += child->margin_left;
                if (child->x + child->width > max_w) {
                    max_w = child->x + child->width + child->margin_right;
                }
                if (placed_bottom > used_h) {
                    used_h = placed_bottom + child->margin_bottom;
                }
                continue;
            }

            {
                float child_y = flow_y + child->margin_top;
                float probe_h = 1.0f;
                float left;
                float right;
                float avail_w;
                float inner_w;

                child_y = q_layout_resolve_clear_y(&float_ctx, child_y, child->clear_type);

                inner_w = box->width - pad_left - pad_right;
                if (inner_w < 0.0f) inner_w = 0.0f;

                left  = q_float_ctx_left_edge(&float_ctx, child_y, probe_h);
                right = q_float_ctx_right_edge(&float_ctx, child_y, probe_h, inner_w);
                avail_w = right - left;
                if (avail_w < 0.0f) avail_w = 0.0f;

                avail_w -= child->margin_left + child->margin_right;
                if (avail_w < 0.0f) avail_w = 0.0f;
                q_layout_measure(child, avail_w, containing_h);
                if (!isnan(child->style_width) && child->margin_left_auto && child->margin_right_auto) {
                    float remaining = avail_w - child->width;
                    if (remaining < 0.0f) {
                        remaining = 0.0f;
                    }
                    child->margin_left = remaining * 0.5f;
                    child->margin_right = remaining * 0.5f;
                }

                left  = q_float_ctx_left_edge(&float_ctx, child_y, q_layout_maxf(child->height, 1.0f));
                right = q_float_ctx_right_edge(&float_ctx, child_y,
                                               q_layout_maxf(child->height, 1.0f), inner_w);
                avail_w = right - left;
                if (avail_w < 0.0f) avail_w = 0.0f;
                avail_w -= child->margin_left + child->margin_right;
                if (avail_w < 0.0f) avail_w = 0.0f;
                if (child->type == Q_BOX_INLINE_CONTAINER) {
                    q_layout_measure(child, avail_w, containing_h);
                }

                child->x = pad_left + left + child->margin_left;
                child->y = child_y;
                flow_y = child_y + child->height + child->margin_bottom;
                used_h = q_layout_maxf(used_h, flow_y);
                if (child->x + child->width + child->margin_right > max_w) {
                    max_w = child->x + child->width + child->margin_right;
                }
            }
        }
        used_h += pad_bottom;
        q_float_ctx_reset(&float_ctx);
    }

    if (box->is_inline_block && isnan(box->style_width)) {
        box->width = max_w;
    } else if (isnan(box->style_width) && box->width <= 0.0f) {
        box->width = max_w;
    }
    box->height = used_h;
    if (!isnan(box->style_height)) {
        box->height = box->style_height;
    }
    q_layout_apply_minmax(box);
    q_layout_clamp_box_scroll(box, max_w, used_h);
}

void q_layout_position(q_box_t *box, float origin_x, float origin_y)
{
    q_box_t *child;
    float cursor_x;

    if (box == NULL) {
        return;
    }

    box->x = origin_x;
    box->y = origin_y;

    if (box->type == Q_BOX_TABLE) {
        q_table_position(box, origin_x, origin_y);
        return;
    }

    if (box->type == Q_BOX_LINE) {
        /* Position word children left-to-right */
        float line_h = (box->height > 0.0f) ? box->height : 0.0f;
        q_text_align_type_t align = Q_TEXT_ALIGN_LEFT;
        float content_w = 0.0f;
        size_t nchildren = 0;
        if (box->parent != NULL) {
            align = box->parent->text_align;
        }
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            content_w += child->width;
            ++nchildren;
        }
        if (nchildren > 1) {
            content_w += (float) (nchildren - 1) * Q_LAYOUT_WORD_SPACING;
        }
        cursor_x = origin_x;
        if (align == Q_TEXT_ALIGN_CENTER && box->width > content_w) {
            cursor_x += (box->width - content_w) * 0.5f;
        } else if (align == Q_TEXT_ALIGN_RIGHT && box->width > content_w) {
            cursor_x += (box->width - content_w);
        }
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            float y = origin_y;

            if (child->vertical_align == Q_VERTICAL_ALIGN_TOP) {
                y = origin_y;
            } else if (child->vertical_align == Q_VERTICAL_ALIGN_MIDDLE) {
                y = origin_y + ((line_h - child->height) * 0.5f);
            } else if (child->vertical_align == Q_VERTICAL_ALIGN_BOTTOM) {
                y = origin_y + (line_h - child->height);
            } else if (child->vertical_align == Q_VERTICAL_ALIGN_SUB) {
                y += line_h * 0.2f;
            } else if (child->vertical_align == Q_VERTICAL_ALIGN_SUPER) {
                y -= line_h * 0.2f;
            }

            child->x = cursor_x;
            child->y = y;
            cursor_x += child->width + Q_LAYOUT_WORD_SPACING;
            /* Inline-block children carry a full sub-tree; recurse so their
             * internal coordinates are converted to absolute form before
             * the paint pass computes dx = child->x - parent->x offsets. */
            if (child->is_inline_block) {
                q_layout_position(child, child->x, child->y);
            }
        }
        return;
    }

    if (box->is_flex_container) {
        cursor_x = origin_x;
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            if (!q_is_out_of_flow(child)) {
                q_layout_position(child, cursor_x, origin_y);
                cursor_x += child->width;
            }
        }
        return;
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        if (!q_is_out_of_flow(child)) {
            q_layout_position(child, origin_x + child->x, origin_y + child->y);
        }
    }
}

/* ── Second pass: position absolute/fixed boxes ──────────────────────────── */

/* Find the nearest ancestor with position != static, or the root for fixed. */
static q_box_t *q_containing_block(q_box_t *box)
{
    q_box_t *p = box->parent;

    if (box->position == Q_POSITION_FIXED) {
        /* Walk up to the root */
        while (p != NULL && p->parent != NULL) {
            p = p->parent;
        }
        return p;
    }

    /* Absolute: nearest non-static ancestor */
    while (p != NULL) {
        if (p->position != Q_POSITION_STATIC) {
            return p;
        }
        if (p->parent == NULL) {
            /* Hit root — it is the containing block even though static */
            return p;
        }
        p = p->parent;
    }
    return NULL;
}

/* Recursively walk the tree and resolve abs/fixed boxes. */
static void q_position_abs_walk(q_box_t *box)
{
    q_box_t *child;

    if (box == NULL) {
        return;
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        if (q_is_out_of_flow(child)) {
            q_box_t *cb = q_containing_block(child);

            if (cb != NULL) {
                float ox = cb->x;
                float oy = cb->y;
                float cw = cb->width;
                float ch = cb->height;

                /* Measure the positioned box itself now that CB dimensions
                 * are finalised.  When width/height are not explicitly set,
                 * pass 0 so q_layout_measure shrinks to content size rather
                 * than stretching to fill the containing block. */
                float mw = !isnan(child->style_width)  ? child->style_width  : 0.0f;
                float mh = !isnan(child->style_height) ? child->style_height : 0.0f;

                q_layout_measure(child, mw, mh);

                /* Resolve horizontal offset: prefer left, then right */
                if (!isnan(child->style_left)) {
                    child->x = ox + child->style_left;
                } else if (!isnan(child->style_right)) {
                    child->x = ox + cw - child->style_right - child->width;
                } else {
                    child->x = ox; /* default: top-left of CB */
                }

                /* Resolve vertical offset: prefer top, then bottom */
                if (!isnan(child->style_top)) {
                    child->y = oy + child->style_top;
                } else if (!isnan(child->style_bottom)) {
                    child->y = oy + ch - child->style_bottom - child->height;
                } else {
                    child->y = oy;
                }

                /* Recurse into the positioned box */
                q_layout_position(child, child->x, child->y);
            }
        }
        /* Always recurse to find deeper out-of-flow descendants */
        q_position_abs_walk(child);
    }
}

void q_layout_position_absolute(q_box_t *root)
{
    q_position_abs_walk(root);
}
