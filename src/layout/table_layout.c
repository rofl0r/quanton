#include "quanton.h"

#include <math.h>
#include <stdlib.h>

#define Q_DEFAULT_BACKGROUND 0xF2F2F2FFu
#define Q_DEFAULT_BORDER 0x303030FFu
#define Q_DEFAULT_BORDER_WIDTH 1.0f

static q_box_t *q_table_create_anonymous_box(q_box_type_t type)
{
    q_box_t *box = (q_box_t *) calloc(1, sizeof(*box));
    if (box == NULL) {
        return NULL;
    }

    box->type = type;
    box->background_color = Q_DEFAULT_BACKGROUND;
    box->border_color[0] = Q_DEFAULT_BORDER;
    box->border_color[1] = Q_DEFAULT_BORDER;
    box->border_color[2] = Q_DEFAULT_BORDER;
    box->border_color[3] = Q_DEFAULT_BORDER;
    box->border_width[0] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[1] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[2] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[3] = Q_DEFAULT_BORDER_WIDTH;
    box->style_top = (float) NAN;
    box->style_right = (float) NAN;
    box->style_bottom = (float) NAN;
    box->style_left = (float) NAN;
    box->style_width = (float) NAN;
    box->style_height = (float) NAN;

    return box;
}

static void q_box_detach(q_box_t *child)
{
    if (child == NULL) {
        return;
    }

    child->parent = NULL;
    child->next_sibling = NULL;
    child->prev_sibling = NULL;
}

static void q_box_append_child(q_box_t *parent, q_box_t *child)
{
    if (parent == NULL || child == NULL) {
        return;
    }

    child->parent = parent;
    child->prev_sibling = parent->last_child;
    child->next_sibling = NULL;
    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static void q_table_fixup_table_box(q_box_t *table_box)
{
    q_box_t *child;
    q_box_t *next;
    q_box_t *anon_section = NULL;
    q_box_t *anon_row = NULL;

    if (table_box == NULL) {
        return;
    }

    child = table_box->first_child;
    table_box->first_child = NULL;
    table_box->last_child = NULL;

    while (child != NULL) {
        next = child->next_sibling;
        q_box_detach(child);

        switch (child->type) {
            case Q_BOX_TABLE_SECTION:
                q_box_append_child(table_box, child);
                anon_section = NULL;
                anon_row = NULL;
                break;
            case Q_BOX_TABLE_ROW:
                if (anon_section == NULL) {
                    anon_section = q_table_create_anonymous_box(Q_BOX_TABLE_SECTION);
                    if (anon_section == NULL) {
                        q_box_append_child(table_box, child);
                        anon_row = NULL;
                        break;
                    }
                    q_box_append_child(table_box, anon_section);
                }
                q_box_append_child(anon_section, child);
                anon_row = child;
                break;
            case Q_BOX_TABLE_CELL:
                if (anon_section == NULL) {
                    anon_section = q_table_create_anonymous_box(Q_BOX_TABLE_SECTION);
                    if (anon_section == NULL) {
                        q_box_append_child(table_box, child);
                        anon_row = NULL;
                        break;
                    }
                    q_box_append_child(table_box, anon_section);
                }
                if (anon_row == NULL) {
                    anon_row = q_table_create_anonymous_box(Q_BOX_TABLE_ROW);
                    if (anon_row == NULL) {
                        q_box_append_child(table_box, child);
                        break;
                    }
                    q_box_append_child(anon_section, anon_row);
                }
                q_box_append_child(anon_row, child);
                break;
            case Q_BOX_TABLE_CAPTION:
                q_box_append_child(table_box, child);
                anon_section = NULL;
                anon_row = NULL;
                break;
            default:
            {
                q_box_t *section = q_table_create_anonymous_box(Q_BOX_TABLE_SECTION);
                q_box_t *row = q_table_create_anonymous_box(Q_BOX_TABLE_ROW);
                q_box_t *cell = q_table_create_anonymous_box(Q_BOX_TABLE_CELL);
                if (section == NULL || row == NULL || cell == NULL) {
                    free(section);
                    free(row);
                    free(cell);
                    q_box_append_child(table_box, child);
                    anon_section = NULL;
                    anon_row = NULL;
                    break;
                }
                q_box_append_child(table_box, section);
                q_box_append_child(section, row);
                q_box_append_child(row, cell);
                q_box_append_child(cell, child);
                anon_section = NULL;
                anon_row = NULL;
                break;
            }
        }

        child = next;
    }
}

void q_table_fixup_anonymous(q_box_t *root)
{
    q_box_t *child;

    if (root == NULL) {
        return;
    }

    if (root->type == Q_BOX_TABLE) {
        q_table_fixup_table_box(root);
    }

    for (child = root->first_child; child != NULL; child = child->next_sibling) {
        q_table_fixup_anonymous(child);
    }
}

/* ── Includes for DOM access in measure/position ── */
#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/character_data.h"

#include <ctype.h>
#include <string.h>

/* Shared constants */
#define Q_LAYOUT_WORD_SPACING  4.0f
#define Q_TABLE_MAX_COLS       128
#define Q_TABLE_MAX_ROWS       1024
#define Q_TABLE_CELL_PAD       8.0f   /* synthetic cell padding (left+right) */
#define Q_TABLE_DEFAULT_FONT_SIZE   16.0f
#define Q_TABLE_DEFAULT_FONT_WEIGHT 400
#define Q_TABLE_MIN_ROW_HEIGHT      20.0f

/* ── q_table_free ─────────────────────────────────────────────────────────── */

void q_table_free(q_table_t *t)
{
    if (t == NULL) {
        return;
    }
    free(t->cols);
    free(t->rows);
    free(t->spans);
    free(t);
}

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Parse a positive integer from a lxb_char_t span, returning def on failure. */
static int q_parse_span_attr(const lxb_char_t *s, size_t len, int def)
{
    int val = 0;
    size_t i;

    if (s == NULL || len == 0) {
        return def;
    }
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char) s[i];
        if (ch < '0' || ch > '9') {
            break;
        }
        val = val * 10 + (int) (ch - '0');
    }
    return (val > 0) ? val : def;
}

