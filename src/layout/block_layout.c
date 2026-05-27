#include "quanton.h"

#include <math.h>

#define Q_LAYOUT_DEFAULT_FONT_SIZE 16.0f
#define Q_LAYOUT_DEFAULT_FONT_WEIGHT 400
#define Q_LAYOUT_WORD_SPACING 4.0f

static void q_layout_measure_text(q_box_t *box)
{
    static q_font_cache_t *cache;
    q_font_t *font = NULL;
    q_shaped_run_t *run = NULL;

    q_shaped_run_free(box->run);
    box->run = NULL;

    if (cache == NULL) {
        cache = q_font_cache_create();
    }

    if (cache != NULL) {
        font = q_font_match(cache,
                            "sans-serif",
                            Q_LAYOUT_DEFAULT_FONT_SIZE,
                            Q_LAYOUT_DEFAULT_FONT_WEIGHT);
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
        box->width = (float) box->text_len * (Q_LAYOUT_DEFAULT_FONT_SIZE * 0.6f);
        box->height = Q_LAYOUT_DEFAULT_FONT_SIZE;
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
        return;
    }

    if (box->type == Q_BOX_IMAGE) {
        q_layout_measure_image(box);
        return;
    }

    if (box->type == Q_BOX_LINE) {
        /* Line width spans the container; height = tallest child */
        float max_h = 0.0f;

        box->width = (containing_w > 0.0f) ? containing_w : 0.0f;
        box->height = 0.0f;
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            q_layout_measure(child, box->width, containing_h);
            if (child->height > max_h) {
                max_h = child->height;
            }
        }
        box->height = max_h;
        return;
    }

    /* Apply explicit CSS width override before measuring children */
    if (!isnan(box->style_width)) {
        box->width = box->style_width;
    } else {
        box->width = (containing_w > 0.0f) ? containing_w : 0.0f;
    }
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

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        q_layout_measure(child, box->width, containing_h);
        if (!q_is_out_of_flow(child)) {
            used_h += child->height;
            if (child->width > max_w) {
                max_w = child->width;
            }
        }
    }

    if (isnan(box->style_width) && box->width <= 0.0f) {
        box->width = max_w;
    }
    box->height = used_h;
    if (!isnan(box->style_height)) {
        box->height = box->style_height;
    }
    q_layout_clamp_box_scroll(box, max_w, used_h);
}

void q_layout_position(q_box_t *box, float origin_x, float origin_y)
{
    q_box_t *child;
    float child_y;
    float cursor_x;

    if (box == NULL) {
        return;
    }

    box->x = origin_x;
    box->y = origin_y;

    if (box->type == Q_BOX_LINE) {
        /* Position word children left-to-right */
        cursor_x = origin_x;
        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            child->x = cursor_x;
            child->y = origin_y;
            cursor_x += child->width + Q_LAYOUT_WORD_SPACING;
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

    child_y = origin_y;
    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        if (!q_is_out_of_flow(child)) {
            q_layout_position(child, origin_x, child_y);
            child_y += child->height;
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
