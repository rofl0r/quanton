#include "quanton.h"

#include <ctype.h>
#include <stdlib.h>

#define Q_LINE_DEFAULT_FONT_SIZE   16.0f
#define Q_LINE_DEFAULT_FONT_WEIGHT 400
#define Q_LINE_WORD_SPACING        0.0f

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
                        Q_LINE_DEFAULT_FONT_WEIGHT,
                        Q_FONT_STYLE_NORMAL);
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
                               lxb_dom_node_t *dom_node,
                               const char *text, size_t len,
                               float w, float h,
                               uint8_t text_decoration,
                               q_vertical_align_type_t vertical_align)
{
    q_box_t *word = (q_box_t *) calloc(1, sizeof(*word));

    if (word == NULL) {
        return NULL;
    }
    word->type = Q_BOX_TEXT;
    word->text = text;
    word->text_len = len;
    word->dom_node = dom_node;
    word->width = w;
    word->height = h;
    word->text_decoration = text_decoration;
    word->vertical_align = vertical_align;
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

static int append_existing_box_to_line(q_box_t *line, q_box_t *child)
{
    if (line == NULL || child == NULL) {
        return -1;
    }
    child->next_sibling = NULL;
    child->prev_sibling = line->last_child;
    child->parent = line;
    if (line->last_child != NULL) {
        line->last_child->next_sibling = child;
    } else {
        line->first_child = child;
    }
    line->last_child = child;
    return 0;
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
    int pending_space = 0; /* set when a text token ended in whitespace */
    const int no_wrap = (ic->white_space == Q_WHITE_SPACE_NOWRAP || ic->white_space == Q_WHITE_SPACE_PRE);
    const float default_line_h = Q_LINE_DEFAULT_FONT_SIZE * 1.2f;
    const float space_w = measure_word(" ", 1u);

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
        next_orig = orig->next_sibling;

        if (orig->type == Q_BOX_TEXT) {
            const char *text = orig->text;
            size_t text_len = orig->text_len;
            size_t i = 0;

            if (ic->white_space == Q_WHITE_SPACE_PRE) {
                while (i < text_len) {
                    size_t seg_start = i;
                    size_t seg_len;
                    float seg_w;
                    float seg_h = default_line_h;

                    while (i < text_len && text[i] != '\n') {
                        ++i;
                    }
                    seg_len = i - seg_start;
                    if (line == NULL) {
                        line = make_line_box(ic);
                        if (line == NULL) {
                            goto cleanup;
                        }
                    }
                    if (seg_len > 0u) {
                        seg_w = measure_word(text + seg_start, seg_len);
                        if (make_word_box(line, orig->dom_node, text + seg_start, seg_len,
                                          seg_w, seg_h,
                                          orig->text_decoration,
                                          orig->vertical_align) == NULL) {
                            goto cleanup;
                        }
                        cursor_x += seg_w + Q_LINE_WORD_SPACING;
                        if (seg_h > line_h) {
                            line_h = seg_h;
                        }
                    }

                    if (i < text_len && text[i] == '\n') {
                        line->height = (line_h > 0.0f) ? line_h : default_line_h;
                        line = make_line_box(ic);
                        if (line == NULL) {
                            goto cleanup;
                        }
                        cursor_x = 0.0f;
                        line_h = 0.0f;
                        ++i;
                    }
                }
            } else {
                while (i < text_len) {
                    size_t word_start;
                    size_t word_len;
                    float word_w;
                    float word_h = default_line_h;
                    float token_w;

                    while (i < text_len && isspace((unsigned char) text[i])) {
                        pending_space = 1;
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

                    if (line == NULL) {
                        line = make_line_box(ic);
                        if (line == NULL) {
                            goto cleanup;
                        }
                        cursor_x = 0.0f;
                        line_h = 0.0f;
                    }

                    token_w = word_w;
                    if (pending_space && line->first_child != NULL) {
                        token_w += space_w + Q_LINE_WORD_SPACING;
                    }
                    if (!no_wrap
                        && line->first_child != NULL && container_w > 0.0f
                        && cursor_x + token_w > container_w) {
                        line->height = (line_h > 0.0f) ? line_h : default_line_h;
                        line = make_line_box(ic);
                        if (line == NULL) {
                            goto cleanup;
                        }
                        cursor_x = 0.0f;
                        line_h = 0.0f;
                    }

                    if (pending_space && line->first_child != NULL) {
                        if (make_word_box(line, orig->dom_node, " ", 1u,
                                          space_w, word_h,
                                          orig->text_decoration,
                                          orig->vertical_align) == NULL) {
                            goto cleanup;
                        }
                        cursor_x += space_w + Q_LINE_WORD_SPACING;
                        if (word_h > line_h) {
                            line_h = word_h;
                        }
                    }

                    if (make_word_box(line, orig->dom_node, text + word_start, word_len,
                                      word_w, word_h,
                                      orig->text_decoration,
                                      orig->vertical_align) == NULL) {
                        goto cleanup;
                    }

                    cursor_x += word_w + Q_LINE_WORD_SPACING;
                    if (word_h > line_h) {
                        line_h = word_h;
                    }
                    pending_space = 0;
                }
            }

            /* Free the original (unsplit) text box; text pointer is DOM-owned */
            q_shaped_run_free(orig->run);
            free(orig->tile);
            free(orig);
            continue;
        }

        if (orig->type == Q_BOX_LINE_BREAK) {
            if (line == NULL) {
                line = make_line_box(ic);
                if (line == NULL) {
                    goto cleanup;
                }
            }
            line->height = (line_h > 0.0f) ? line_h : default_line_h;
            line = make_line_box(ic);
            if (line == NULL) {
                goto cleanup;
            }
            cursor_x = 0.0f;
            line_h = 0.0f;
            pending_space = 0;
            free(orig);
            continue;
        }

        q_layout_measure(orig, orig->is_inline_block ? 0.0f : container_w, 0.0f);
        if (orig->height <= 0.0f) {
            orig->height = default_line_h;
        }

        if (line == NULL) {
            line = make_line_box(ic);
            if (line == NULL) {
                goto cleanup;
            }
            cursor_x = 0.0f;
            line_h = 0.0f;
        }

        if (!no_wrap
            && line->first_child != NULL && container_w > 0.0f
            && cursor_x + orig->width > container_w) {
            line->height = (line_h > 0.0f) ? line_h : default_line_h;
            line = make_line_box(ic);
            if (line == NULL) {
                goto cleanup;
            }
            cursor_x = 0.0f;
            line_h = 0.0f;
        }

        /* Inject inter-element space when the preceding text ended with
         * whitespace and there is already content on the current line. */
        if (pending_space && line->first_child != NULL) {
            float sp_h = default_line_h;
            if (make_word_box(line, orig->dom_node, " ", 1u,
                              space_w, sp_h, 0,
                              Q_VERTICAL_ALIGN_BASELINE) == NULL) {
                goto cleanup;
            }
            cursor_x += space_w + Q_LINE_WORD_SPACING;
            if (sp_h > line_h) {
                line_h = sp_h;
            }
        }
        pending_space = 0;

        if (append_existing_box_to_line(line, orig) != 0) {
            goto cleanup;
        }

        cursor_x += orig->width + Q_LINE_WORD_SPACING;
        if (orig->height > line_h) {
            line_h = orig->height;
        }
    }

    if (line != NULL) {
        line->height = (line_h > 0.0f) ? line_h : default_line_h;
    }
    return;

cleanup:
    /* Allocation failure: free remaining original text boxes */
    for (; orig != NULL; orig = next_orig) {
        next_orig = orig->next_sibling;
        if (orig->type == Q_BOX_TEXT) {
            q_shaped_run_free(orig->run);
            free(orig->tile);
            free(orig);
        } else {
            q_layout_free_tree(orig);
        }
    }
}