/* Retrieve colspan and rowspan from a TABLE_CELL box's DOM node. */
static void q_cell_get_span(q_box_t *cell, int *out_rowspan, int *out_colspan)
{
    size_t len;
    const lxb_char_t *val;

    *out_rowspan = 1;
    *out_colspan = 1;

    if (cell->dom_node == NULL ||
        cell->dom_node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return;
    }

    val = lxb_dom_element_get_attribute(
              lxb_dom_interface_element(cell->dom_node),
              (const lxb_char_t *) "colspan", 7, &len);
    if (val != NULL) {
        *out_colspan = q_parse_span_attr(val, len, 1);
    }

    val = lxb_dom_element_get_attribute(
              lxb_dom_interface_element(cell->dom_node),
              (const lxb_char_t *) "rowspan", 7, &len);
    if (val != NULL) {
        *out_rowspan = q_parse_span_attr(val, len, 1);
    }

    if (*out_colspan < 1) *out_colspan = 1;
    if (*out_rowspan < 1) *out_rowspan = 1;
}

/*
 * q_dom_text_natural_width — estimate the max-content width of a DOM subtree
 * by measuring all text words at infinite line width (no wrapping).
 * Uses a single font cache allocation for all words in the subtree.
 */
static float q_dom_text_natural_width(lxb_dom_node_t *root)
{
    lxb_dom_node_t *cur;
    q_font_cache_t *cache;
    q_font_t *font;
    float total_w = 0.0f;
    int word_count = 0;

    if (root == NULL) {
        return 0.0f;
    }

    cache = q_font_cache_create();
    font  = (cache != NULL) ? q_font_match(cache, "sans-serif",
                                            Q_TABLE_DEFAULT_FONT_SIZE,
                                            Q_TABLE_DEFAULT_FONT_WEIGHT) : NULL;

    /* Depth-first traversal of DOM subtree */
    cur = root;
    while (cur != NULL) {
        if (cur->type == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_character_data_t *cd = (lxb_dom_character_data_t *) cur;
            const char *text     = (const char *) cd->data.data;
            size_t      text_len = cd->data.length;
            size_t i = 0;

            while (i < text_len) {
                size_t word_start, word_len;
                float  word_w;

                while (i < text_len && isspace((unsigned char) text[i])) {
                    i++;
                }
                if (i >= text_len) {
                    break;
                }
                word_start = i;
                while (i < text_len && !isspace((unsigned char) text[i])) {
                    i++;
                }
                word_len = i - word_start;

                if (font != NULL) {
                    word_w = q_font_measure(font, text + word_start, word_len);
                    if (word_w <= 0.0f) {
                        word_w = (float) word_len * 9.6f;
                    }
                } else {
                    word_w = (float) word_len * 9.6f;
                }

                if (word_count > 0) {
                    total_w += Q_LAYOUT_WORD_SPACING;
                }
                total_w += word_w;
                word_count++;
            }
        }

        /* Depth-first: descend if possible */
        if (cur->first_child != NULL) {
            cur = cur->first_child;
        } else {
            while (cur != NULL && cur != root) {
                if (cur->next != NULL) {
                    cur = cur->next;
                    break;
                }
                cur = cur->parent;
            }
            if (cur == root) {
                break;
            }
        }
    }

    if (cache != NULL) {
        q_font_cache_destroy(cache);
    }

    return total_w;
}

