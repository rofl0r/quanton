#include "quanton.h"

#include <math.h>

#define Q_LAYOUT_DEFAULT_FONT_SIZE 16.0f
#define Q_LAYOUT_DEFAULT_FONT_WEIGHT 400

static q_font_t *q_layout_default_font(void)
{
    static q_font_cache_t *cache;

    if (cache == NULL) {
        cache = q_font_cache_create();
        if (cache == NULL) {
            return NULL;
        }
    }

    return q_font_match(cache,
                        "sans-serif",
                        Q_LAYOUT_DEFAULT_FONT_SIZE,
                        Q_LAYOUT_DEFAULT_FONT_WEIGHT);
}

static void q_layout_measure_text(q_box_t *box)
{
    q_font_t *font;

    q_shaped_run_free(box->run);
    box->run = NULL;

    font = q_layout_default_font();
    if (font != NULL) {
        box->run = q_font_shape_run(font, box->text, box->text_len);
    }

    if (box->run != NULL) {
        box->width = box->run->total_advance;
        box->height = box->run->ascender + fabsf(box->run->descender);
        if (box->run->line_gap > 0.0f) {
            box->height += box->run->line_gap;
        }
        return;
    }

    box->width = (float) box->text_len * (Q_LAYOUT_DEFAULT_FONT_SIZE * 0.6f);
    box->height = Q_LAYOUT_DEFAULT_FONT_SIZE;
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

    box->width = (containing_w > 0.0f) ? containing_w : 0.0f;
    box->height = 0.0f;

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

    if (box == NULL) {
        return;
    }

    box->x = origin_x;
    box->y = origin_y;

    child_y = origin_y;
    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        q_layout_position(child, origin_x, child_y);
        child_y += child->height;
    }
}
