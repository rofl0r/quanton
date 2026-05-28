#include "quanton.h"

#include <stdlib.h>

static int q_float_overlaps(const q_box_t *box, float y, float h)
{
    float top;
    float bottom;
    float query_bottom;

    if (box == NULL || h <= 0.0f) {
        return 0;
    }

    top = box->y;
    bottom = box->y + box->height;
    query_bottom = y + h;
    return !(bottom <= y || top >= query_bottom);
}

float q_float_ctx_left_edge(const q_float_ctx_t *ctx, float y, float line_h)
{
    q_float_entry_t *entry;
    float left = 0.0f;

    if (ctx == NULL) {
        return 0.0f;
    }

    for (entry = ctx->left_floats; entry != NULL; entry = entry->next) {
        q_box_t *box = entry->box;
        float edge;

        if (!q_float_overlaps(box, y, line_h)) {
            continue;
        }
        edge = box->x + box->width;
        if (edge > left) {
            left = edge;
        }
    }
    return left;
}

float q_float_ctx_right_edge(const q_float_ctx_t *ctx, float y, float line_h, float containing_w)
{
    q_float_entry_t *entry;
    float right = containing_w;

    if (ctx == NULL) {
        return containing_w;
    }

    for (entry = ctx->right_floats; entry != NULL; entry = entry->next) {
        q_box_t *box = entry->box;
        if (!q_float_overlaps(box, y, line_h)) {
            continue;
        }
        if (box->x < right) {
            right = box->x;
        }
    }
    return right;
}

float q_float_ctx_clear_y(const q_float_ctx_t *ctx, q_clear_type_t clear)
{
    q_float_entry_t *entry;
    float clear_y = 0.0f;

    if (ctx == NULL || clear == Q_CLEAR_NONE) {
        return 0.0f;
    }

    if (clear == Q_CLEAR_LEFT || clear == Q_CLEAR_BOTH) {
        for (entry = ctx->left_floats; entry != NULL; entry = entry->next) {
            float bottom = entry->box->y + entry->box->height;
            if (bottom > clear_y) {
                clear_y = bottom;
            }
        }
    }

    if (clear == Q_CLEAR_RIGHT || clear == Q_CLEAR_BOTH) {
        for (entry = ctx->right_floats; entry != NULL; entry = entry->next) {
            float bottom = entry->box->y + entry->box->height;
            if (bottom > clear_y) {
                clear_y = bottom;
            }
        }
    }

    return clear_y;
}

float q_float_ctx_next_y(const q_float_ctx_t *ctx, float y, float line_h)
{
    q_float_entry_t *entry;
    float next_y = -1.0f;

    if (ctx == NULL || line_h <= 0.0f) {
        return y;
    }

    for (entry = ctx->left_floats; entry != NULL; entry = entry->next) {
        q_box_t *box = entry->box;
        float bottom;

        if (!q_float_overlaps(box, y, line_h)) {
            continue;
        }
        bottom = box->y + box->height;
        if (bottom > y && (next_y < 0.0f || bottom < next_y)) {
            next_y = bottom;
        }
    }

    for (entry = ctx->right_floats; entry != NULL; entry = entry->next) {
        q_box_t *box = entry->box;
        float bottom;

        if (!q_float_overlaps(box, y, line_h)) {
            continue;
        }
        bottom = box->y + box->height;
        if (bottom > y && (next_y < 0.0f || bottom < next_y)) {
            next_y = bottom;
        }
    }

    return (next_y < 0.0f) ? y : next_y;
}

int q_float_ctx_add(q_float_ctx_t *ctx, q_box_t *float_box, q_float_type_t side)
{
    q_float_entry_t *entry;
    q_float_entry_t **head;

    if (ctx == NULL || float_box == NULL || side == Q_FLOAT_NONE) {
        return -1;
    }

    entry = (q_float_entry_t *) calloc(1, sizeof(*entry));
    if (entry == NULL) {
        return -1;
    }
    entry->box = float_box;

    head = (side == Q_FLOAT_RIGHT) ? &ctx->right_floats : &ctx->left_floats;
    entry->next = *head;
    *head = entry;
    return 0;
}

void q_float_ctx_reset(q_float_ctx_t *ctx)
{
    q_float_entry_t *entry;
    q_float_entry_t *next;

    if (ctx == NULL) {
        return;
    }

    entry = ctx->left_floats;
    while (entry != NULL) {
        next = entry->next;
        free(entry);
        entry = next;
    }
    ctx->left_floats = NULL;

    entry = ctx->right_floats;
    while (entry != NULL) {
        next = entry->next;
        free(entry);
        entry = next;
    }
    ctx->right_floats = NULL;
}