/* ── q_table_measure ──────────────────────────────────────────────────────── */

/*
 * Measure a TABLE box:
 *  1. Count rows and estimate column count.
 *  2. Build occupancy grid, record span info.
 *  3. Compute column natural widths (DOM text measurement, no box layout).
 *  4. Derive final column widths (scale to table style_width or containing_w).
 *  5. Measure each cell once with its final column width via q_layout_measure.
 *  6. Compute row heights; handle rowspan > 1 via deficit distribution.
 *  7. Store results in table_box->table (q_table_t).
 */
void q_table_measure(q_box_t *table_box, float containing_w)
{
    q_box_t       *section, *row_box, *cell;
    q_table_t     *t;
    q_table_span_t *span;
    uint8_t        *grid = NULL;
    int             r, c, dr, dc;
    int             ncols, nrows, span_alloc;
    float           table_w;
    const float     min_col_w = 16.0f;

    if (table_box == NULL) {
        return;
    }

    /* Free any previous layout data */
    if (table_box->table != NULL) {
        q_table_free(table_box->table);
        table_box->table = NULL;
    }

    /* ── Pass 1: structural count ─────────────────────────────────────────── */
    nrows      = 0;
    ncols      = 0;
    span_alloc = 0;

    for (section = table_box->first_child;
         section != NULL;
         section = section->next_sibling) {
        if (section->type != Q_BOX_TABLE_SECTION) {
            continue;
        }
        for (row_box = section->first_child;
             row_box != NULL;
             row_box = row_box->next_sibling) {
            int row_cols = 0;

            if (row_box->type != Q_BOX_TABLE_ROW) {
                continue;
            }
            for (cell = row_box->first_child;
                 cell != NULL;
                 cell = cell->next_sibling) {
                int rs, cs;
                if (cell->type != Q_BOX_TABLE_CELL) {
                    continue;
                }
                q_cell_get_span(cell, &rs, &cs);
                row_cols += cs;
                span_alloc++;
            }
            if (row_cols > ncols) {
                ncols = row_cols;
            }
            nrows++;
        }
    }

    if (nrows == 0 || ncols == 0) {
        table_box->width  = 0.0f;
        table_box->height = 0.0f;
        return;
    }
    if (ncols > Q_TABLE_MAX_COLS) ncols = Q_TABLE_MAX_COLS;
    if (nrows > Q_TABLE_MAX_ROWS) nrows = Q_TABLE_MAX_ROWS;

    /* ── Allocate table data ─────────────────────────────────────────────── */
    t = (q_table_t *) calloc(1, sizeof(*t));
    if (t == NULL) {
        return;
    }
    t->col_count = ncols;
    t->row_count = nrows;
    t->border_collapse = table_box->table_border_collapse;
    t->cols  = (q_table_col_t  *) calloc((size_t) ncols, sizeof(*t->cols));
    t->rows  = (q_table_row_t  *) calloc((size_t) nrows, sizeof(*t->rows));
    t->spans = (q_table_span_t *) calloc(
        (size_t) (span_alloc > 0 ? span_alloc : 1), sizeof(*t->spans));
    grid = (uint8_t *) calloc(
        (size_t) nrows * (size_t) ncols, sizeof(uint8_t));

    if (t->cols == NULL || t->rows == NULL || t->spans == NULL || grid == NULL) {
        free(grid);
        q_table_free(t);
        return;
    }

    /* ── Pass 2: occupancy grid + natural widths ─────────────────────────── */
    r = 0;
    t->span_count = 0;

    for (section = table_box->first_child;
         section != NULL && r < nrows;
         section = section->next_sibling) {
        if (section->type != Q_BOX_TABLE_SECTION) {
            continue;
        }
        for (row_box = section->first_child;
             row_box != NULL && r < nrows;
             row_box = row_box->next_sibling) {
            if (row_box->type != Q_BOX_TABLE_ROW) {
                continue;
            }
            t->rows[r].box = row_box;

            c = 0;
            for (cell = row_box->first_child;
                 cell != NULL;
                 cell = cell->next_sibling) {
                int rs, cs, dc2;
                float nat_w, per_col;

                if (cell->type != Q_BOX_TABLE_CELL) {
                    continue;
                }
                q_cell_get_span(cell, &rs, &cs);

                /* Advance past occupied columns */
                while (c < ncols && grid[r * ncols + c]) {
                    c++;
                }
                if (c >= ncols) {
                    break;
                }

                /* Clamp span to grid bounds */
                if (c + cs > ncols) cs = ncols - c;
                if (r + rs > nrows) rs = nrows - r;

                /* Record span */
                if (t->span_count < span_alloc) {
                    span               = &t->spans[t->span_count++];
                    span->row          = r;
                    span->col          = c;
                    span->rowspan      = rs;
                    span->colspan      = cs;
                    span->cell_box     = cell;
                }

                /* Mark occupancy */
                for (dr = 0; dr < rs; dr++) {
                    for (dc = 0; dc < cs; dc++) {
                        grid[(r + dr) * ncols + (c + dc)] = 1;
                    }
                }

                /* Natural width estimate from DOM text */
                nat_w = q_dom_text_natural_width(cell->dom_node)
                        + Q_TABLE_CELL_PAD;
                if (nat_w < min_col_w) {
                    nat_w = min_col_w;
                }

                if (cs == 1) {
                    if (nat_w > t->cols[c].max_width) {
                        t->cols[c].max_width = nat_w;
                    }
                } else {
                    per_col = nat_w / (float) cs;
                    for (dc2 = 0; dc2 < cs; dc2++) {
                        if (per_col > t->cols[c + dc2].max_width) {
                            t->cols[c + dc2].max_width = per_col;
                        }
                    }
                }

                c += cs;
            }
            r++;
        }
    }

    free(grid);
    grid = NULL;

    /* ── Pass 3: final column widths ──────────────────────────────────────── */
    if (!isnan(table_box->style_width) && table_box->style_width > 0.0f) {
        table_w = table_box->style_width;
    } else {
        table_w = (containing_w > 0.0f) ? containing_w : 0.0f;
    }

    {
        float total_natural = 0.0f;
        int ci;

        for (ci = 0; ci < ncols; ci++) {
            if (t->cols[ci].max_width < min_col_w) {
                t->cols[ci].max_width = min_col_w;
            }
            total_natural += t->cols[ci].max_width;
        }

        if (total_natural > 0.0f && table_w > 0.0f) {
            float scale = table_w / total_natural;
            for (ci = 0; ci < ncols; ci++) {
                t->cols[ci].final_width = t->cols[ci].max_width * scale;
                if (t->cols[ci].final_width < min_col_w) {
                    t->cols[ci].final_width = min_col_w;
                }
            }
        } else {
            /* Fallback: equal distribution */
            float equal_w = (ncols > 0 && table_w > 0.0f)
                            ? (table_w / (float) ncols)
                            : min_col_w;
            for (ci = 0; ci < ncols; ci++) {
                t->cols[ci].final_width = equal_w;
            }
        }
    }

    /* ── Pass 4: measure cells, compute row heights (rowspan == 1) ─────────── */
    {
        int si;
        for (si = 0; si < t->span_count; si++) {
            float cell_w = 0.0f;
            int dc3;

            span = &t->spans[si];
            cell = span->cell_box;

            for (dc3 = 0; dc3 < span->colspan; dc3++) {
                cell_w += t->cols[span->col + dc3].final_width;
            }
            if (cell_w < 1.0f) cell_w = 1.0f;

            q_layout_measure(cell, cell_w, 0.0f);

            if (span->rowspan == 1) {
                if (cell->height > t->rows[span->row].height) {
                    t->rows[span->row].height = cell->height;
                }
            }
        }
    }

    /* ── Pass 5: rowspan > 1 height distribution ──────────────────────────── */
    {
        int si;
        for (si = 0; si < t->span_count; si++) {
            float spanned_h = 0.0f;
            int ri2;

            span = &t->spans[si];
            if (span->rowspan <= 1) {
                continue;
            }
            cell = span->cell_box;

            for (ri2 = 0; ri2 < span->rowspan; ri2++) {
                spanned_h += t->rows[span->row + ri2].height;
            }
            if (cell->height > spanned_h) {
                float extra = (cell->height - spanned_h) / (float) span->rowspan;
                for (ri2 = 0; ri2 < span->rowspan; ri2++) {
                    t->rows[span->row + ri2].height += extra;
                }
            }
        }
    }

    /* Enforce minimum row height */
    {
        int ri;
        for (ri = 0; ri < nrows; ri++) {
            if (t->rows[ri].height < 1.0f) {
                t->rows[ri].height = Q_TABLE_MIN_ROW_HEIGHT;
            }
        }
    }

    /* ── Compute total table dimensions ──────────────────────────────────── */
    {
        float total_h = 0.0f;
        int ri;
        for (ri = 0; ri < nrows; ri++) {
            total_h += t->rows[ri].height;
        }
        table_box->width  = table_w;
        table_box->height = total_h;
    }

    table_box->table = t;
}

