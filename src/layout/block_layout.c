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

    box->width = (containing_w > 0.0f) ? containing_w : 0.0f;
    box->height = 0.0f;

    if (box->is_flex_container) {
        size_t child_count = 0;
        float item_w = 0.0f;
        float max_h = 0.0f;
        float used_w = 0.0f;

        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            ++child_count;
        }

        if (child_count > 0 && box->width > 0.0f) {
            item_w = box->width / (float) child_count;
        }

        for (child = box->first_child; child != NULL; child = child->next_sibling) {
            q_layout_measure(child, item_w, containing_h);
            used_w += child->width;
            if (child->height > max_h) {
                max_h = child->height;
            }
        }

        if (box->width <= 0.0f) {
            box->width = used_w;
        }
        box->height = max_h;
        return;
    }

    if (box->type == Q_BOX_INLINE_CONTAINER) {
        /* Split text children into word-level line boxes first */
        q_layout_line_wrap(box);
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        q_layout_measure(child, box->width, containing_h);
        used_h += child->height;
        if (child->width > max_w) {
            max_w = child->width;
        }
    }

    if (box->width <= 0.0f) {
        box->width = max_w;
    }
    box->height = used_h;
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
            q_layout_position(child, cursor_x, origin_y);
            cursor_x += child->width;
        }
        return;
    }

    child_y = origin_y;
    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        q_layout_position(child, origin_x, child_y);
        child_y += child->height;
    }
}
