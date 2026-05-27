#include "quanton.h"

#include <ctype.h>
#include <stdlib.h>

#define Q_LINE_DEFAULT_FONT_SIZE   16.0f
#define Q_LINE_DEFAULT_FONT_WEIGHT 400
#define Q_LINE_WORD_SPACING        4.0f

static float measure_word(const char *text, size_t len)
{
    q_font_cache_t *cache;
    q_font_t *font;
    float w;

    cache = q_font_cache_create();
    if (cache == NULL) {
        return (float) len * (Q_LINE_DEFAULT_FONT_SIZE * 0.6f);
    }
    font = q_font_match(cache, "sans-serif",
                        Q_LINE_DEFAULT_FONT_SIZE,
                        Q_LINE_DEFAULT_FONT_WEIGHT);
    if (font != NULL) {
        w = q_font_measure(font, text, len);
    } else {
        w = (float) len * (Q_LINE_DEFAULT_FONT_SIZE * 0.6f);
    }
    q_font_cache_destroy(cache);
    return (w > 0.0f) ? w : (float) len * (Q_LINE_DEFAULT_FONT_SIZE * 0.6f);
}

static q_box_t *make_line_box(q_box_t *parent)
{
    q_box_t *line = (q_box_t *) calloc(1, sizeof(*line));

    if (line == NULL) {
        return NULL;
    }
    line->type = Q_BOX_LINE;
    line->parent = parent;
    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = line;
        line->prev_sibling = parent->last_child;
    } else {
        parent->first_child = line;
    }
    parent->last_child = line;
    return line;
}

static q_box_t *make_word_box(q_box_t *line_parent,
                               const char *text, size_t len,
                               float w, float h)
{
    q_box_t *word = (q_box_t *) calloc(1, sizeof(*word));

    if (word == NULL) {
        return NULL;
    }
    word->type = Q_BOX_TEXT;
    word->text = text;
    word->text_len = len;
    word->width = w;
    word->height = h;
    word->parent = line_parent;
    if (line_parent->last_child != NULL) {
        line_parent->last_child->next_sibling = word;
        word->prev_sibling = line_parent->last_child;
    } else {
        line_parent->first_child = word;
    }
    line_parent->last_child = word;
    return word;
}

/*
 * q_layout_line_wrap — break the inline container's Q_BOX_TEXT children
 * into Q_BOX_LINE children, each holding word-level Q_BOX_TEXT boxes.
 *
 * Words are split at whitespace boundaries.  When a word does not fit on
 * the current line (and the line is non-empty), a new line is opened.
 * Word boxes point directly into the original DOM text data (no copy).
 * The original unsplit Q_BOX_TEXT children are consumed and freed.
 *
 * If the container already has Q_BOX_LINE children (re-measure without
 * tree rebuild) the function returns immediately; a full relayout rebuilds
 * the tree from scratch via q_layout_build_tree().
 */
void q_layout_line_wrap(q_box_t *ic)
{
    q_box_t *orig;
    q_box_t *next_orig;
    q_box_t *line = NULL;
    float cursor_x = 0.0f;
    float line_h = 0.0f;
    float container_w;

    if (ic == NULL) {
        return;
    }

    /* Already wrapped — nothing to do */
    if (ic->first_child != NULL && ic->first_child->type == Q_BOX_LINE) {
        return;
    }

    container_w = ic->width;

    /* Detach original text children; we will rebuild from them */
    orig = ic->first_child;
    ic->first_child = NULL;
    ic->last_child = NULL;

    for (; orig != NULL; orig = next_orig) {
        const char *text = orig->text;
        size_t text_len = orig->text_len;
        size_t i = 0;

        next_orig = orig->next_sibling;

        while (i < text_len) {
            size_t word_start;
            size_t word_len;
            float word_w;
            float word_h;

            /* skip whitespace */
            while (i < text_len && isspace((unsigned char) text[i])) {
                ++i;
            }
            if (i >= text_len) {
                break;
            }

            word_start = i;
            while (i < text_len && !isspace((unsigned char) text[i])) {
                ++i;
            }
            word_len = i - word_start;

            word_w = measure_word(text + word_start, word_len);
            word_h = Q_LINE_DEFAULT_FONT_SIZE * 1.2f;

            /* Open first line if needed */
            if (line == NULL) {
                line = make_line_box(ic);
                if (line == NULL) {
                    goto cleanup;
                }
                cursor_x = 0.0f;
                line_h = 0.0f;
            }

            /* Break to new line when word doesn't fit on a non-empty line */
            if (line->first_child != NULL && container_w > 0.0f
                && cursor_x + word_w > container_w) {
                line->height = line_h;
                line = make_line_box(ic);
                if (line == NULL) {
                    goto cleanup;
                }
                cursor_x = 0.0f;
                line_h = 0.0f;
            }

            if (make_word_box(line, text + word_start, word_len,
                              word_w, word_h) == NULL) {
                goto cleanup;
            }

            cursor_x += word_w + Q_LINE_WORD_SPACING;
            if (word_h > line_h) {
                line_h = word_h;
            }
        }

        /* Free the original (unsplit) text box; text pointer is DOM-owned */
        q_shaped_run_free(orig->run);
        free(orig->tile);
        free(orig);
    }

    if (line != NULL) {
        line->height = line_h;
    }
    return;

cleanup:
    /* Allocation failure: free remaining original text boxes */
    for (; orig != NULL; orig = next_orig) {
        next_orig = orig->next_sibling;
        q_shaped_run_free(orig->run);
        free(orig->tile);
        free(orig);
    }
}