/* ── q_table_position ─────────────────────────────────────────────────────── */

/*
 * Assign absolute x/y coordinates to the table box, its sections, rows, and
 * all cells (by delegating to q_layout_position for each cell).
 */
void q_table_position(q_box_t *table_box, float origin_x, float origin_y)
{
    q_table_t     *t;
    q_box_t       *section, *row_box;
    float         *row_y = NULL;
    float         *col_x = NULL;
    int            r, c, si;
    int            nrows, ncols;

    if (table_box == NULL || table_box->table == NULL) {
        return;
    }

    table_box->x = origin_x;
    table_box->y = origin_y;

    t     = table_box->table;
    nrows = t->row_count;
    ncols = t->col_count;

    /* Cumulative row/column offset arrays */
    row_y = (float *) calloc((size_t) (nrows + 1), sizeof(float));
    col_x = (float *) calloc((size_t) (ncols + 1), sizeof(float));
    if (row_y == NULL || col_x == NULL) {
        free(row_y);
        free(col_x);
        return;
    }

    row_y[0] = origin_y;
    for (r = 0; r < nrows; r++) {
        row_y[r + 1] = row_y[r] + t->rows[r].height;
    }
    col_x[0] = origin_x;
    for (c = 0; c < ncols; c++) {
        col_x[c + 1] = col_x[c] + t->cols[c].final_width;
    }

    /* Position each cell using pre-computed spans */
    for (si = 0; si < t->span_count; si++) {
        q_table_span_t *span = &t->spans[si];
        q_box_t        *cell = span->cell_box;
        float           cell_x, cell_y, cell_w, cell_h;
        int             dc, dr;

        cell_x = col_x[span->col];
        cell_y = row_y[span->row];

        cell_w = 0.0f;
        for (dc = 0; dc < span->colspan; dc++) {
            cell_w += t->cols[span->col + dc].final_width;
        }
        cell_h = 0.0f;
        for (dr = 0; dr < span->rowspan; dr++) {
            cell_h += t->rows[span->row + dr].height;
        }

        cell->width  = cell_w;
        cell->height = cell_h;
        q_layout_position(cell, cell_x, cell_y);
    }

    /* Position section and row boxes */
    r = 0;
    for (section = table_box->first_child;
         section != NULL;
         section = section->next_sibling) {
        int section_row_start = r;

        if (section->type != Q_BOX_TABLE_SECTION) {
            continue;
        }
        for (row_box = section->first_child;
             row_box != NULL && r < nrows;
             row_box = row_box->next_sibling) {
            if (row_box->type != Q_BOX_TABLE_ROW) {
                continue;
            }
            row_box->x      = origin_x;
            row_box->y      = row_y[r];
            row_box->width  = table_box->width;
            row_box->height = t->rows[r].height;
            t->rows[r].box  = row_box;
            r++;
        }

        {
            float sec_y_top    = (section_row_start <= nrows)
                                 ? row_y[section_row_start] : origin_y;
            float sec_y_bottom = row_y[r];

            section->x      = origin_x;
            section->y      = sec_y_top;
            section->width  = table_box->width;
            section->height = sec_y_bottom - sec_y_top;
        }
    }

    free(row_y);
    free(col_x);
}
