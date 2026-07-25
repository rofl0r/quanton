#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/character_data.h"
#include "lexbor/html/interfaces/document.h"
#include "lexbor/tag/const.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define Q_DEFAULT_BACKGROUND 0xF2F2F2FFu
#define Q_DEFAULT_BORDER 0x303030FFu
#define Q_DEFAULT_TEXT_COLOR 0x000000FFu
#define Q_DEFAULT_BORDER_WIDTH 0.0f
#define Q_CSS_INT_PARSE_BUF_SIZE 64u

/* ─── Inline CSS style attribute parser ─────────────────────────────────── */

/* Case-insensitive compare of a lxb_char_t span against a lowercase literal.
 * The span is [s, s+slen); the literal is a NUL-terminated lowercase string.
 * Returns 1 if equal. */
static int css_name_eq(const lxb_char_t *s, size_t slen, const char *name)
{
    size_t i;
    for (i = 0; i < slen; ++i) {
        if (name[i] == '\0') {
            return 0; /* name is shorter */
        }
        if (tolower((unsigned char) s[i]) != (unsigned char) name[i]) {
            return 0;
        }
    }
    return name[slen] == '\0';
}

/* Case-insensitive compare of the value region [val, val+vlen) (with leading
 * whitespace stripped) against a lowercase keyword.  Trailing whitespace in
 * the value is also ignored. */
static int css_value_is(const lxb_char_t *val, size_t vlen, const char *kw)
{
    size_t kwlen = strlen(kw);
    size_t i = 0;

    /* skip leading space */
    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    if (vlen - i < kwlen) {
        return 0;
    }
    if (!css_name_eq(val + i, kwlen, kw)) {
        return 0;
    }
    i += kwlen;
    /* only trailing space is allowed after the keyword */
    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    return i >= vlen;
}

/* Parse a CSS length value (e.g. "10px", "  20 ", "0") and return it as a
 * float in pixels.  Units other than px are accepted but treated as px.
 * Returns 0.0f on parse failure. */
/* Parse a CSS length value (number + optional unit suffix).
 * If is_pct is non-NULL it is set to 1 when the value ends in '%', else 0.
 * Pass NULL for is_pct when percentage detection is not needed. */
static float css_parse_length_pct(const lxb_char_t *val, size_t vlen, int *is_pct)
{
    int _dummy;
    char buf[32];
    size_t i = 0;
    size_t n;
    char *endp = NULL;
    float result;

    if (is_pct == NULL) is_pct = &_dummy;
    *is_pct = 0;
    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    n = vlen - i;
    if (n == 0) {
        return 0.0f;
    }
    if (n > sizeof(buf) - 1) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, val + i, n);
    buf[n] = '\0';
    result = strtof(buf, &endp);
    if (endp != NULL) {
        while (*endp == ' ' || *endp == '\t') ++endp;
        if (*endp == '%') {
            *is_pct = 1;
        }
    }
    return result;
}

static int css_parse_int(const lxb_char_t *val, size_t vlen, int *out)
{
    char buf[Q_CSS_INT_PARSE_BUF_SIZE];
    size_t i = 0;
    size_t n;
    char *endp;
    long z;

    if (out == NULL) {
        return 0;
    }

    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    n = vlen - i;
    if (n == 0 || n > sizeof(buf) - 1u) {
        return 0;
    }

    memcpy(buf, val + i, n);
    buf[n] = '\0';

    z = strtol(buf, &endp, 10);
    if (endp == buf) {
        return 0;
    }
    while (*endp != '\0') {
        if (!isspace((unsigned char) *endp)) {
            return 0;
        }
        ++endp;
    }
    if (z < INT_MIN || z > INT_MAX) {
        return 0;
    }

    *out = (int) z;
    return 1;
}

static int css_parse_hex_nibble(lxb_char_t ch, uint8_t *out)
{
    if (ch >= '0' && ch <= '9') {
        *out = (uint8_t) (ch - '0');
        return 1;
    }
    if (ch >= 'a' && ch <= 'f') {
        *out = (uint8_t) (10 + ch - 'a');
        return 1;
    }
    if (ch >= 'A' && ch <= 'F') {
        *out = (uint8_t) (10 + ch - 'A');
        return 1;
    }
    return 0;
}

static int css_parse_color(const lxb_char_t *val, size_t vlen, uint32_t *out)
{
    size_t start = 0;
    size_t end = vlen;

    if (out == NULL) {
        return 0;
    }

    while (start < end && isspace((unsigned char) val[start])) {
        ++start;
    }
    while (end > start && isspace((unsigned char) val[end - 1])) {
        --end;
    }
    if (start >= end) {
        return 0;
    }

    if (css_name_eq(val + start, end - start, "transparent")) {
        *out = 0x00000000u;
        return 1;
    }

    if (val[start] == '#') {
        uint8_t r0;
        uint8_t g0;
        uint8_t b0;
        size_t len = end - start;

        if (len == 4u &&
            css_parse_hex_nibble(val[start + 1], &r0) &&
            css_parse_hex_nibble(val[start + 2], &g0) &&
            css_parse_hex_nibble(val[start + 3], &b0))
        {
            uint8_t r = (uint8_t) ((r0 << 4) | r0);
            uint8_t g = (uint8_t) ((g0 << 4) | g0);
            uint8_t b = (uint8_t) ((b0 << 4) | b0);
            *out = ((uint32_t) r << 24) |
                   ((uint32_t) g << 16) |
                   ((uint32_t) b << 8)  |
                   0xFFu;
            return 1;
        }

        if (len == 7u) {
            uint8_t rh;
            uint8_t rl;
            uint8_t gh;
            uint8_t gl;
            uint8_t bh;
            uint8_t bl;

            if (css_parse_hex_nibble(val[start + 1], &rh) &&
                css_parse_hex_nibble(val[start + 2], &rl) &&
                css_parse_hex_nibble(val[start + 3], &gh) &&
                css_parse_hex_nibble(val[start + 4], &gl) &&
                css_parse_hex_nibble(val[start + 5], &bh) &&
                css_parse_hex_nibble(val[start + 6], &bl))
            {
                uint8_t r = (uint8_t) ((rh << 4) | rl);
                uint8_t g = (uint8_t) ((gh << 4) | gl);
                uint8_t b = (uint8_t) ((bh << 4) | bl);
                *out = ((uint32_t) r << 24) |
                       ((uint32_t) g << 16) |
                       ((uint32_t) b << 8)  |
                       0xFFu;
                return 1;
            }
        }
    }

    return 0;
}

static size_t css_trim_start(const lxb_char_t *s, size_t len)
{
    size_t i = 0;
    while (i < len && isspace((unsigned char) s[i])) {
        ++i;
    }
    return i;
}

static size_t css_trim_end(const lxb_char_t *s, size_t len)
{
    while (len > 0 && isspace((unsigned char) s[len - 1])) {
        --len;
    }
    return len;
}

static size_t css_split_tokens(const lxb_char_t *val, size_t vlen,
                               const lxb_char_t **tokens, size_t *token_lens, size_t max_tokens)
{
    size_t i = 0;
    size_t count = 0;

    while (i < vlen && count < max_tokens) {
        size_t start;
        size_t end;
        while (i < vlen && isspace((unsigned char) val[i])) {
            ++i;
        }
        if (i >= vlen) {
            break;
        }
        start = i;
        while (i < vlen && !isspace((unsigned char) val[i])) {
            ++i;
        }
        end = i;
        tokens[count] = val + start;
        token_lens[count] = end - start;
        ++count;
    }

    return count;
}

static int css_parse_margin_token(const lxb_char_t *val, size_t len, float *out_value, int *out_auto)
{
    if (out_value == NULL || out_auto == NULL) {
        return 0;
    }

    if (css_value_is(val, len, "auto")) {
        *out_value = 0.0f;
        *out_auto = 1;
        return 1;
    }

    *out_value = css_parse_length_pct(val, len, NULL);
    *out_auto = 0;
    return 1;
}

static void css_apply_border_radius_shorthand(const lxb_char_t *val, size_t vlen, q_box_t *box)
{
    const lxb_char_t *tokens[4];
    size_t token_lens[4];
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    size_t count;
    size_t i;

    if (box == NULL) {
        return;
    }

    count = css_split_tokens(val, vlen, tokens, token_lens, 4u);
    if (count == 0u) {
        return;
    }
    for (i = 0; i < count; ++i) {
        values[i] = css_parse_length_pct(tokens[i], token_lens[i], NULL);
        if (values[i] < 0.0f) {
            values[i] = 0.0f;
        }
    }

    if (count == 1u) {
        box->border_radius[0] = values[0];
        box->border_radius[1] = values[0];
        box->border_radius[2] = values[0];
        box->border_radius[3] = values[0];
    } else if (count == 2u) {
        box->border_radius[0] = values[0];
        box->border_radius[1] = values[1];
        box->border_radius[2] = values[0];
        box->border_radius[3] = values[1];
    } else if (count == 3u) {
        box->border_radius[0] = values[0];
        box->border_radius[1] = values[1];
        box->border_radius[2] = values[2];
        box->border_radius[3] = values[1];
    } else {
        box->border_radius[0] = values[0];
        box->border_radius[1] = values[1];
        box->border_radius[2] = values[2];
        box->border_radius[3] = values[3];
    }
}

static int css_extract_url(const lxb_char_t *val, size_t vlen, const lxb_char_t **out_url, size_t *out_len)
{
    size_t i = 0;
    size_t start;
    size_t end;
    lxb_char_t quote = 0;

    if (out_url == NULL || out_len == NULL) {
        return 0;
    }

    while (i + 3u < vlen) {
        if (tolower((unsigned char) val[i]) == 'u'
            && tolower((unsigned char) val[i + 1u]) == 'r'
            && tolower((unsigned char) val[i + 2u]) == 'l'
            && val[i + 3u] == '(')
        {
            i += 4u;
            break;
        }
        ++i;
    }
    if (i + 3u >= vlen) {
        return 0;
    }

    while (i < vlen && isspace((unsigned char) val[i])) {
        ++i;
    }
    if (i >= vlen) {
        return 0;
    }

    if (val[i] == '\'' || val[i] == '"') {
        quote = val[i];
        ++i;
    }
    start = i;
    if (quote != 0) {
        while (i < vlen && val[i] != quote) {
            ++i;
        }
        end = i;
    } else {
        while (i < vlen && val[i] != ')') {
            ++i;
        }
        end = css_trim_end(val + start, i - start) + start;
    }

    if (end <= start) {
        return 0;
    }
    *out_url = val + start;
    *out_len = end - start;
    return 1;
}

static void q_box_load_background_image(q_document_t *doc, q_box_t *box,
                                        const lxb_char_t *val, size_t vlen)
{
    const lxb_char_t *url_span;
    size_t url_len;
    char *url = NULL;
    char *resolved = NULL;

    if (box == NULL || val == NULL || vlen == 0u) {
        return;
    }

    q_image_release(box->background_image);
    box->background_image = NULL;

    if (css_value_is(val, vlen, "none")) {
        return;
    }
    if (!css_extract_url(val, vlen, &url_span, &url_len) || url_len == 0u) {
        return;
    }

    url = (char *) malloc(url_len + 1u);
    if (url == NULL) {
        return;
    }
    memcpy(url, url_span, url_len);
    url[url_len] = '\0';

    resolved = q_url_resolve(q_document_base_url(doc), url);
    free(url);
    if (resolved == NULL) {
        return;
    }

    box->background_image = q_image_load_url(resolved);
    free(resolved);
}

/* Parse relevant CSS properties from a style attribute string and apply them
 * to *box.  Handles: display, position, z-index, top/right/bottom/left, width, height. */
static void parse_style_attribute(const lxb_char_t *style, size_t style_len,
                                  q_box_t *box, q_document_t *doc)
{
    size_t i = 0;

    while (i < style_len) {
        size_t prop_start;
        size_t prop_end;
        size_t val_start;
        size_t val_end;
        size_t prop_len;
        size_t val_len;
        const lxb_char_t *prop;
        const lxb_char_t *val;

        /* skip leading whitespace */
        while (i < style_len && isspace((unsigned char) style[i])) {
            ++i;
        }
        if (i >= style_len) {
            break;
        }

        prop_start = i;
        /* scan property name up to ':' or ';' */
        while (i < style_len && style[i] != ':' && style[i] != ';') {
            ++i;
        }
        if (i >= style_len || style[i] != ':') {
            /* no colon — skip to next ';' */
            while (i < style_len && style[i] != ';') {
                ++i;
            }
            if (i < style_len) {
                ++i; /* skip ';' */
            }
            continue;
        }
        prop_end = i;
        ++i; /* skip ':' */

        val_start = i;
        while (i < style_len && style[i] != ';') {
            ++i;
        }
        val_end = i;
        if (i < style_len) {
            ++i; /* skip ';' */
        }

        /* trim trailing whitespace from property name */
        while (prop_end > prop_start
               && isspace((unsigned char) style[prop_end - 1])) {
            --prop_end;
        }
        prop_len = prop_end - prop_start;
        val_len  = val_end  - val_start;
        prop = style + prop_start;
        val  = style + val_start;

        if (css_name_eq(prop, prop_len, "display")) {
            if (css_value_is(val, val_len, "flex")) {
                box->is_flex_container = 1;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_BLOCK;
                }
            } else if (css_value_is(val, val_len, "inline-block")) {
                box->is_flex_container = 0;
                box->is_inline_block = 1;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_BLOCK;
                }
            } else if (css_value_is(val, val_len, "list-item")) {
                box->is_flex_container = 0;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_BLOCK;
                }
                if (box->list_style_type == Q_LIST_STYLE_NONE) {
                    box->list_style_type = Q_LIST_STYLE_DISC;
                }
            } else if (css_value_is(val, val_len, "table")
                       || css_value_is(val, val_len, "inline-table")) {
                box->is_flex_container = 0;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_TABLE;
                }
            } else if (css_value_is(val, val_len, "table-row-group")
                       || css_value_is(val, val_len, "table-header-group")
                       || css_value_is(val, val_len, "table-footer-group")) {
                box->is_flex_container = 0;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_TABLE_SECTION;
                }
            } else if (css_value_is(val, val_len, "table-row")) {
                box->is_flex_container = 0;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_TABLE_ROW;
                }
            } else if (css_value_is(val, val_len, "table-cell")) {
                box->is_flex_container = 0;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_TABLE_CELL;
                }
            } else if (css_value_is(val, val_len, "table-caption")) {
                box->is_flex_container = 0;
                box->is_inline_block = 0;
                if (box->type != Q_BOX_IMAGE) {
                    box->type = Q_BOX_TABLE_CAPTION;
                }
            }
        } else if (css_name_eq(prop, prop_len, "position")) {
            if (css_value_is(val, val_len, "absolute")) {
                box->position = Q_POSITION_ABSOLUTE;
            } else if (css_value_is(val, val_len, "fixed")) {
                box->position = Q_POSITION_FIXED;
            } else if (css_value_is(val, val_len, "relative")) {
                box->position = Q_POSITION_RELATIVE;
            }
        } else if (css_name_eq(prop, prop_len, "z-index")) {
            int z;
            if (css_parse_int(val, val_len, &z)) {
                box->has_z_index = 1;
                box->z_index = z;
            }
        } else if (css_name_eq(prop, prop_len, "top")) {
            box->style_top = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "right")) {
            box->style_right = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "bottom")) {
            box->style_bottom = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "left")) {
            box->style_left = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "width")) {
            {
                int is_pct = 0;
                float v = css_parse_length_pct(val, val_len, &is_pct);
                if (is_pct) {
                    box->style_width_pct = v;
                    box->style_width = (float) NAN;
                } else {
                    box->style_width = v;
                    box->style_width_pct = (float) NAN;
                }
            }
        } else if (css_name_eq(prop, prop_len, "height")) {
            box->style_height = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "min-width")) {
            box->style_min_width = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "max-width")) {
            box->style_max_width = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "min-height")) {
            box->style_min_height = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "max-height")) {
            box->style_max_height = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "margin")) {
            const lxb_char_t *tokens[4];
            size_t token_lens[4];
            size_t count = css_split_tokens(val, val_len, tokens, token_lens, 4u);
            float m[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int a[4] = {0, 0, 0, 0};
            size_t i;

            if (count == 0u) {
                continue;
            }

            for (i = 0; i < count; ++i) {
                css_parse_margin_token(tokens[i], token_lens[i], &m[i], &a[i]);
            }

            if (count == 1u) {
                box->margin_top = m[0];
                box->margin_right = m[0];
                box->margin_bottom = m[0];
                box->margin_left = m[0];
                box->margin_right_auto = a[0];
                box->margin_left_auto = a[0];
            } else if (count == 2u) {
                box->margin_top = m[0];
                box->margin_bottom = m[0];
                box->margin_right = m[1];
                box->margin_left = m[1];
                box->margin_right_auto = a[1];
                box->margin_left_auto = a[1];
            } else if (count == 3u) {
                box->margin_top = m[0];
                box->margin_right = m[1];
                box->margin_left = m[1];
                box->margin_bottom = m[2];
                box->margin_right_auto = a[1];
                box->margin_left_auto = a[1];
            } else {
                box->margin_top = m[0];
                box->margin_right = m[1];
                box->margin_bottom = m[2];
                box->margin_left = m[3];
                box->margin_right_auto = a[1];
                box->margin_left_auto = a[3];
            }
        } else if (css_name_eq(prop, prop_len, "margin-top")) {
            box->margin_top    = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "margin-right")) {
            css_parse_margin_token(val, val_len, &box->margin_right, &box->margin_right_auto);
        } else if (css_name_eq(prop, prop_len, "margin-bottom")) {
            box->margin_bottom = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "margin-left")) {
            css_parse_margin_token(val, val_len, &box->margin_left, &box->margin_left_auto);
        } else if (css_name_eq(prop, prop_len, "padding")) {
            float v = css_parse_length_pct(val, val_len, NULL);
            box->padding_top    = v;
            box->padding_right  = v;
            box->padding_bottom = v;
            box->padding_left   = v;
        } else if (css_name_eq(prop, prop_len, "padding-top")) {
            box->padding_top    = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "padding-right")) {
            box->padding_right  = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "padding-bottom")) {
            box->padding_bottom = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "padding-left")) {
            box->padding_left   = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "border-width")) {
            float v = css_parse_length_pct(val, val_len, NULL);
            box->border_width[0] = v;
            box->border_width[1] = v;
            box->border_width[2] = v;
            box->border_width[3] = v;
        } else if (css_name_eq(prop, prop_len, "border-top-width")) {
            box->border_width[0] = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "border-right-width")) {
            box->border_width[1] = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "border-bottom-width")) {
            box->border_width[2] = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "border-left-width")) {
            box->border_width[3] = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "border-color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->border_color[0] = color;
                box->border_color[1] = color;
                box->border_color[2] = color;
                box->border_color[3] = color;
            }
        } else if (css_name_eq(prop, prop_len, "border-top-color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->border_color[0] = color;
            }
        } else if (css_name_eq(prop, prop_len, "border-right-color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->border_color[1] = color;
            }
        } else if (css_name_eq(prop, prop_len, "border-bottom-color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->border_color[2] = color;
            }
        } else if (css_name_eq(prop, prop_len, "border-left-color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->border_color[3] = color;
            }
        } else if (css_name_eq(prop, prop_len, "border-spacing")) {
            box->table_border_spacing = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "border")) {
            /* Simplified border shorthand: parse space-separated tokens for
             * a width (number+unit), style keyword (ignored), and color. */
            {
                const lxb_char_t *p = val;
                size_t rem = val_len;
                float bw = -1.0f;
                uint32_t bcolor = 0;
                int got_color = 0;

                while (rem > 0) {
                    /* skip whitespace */
                    while (rem > 0 && isspace((unsigned char) *p)) { ++p; --rem; }
                    if (rem == 0) break;
                    /* find end of token */
                    {
                        size_t tlen = 0;
                        while (tlen < rem && !isspace((unsigned char) p[tlen])) ++tlen;
                        /* try as color */
                        if (!got_color && css_parse_color(p, tlen, &bcolor)) {
                            got_color = 1;
                        } else if (bw < 0.0f) {
                            float v = strtof((const char *) p, NULL);
                            if (v >= 0.0f) bw = v;
                        }
                        p   += tlen;
                        rem -= tlen;
                    }
                }
                if (bw >= 0.0f) {
                    box->border_width[0] = bw;
                    box->border_width[1] = bw;
                    box->border_width[2] = bw;
                    box->border_width[3] = bw;
                }
                if (got_color) {
                    box->border_color[0] = bcolor;
                    box->border_color[1] = bcolor;
                    box->border_color[2] = bcolor;
                    box->border_color[3] = bcolor;
                }
            }
        } else if (css_name_eq(prop, prop_len, "background-color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->background_color = color;
            }
        } else if (css_name_eq(prop, prop_len, "font-size")) {
            box->font_size = css_parse_length_pct(val, val_len, NULL);
        } else if (css_name_eq(prop, prop_len, "font-weight")) {
            int fw = 0;
            if (css_value_is(val, val_len, "normal")) {
                fw = 400;
            } else if (css_value_is(val, val_len, "bold")) {
                fw = 700;
            } else if (css_parse_int(val, val_len, &fw)) {
                if (fw < 1) {
                    fw = 1;
                }
            }
            box->font_weight = fw;
        } else if (css_name_eq(prop, prop_len, "color")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->text_color = color;
                box->has_text_color = 1;
            }
        } else if (css_name_eq(prop, prop_len, "background")) {
            uint32_t color;
            if (css_parse_color(val, val_len, &color)) {
                box->background_color = color;
            }
            if (doc != NULL) {
                q_box_load_background_image(doc, box, val, val_len);
            }
            if (css_value_is(val, val_len, "repeat-x")) {
                box->background_repeat = Q_BACKGROUND_REPEAT_REPEAT_X;
            } else if (css_value_is(val, val_len, "repeat-y")) {
                box->background_repeat = Q_BACKGROUND_REPEAT_REPEAT_Y;
            } else if (css_value_is(val, val_len, "no-repeat")) {
                box->background_repeat = Q_BACKGROUND_REPEAT_NO_REPEAT;
            } else {
                box->background_repeat = Q_BACKGROUND_REPEAT_REPEAT;
            }
        } else if (css_name_eq(prop, prop_len, "background-image")) {
            if (doc != NULL) {
                q_box_load_background_image(doc, box, val, val_len);
            }
        } else if (css_name_eq(prop, prop_len, "background-repeat")) {
            if (css_value_is(val, val_len, "repeat-x")) {
                box->background_repeat = Q_BACKGROUND_REPEAT_REPEAT_X;
            } else if (css_value_is(val, val_len, "repeat-y")) {
                box->background_repeat = Q_BACKGROUND_REPEAT_REPEAT_Y;
            } else if (css_value_is(val, val_len, "no-repeat")) {
                box->background_repeat = Q_BACKGROUND_REPEAT_NO_REPEAT;
            } else {
                box->background_repeat = Q_BACKGROUND_REPEAT_REPEAT;
            }
        } else if (css_name_eq(prop, prop_len, "list-style-type")) {
            if (css_value_is(val, val_len, "none")) {
                box->list_style_type = Q_LIST_STYLE_NONE;
            } else if (css_value_is(val, val_len, "decimal")) {
                box->list_style_type = Q_LIST_STYLE_DECIMAL;
            } else {
                box->list_style_type = Q_LIST_STYLE_DISC;
            }
        } else if (css_name_eq(prop, prop_len, "list-style")) {
            if (css_value_is(val, val_len, "none")) {
                box->list_style_type = Q_LIST_STYLE_NONE;
            } else if (css_value_is(val, val_len, "decimal")) {
                box->list_style_type = Q_LIST_STYLE_DECIMAL;
            } else if (css_value_is(val, val_len, "disc")) {
                box->list_style_type = Q_LIST_STYLE_DISC;
            }
        } else if (css_name_eq(prop, prop_len, "overflow-x")) {
            if (css_value_is(val, val_len, "hidden")) {
                box->overflow_x = Q_OVERFLOW_HIDDEN;
            } else if (css_value_is(val, val_len, "clip")) {
                box->overflow_x = Q_OVERFLOW_CLIP;
            } else if (css_value_is(val, val_len, "scroll")) {
                box->overflow_x = Q_OVERFLOW_SCROLL;
            } else if (css_value_is(val, val_len, "auto")) {
                box->overflow_x = Q_OVERFLOW_AUTO;
            } else {
                box->overflow_x = Q_OVERFLOW_VISIBLE;
            }
        } else if (css_name_eq(prop, prop_len, "overflow-y")) {
            if (css_value_is(val, val_len, "hidden")) {
                box->overflow_y = Q_OVERFLOW_HIDDEN;
            } else if (css_value_is(val, val_len, "clip")) {
                box->overflow_y = Q_OVERFLOW_CLIP;
            } else if (css_value_is(val, val_len, "scroll")) {
                box->overflow_y = Q_OVERFLOW_SCROLL;
            } else if (css_value_is(val, val_len, "auto")) {
                box->overflow_y = Q_OVERFLOW_AUTO;
            } else {
                box->overflow_y = Q_OVERFLOW_VISIBLE;
            }
        } else if (css_name_eq(prop, prop_len, "overflow")) {
            q_overflow_type_t ov = Q_OVERFLOW_VISIBLE;
            if (css_value_is(val, val_len, "hidden")) {
                ov = Q_OVERFLOW_HIDDEN;
            } else if (css_value_is(val, val_len, "clip")) {
                ov = Q_OVERFLOW_CLIP;
            } else if (css_value_is(val, val_len, "scroll")) {
                ov = Q_OVERFLOW_SCROLL;
            } else if (css_value_is(val, val_len, "auto")) {
                ov = Q_OVERFLOW_AUTO;
            }
            box->overflow_x = ov;
            box->overflow_y = ov;
        } else if (css_name_eq(prop, prop_len, "float")) {
            if (css_value_is(val, val_len, "left")) {
                box->float_type = Q_FLOAT_LEFT;
            } else if (css_value_is(val, val_len, "right")) {
                box->float_type = Q_FLOAT_RIGHT;
            } else {
                box->float_type = Q_FLOAT_NONE;
            }
        } else if (css_name_eq(prop, prop_len, "clear")) {
            if (css_value_is(val, val_len, "left")) {
                box->clear_type = Q_CLEAR_LEFT;
            } else if (css_value_is(val, val_len, "right")) {
                box->clear_type = Q_CLEAR_RIGHT;
            } else if (css_value_is(val, val_len, "both")) {
                box->clear_type = Q_CLEAR_BOTH;
            } else {
                box->clear_type = Q_CLEAR_NONE;
            }
        } else if (css_name_eq(prop, prop_len, "border-collapse")) {
            box->table_border_collapse = css_value_is(val, val_len, "collapse") ? 1 : 0;
            if (box->table_border_collapse) {
                /* collapse implies no gap between cells */
                box->table_border_spacing = 0.0f;
            }
        } else if (css_name_eq(prop, prop_len, "white-space")) {
            if (css_value_is(val, val_len, "pre")) {
                box->white_space = Q_WHITE_SPACE_PRE;
            } else if (css_value_is(val, val_len, "nowrap")) {
                box->white_space = Q_WHITE_SPACE_NOWRAP;
            } else {
                box->white_space = Q_WHITE_SPACE_NORMAL;
            }
        } else if (css_name_eq(prop, prop_len, "vertical-align")) {
            if (css_value_is(val, val_len, "top")) {
                box->vertical_align = Q_VERTICAL_ALIGN_TOP;
            } else if (css_value_is(val, val_len, "middle")) {
                box->vertical_align = Q_VERTICAL_ALIGN_MIDDLE;
            } else if (css_value_is(val, val_len, "bottom")) {
                box->vertical_align = Q_VERTICAL_ALIGN_BOTTOM;
            } else if (css_value_is(val, val_len, "sub")) {
                box->vertical_align = Q_VERTICAL_ALIGN_SUB;
            } else if (css_value_is(val, val_len, "super")) {
                box->vertical_align = Q_VERTICAL_ALIGN_SUPER;
            } else {
                box->vertical_align = Q_VERTICAL_ALIGN_BASELINE;
            }
        } else if (css_name_eq(prop, prop_len, "text-align")) {
            if (css_value_is(val, val_len, "center")) {
                box->text_align = Q_TEXT_ALIGN_CENTER;
            } else if (css_value_is(val, val_len, "right")) {
                box->text_align = Q_TEXT_ALIGN_RIGHT;
            } else {
                box->text_align = Q_TEXT_ALIGN_LEFT;
            }
        } else if (css_name_eq(prop, prop_len, "text-decoration")
                   || css_name_eq(prop, prop_len, "text-decoration-line")) {
            const lxb_char_t *tokens[8];
            size_t token_lens[8];
            size_t ti;
            size_t count;
            uint8_t deco = 0;
            size_t trim_start = css_trim_start(val, val_len);
            size_t trim_len = css_trim_end(val + trim_start, val_len - trim_start);

            count = css_split_tokens(val + trim_start, trim_len, tokens, token_lens, 8u);
            for (ti = 0; ti < count; ++ti) {
                if (css_name_eq(tokens[ti], token_lens[ti], "none")) {
                    deco = 0;
                    break;
                } else if (css_name_eq(tokens[ti], token_lens[ti], "underline")) {
                    deco |= Q_TEXT_DECORATION_UNDERLINE;
                } else if (css_name_eq(tokens[ti], token_lens[ti], "overline")) {
                    deco |= Q_TEXT_DECORATION_OVERLINE;
                } else if (css_name_eq(tokens[ti], token_lens[ti], "line-through")) {
                    deco |= Q_TEXT_DECORATION_LINE_THROUGH;
                }
            }
            box->text_decoration = deco;
        } else if (css_name_eq(prop, prop_len, "border-radius")) {
            css_apply_border_radius_shorthand(val, val_len, box);
        } else if (css_name_eq(prop, prop_len, "border-top-left-radius")) {
            float r = css_parse_length_pct(val, val_len, NULL);
            box->border_radius[0] = (r < 0.0f) ? 0.0f : r;
        } else if (css_name_eq(prop, prop_len, "border-top-right-radius")) {
            float r = css_parse_length_pct(val, val_len, NULL);
            box->border_radius[1] = (r < 0.0f) ? 0.0f : r;
        } else if (css_name_eq(prop, prop_len, "border-bottom-right-radius")) {
            float r = css_parse_length_pct(val, val_len, NULL);
            box->border_radius[2] = (r < 0.0f) ? 0.0f : r;
        } else if (css_name_eq(prop, prop_len, "border-bottom-left-radius")) {
            float r = css_parse_length_pct(val, val_len, NULL);
            box->border_radius[3] = (r < 0.0f) ? 0.0f : r;
        }
    }
}

/* ─── Box tree ────────────────────────────────────────────────────────────── */

static q_box_t *q_box_create(q_box_type_t type, lxb_dom_node_t *dom_node,
                              const char *text, size_t text_len)
{
    q_box_t *box = (q_box_t *) calloc(1, sizeof(*box));
    if (box == NULL) {
        return NULL;
    }

    box->type = type;
    box->dom_node = dom_node;
    box->text = text;
    box->text_len = text_len;
    box->background_color = Q_DEFAULT_BACKGROUND;
    box->border_color[0] = Q_DEFAULT_BORDER;
    box->border_color[1] = Q_DEFAULT_BORDER;
    box->border_color[2] = Q_DEFAULT_BORDER;
    box->border_color[3] = Q_DEFAULT_BORDER;
    box->border_width[0] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[1] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[2] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[3] = Q_DEFAULT_BORDER_WIDTH;
    box->font_size = (float) NAN;
    box->font_weight = 0;
    box->text_color = Q_DEFAULT_TEXT_COLOR;
    box->has_text_color = 0;

    if (type == Q_BOX_IMAGE) {
        box->background_color = 0x00000000u;
        box->border_width[0] = 0.0f;
        box->border_width[1] = 0.0f;
        box->border_width[2] = 0.0f;
        box->border_width[3] = 0.0f;
    }
    if (type == Q_BOX_LINE_BREAK) {
        box->background_color = 0x00000000u;
        box->border_width[0] = 0.0f;
        box->border_width[1] = 0.0f;
        box->border_width[2] = 0.0f;
        box->border_width[3] = 0.0f;
    }

    /* NaN sentinel = "not set" for all explicit style dimensions / offsets */
    box->style_top       = (float) NAN;
    box->style_right     = (float) NAN;
    box->style_bottom    = (float) NAN;
    box->style_left      = (float) NAN;
    box->style_width     = (float) NAN;
    box->style_width_pct = (float) NAN;
    box->style_height    = (float) NAN;
    box->style_min_width  = (float) NAN;
    box->style_max_width  = (float) NAN;
    box->style_min_height = (float) NAN;
    box->style_max_height = (float) NAN;

    /* UA stylesheet defaults for specific element types */
    if (type == Q_BOX_TABLE) {
        /* WHATWG UA: table { border-spacing: 2px } */
        box->table_border_spacing = 2.0f;
    }

    return box;
}

static void q_box_inherit_text_style(q_box_t *box, const q_box_t *parent)
{
    if (box == NULL || parent == NULL) {
        return;
    }
    if (isnan(box->font_size) && !isnan(parent->font_size)) {
        box->font_size = parent->font_size;
    }
    if (box->font_weight == 0 && parent->font_weight != 0) {
        box->font_weight = parent->font_weight;
    }
    if (box->font_style == Q_FONT_STYLE_NORMAL
        && parent->font_style != Q_FONT_STYLE_NORMAL) {
        box->font_style = parent->font_style;
    }
    if (box->font_family == NULL && parent->font_family != NULL) {
        box->font_family = parent->font_family;
    }
    if (!box->has_text_color && parent->has_text_color) {
        box->text_color = parent->text_color;
        box->has_text_color = 1;
    }
    if (box->text_align == Q_TEXT_ALIGN_LEFT
        && parent->text_align != Q_TEXT_ALIGN_LEFT) {
        box->text_align = parent->text_align;
    }
}

static int q_box_append_child(q_box_t *parent, q_box_t *child)
{
    child->parent = parent;

    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
    } else {
        parent->first_child = child;
    }

    parent->last_child = child;
    return 0;
}

static void q_box_load_image(q_document_t *doc, q_box_t *box, lxb_dom_node_t *node)
{
    lxb_dom_element_t *element;
    const lxb_char_t *src;
    size_t src_len = 0;
    char *src_buf;
    char *resolved;

    if (doc == NULL || box == NULL || node == NULL
        || node->type != LXB_DOM_NODE_TYPE_ELEMENT
        || lxb_dom_node_tag_id(node) != LXB_TAG_IMG)
    {
        return;
    }

    element = lxb_dom_interface_element(node);
    src = lxb_dom_element_get_attribute(element, (const lxb_char_t *) "src", 3, &src_len);
    if (src == NULL || src_len == 0u) {
        return;
    }

    src_buf = (char *) malloc(src_len + 1u);
    if (src_buf == NULL) {
        return;
    }

    memcpy(src_buf, src, src_len);
    src_buf[src_len] = '\0';

    resolved = q_url_resolve(q_document_base_url(doc), src_buf);
    free(src_buf);
    if (resolved == NULL) {
        return;
    }

    box->image = q_image_load_url(resolved);
    free(resolved);
}

static int q_text_is_whitespace(const lxb_char_t *text, size_t len)
{
    size_t i;

    for (i = 0; i < len; ++i) {
        if (!isspace((unsigned char) text[i])) {
            return 0;
        }
    }

    return 1;
}

static q_box_type_t q_box_type_from_tag_id(lxb_tag_id_t tag_id)
{
    switch (tag_id) {
        case LXB_TAG_IMG:
            return Q_BOX_IMAGE;
        case LXB_TAG_TABLE:
            return Q_BOX_TABLE;
        case LXB_TAG_THEAD:
        case LXB_TAG_TBODY:
        case LXB_TAG_TFOOT:
            return Q_BOX_TABLE_SECTION;
        case LXB_TAG_TR:
            return Q_BOX_TABLE_ROW;
        case LXB_TAG_TD:
        case LXB_TAG_TH:
            return Q_BOX_TABLE_CELL;
        case LXB_TAG_CAPTION:
            return Q_BOX_TABLE_CAPTION;
        case LXB_TAG_BR:
            return Q_BOX_LINE_BREAK;
        default:
            return Q_BOX_BLOCK;
    }
}

static q_box_t *q_ensure_inline_container(q_box_t *parent)
{
    q_box_t *ic;

    if (parent == NULL) {
        return NULL;
    }

    if (parent->last_child != NULL
        && parent->last_child->type == Q_BOX_INLINE_CONTAINER)
    {
        parent->last_child->white_space = parent->white_space;
        parent->last_child->text_decoration = parent->text_decoration;
        q_box_inherit_text_style(parent->last_child, parent);
        return parent->last_child;
    }

    ic = q_box_create(Q_BOX_INLINE_CONTAINER, NULL, NULL, 0);
    if (ic == NULL) {
        return NULL;
    }
    if (q_box_append_child(parent, ic) != 0) {
        free(ic);
        return NULL;
    }
    ic->white_space = parent->white_space;
    ic->text_decoration = parent->text_decoration;
    q_box_inherit_text_style(ic, parent);
    return ic;
}

static void q_box_set_widget_value(q_box_t *box, const lxb_char_t *value, size_t value_len)
{
    char *buf;

    if (box == NULL) {
        return;
    }

    free(box->widget_value);
    box->widget_value = NULL;
    box->widget_value_len = 0;
    box->widget_caret = 0;

    if (value == NULL || value_len == 0u) {
        return;
    }

    buf = (char *) malloc(value_len + 1u);
    if (buf == NULL) {
        return;
    }

    memcpy(buf, value, value_len);
    buf[value_len] = '\0';
    box->widget_value = buf;
    box->widget_value_len = value_len;
    box->widget_caret = value_len;
}

static int q_count_preceding_list_items(lxb_dom_node_t *node)
{
    int index = 1;
    lxb_dom_node_t *prev;

    if (node == NULL) {
        return 1;
    }

    for (prev = node->prev; prev != NULL; prev = prev->prev) {
        if (prev->type == LXB_DOM_NODE_TYPE_ELEMENT
            && lxb_dom_node_tag_id(prev) == LXB_TAG_LI)
        {
            ++index;
        }
    }

    return index;
}

static int q_layout_walk_node(q_document_t *doc, lxb_dom_node_t *node, q_box_t *parent)
{
    q_box_t *current = NULL;
    q_box_t *child_parent;
    lxb_dom_node_t *child;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT
        || node->type == LXB_DOM_NODE_TYPE_DOCUMENT)
    {
        q_box_type_t type = Q_BOX_BLOCK;

        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT)
        {
            type = q_box_type_from_tag_id(lxb_dom_node_tag_id(node));
        }

        current = q_box_create(type, node, NULL, 0);
        if (current != NULL && parent != NULL) {
            q_box_inherit_text_style(current, parent);
        }
        if (current != NULL && lxb_dom_node_type(node) == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_tag_id_t tag_id = lxb_dom_node_tag_id(node);
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            size_t style_len = 0;
            const lxb_char_t *style =
                lxb_dom_element_get_attribute(el,
                                              (const lxb_char_t *) "style",
                                              sizeof("style") - 1,
                                              &style_len);

            /* UA stylesheet defaults applied before author styles */
            if (tag_id == LXB_TAG_BODY) {
                /* WHATWG UA: body { margin: 8px } */
                current->margin_top    = 8.0f;
                current->margin_right  = 8.0f;
                current->margin_bottom = 8.0f;
                current->margin_left   = 8.0f;
            } else if (tag_id == LXB_TAG_UL) {
                /* WHATWG UA: ul { margin-block: 1em; padding-inline-start: 40px; list-style-type: disc } */
                current->margin_top = 16.0f;
                current->margin_bottom = 16.0f;
                current->padding_left = 40.0f;
                current->list_style_type = Q_LIST_STYLE_DISC;
            } else if (tag_id == LXB_TAG_OL) {
                /* WHATWG UA: ol { margin-block: 1em; padding-inline-start: 40px; list-style-type: decimal } */
                current->margin_top = 16.0f;
                current->margin_bottom = 16.0f;
                current->padding_left = 40.0f;
                current->list_style_type = Q_LIST_STYLE_DECIMAL;
            } else if (tag_id == LXB_TAG_LI) {
                /* list-item markers are rendered in paint.c */
                current->padding_left = 20.0f;
                if (parent != NULL && parent->list_style_type != Q_LIST_STYLE_NONE) {
                    current->list_style_type = parent->list_style_type;
                } else {
                    current->list_style_type = Q_LIST_STYLE_DISC;
                }
                current->list_item_index = q_count_preceding_list_items(node);
            } else if (tag_id >= LXB_TAG_H1 && tag_id <= LXB_TAG_H6) {
                /* WHATWG UA: h1-h6 { font-weight:bold; font-size:Nem;
                 *                    margin-block: 0.67em * font_size } */
                static const float heading_em[6] = {
                    2.0f, 1.5f, 1.17f, 1.0f, 0.83f, 0.67f
                };
                int level = (int)(tag_id - LXB_TAG_H1); /* 0 = h1 … 5 = h6 */
                current->font_size    = heading_em[level] * 16.0f;
                current->font_weight  = 700;
                current->margin_top   = current->font_size * 0.67f;
                current->margin_bottom = current->font_size * 0.67f;
            }

            if (tag_id == LXB_TAG_TD || tag_id == LXB_TAG_TH) {
                /* WHATWG UA: td, th { padding: 1px } */
                current->padding_top    = 1.0f;
                current->padding_right  = 1.0f;
                current->padding_bottom = 1.0f;
                current->padding_left   = 1.0f;
            }

            if (tag_id == LXB_TAG_B || tag_id == LXB_TAG_STRONG) {
                /* WHATWG UA: b, strong { font-weight: bold }
                 * Mark as inline-block so the box flows inline with surrounding
                 * text while carrying the bold font-weight for its children. */
                current->font_weight    = 700;
                current->is_inline_block = 1;
            }

            if (tag_id == LXB_TAG_I || tag_id == LXB_TAG_EM) {
                /* WHATWG UA: i, em { font-style: italic }
                 * Mark as inline-block so the box flows inline with surrounding
                 * text while carrying the italic font-style for its children. */
                current->font_style     = Q_FONT_STYLE_ITALIC;
                current->is_inline_block = 1;
            }

            if (tag_id == LXB_TAG_CODE || tag_id == LXB_TAG_KBD || tag_id == LXB_TAG_TT) {
                /* WHATWG UA: code, kbd, tt { font-family: monospace } */
                current->font_family = "monospace";
                current->is_inline_block = 1;
            }

            if (tag_id == LXB_TAG_BLOCKQUOTE) {
                /* WHATWG UA: blockquote { margin-inline: 40px } */
                current->margin_left = 40.0f;
                current->margin_right = 40.0f;
            }

            if (tag_id == LXB_TAG_S || tag_id == LXB_TAG_DEL) {
                /* WHATWG UA: s, del { text-decoration: line-through } */
                current->text_decoration |= Q_TEXT_DECORATION_LINE_THROUGH;
                current->is_inline_block = 1;
            }

            if (tag_id == LXB_TAG_A) {
                /* WHATWG UA: a:link { color: #0000EE; text-decoration: underline }
                 * Read the href attribute and store a copy on the box so that
                 * event.c can walk up the box tree to find the nearest href. */
                const lxb_char_t *href_attr;
                size_t href_len = 0;
                current->text_color = 0x0000EEFFu;
                current->has_text_color = 1;
                current->text_decoration |= Q_TEXT_DECORATION_UNDERLINE;
                current->is_inline_block = 1;
                href_attr = lxb_dom_element_get_attribute(
                    el, (const lxb_char_t *) "href", 4, &href_len);
                if (href_attr != NULL && href_len > 0) {
                    current->href = (char *) malloc(href_len + 1u);
                    if (current->href != NULL) {
                        memcpy(current->href, href_attr, href_len);
                        current->href[href_len] = '\0';
                    }
                }
            }

            if (tag_id == LXB_TAG_SUP || tag_id == LXB_TAG_SUB) {
                /* WHATWG UA: sup/sub use vertical-align and smaller font-size. */
                current->vertical_align = (tag_id == LXB_TAG_SUP)
                                          ? Q_VERTICAL_ALIGN_SUPER
                                          : Q_VERTICAL_ALIGN_SUB;
                if (!isnan(current->font_size) && current->font_size > 0.0f) {
                    current->font_size *= 0.75f;
                } else {
                    current->font_size = 12.0f;
                }
                current->is_inline_block = 1;
            }

            if (tag_id == LXB_TAG_HR) {
                /* WHATWG UA: hr { border-top: 1px solid #888; margin-block: 4px;
                 *                 height: 0 } */
                current->style_height     = 0.0f;
                current->border_width[0]  = 1.0f;  /* top */
                current->border_color[0]  = 0x888888FFu;
                current->margin_top       = 4.0f;
                current->margin_bottom    = 4.0f;
            }

            if (tag_id == LXB_TAG_INPUT) {
                const lxb_char_t *type_attr;
                const lxb_char_t *value_attr;
                size_t type_len = 0;
                size_t value_len = 0;
                current->is_inline_block = 1;
                current->background_color = 0xFFFFFFFFu;
                current->border_width[0] = 1.0f;
                current->border_width[1] = 1.0f;
                current->border_width[2] = 1.0f;
                current->border_width[3] = 1.0f;
                current->border_color[0] = 0x707070FFu;
                current->border_color[1] = 0x707070FFu;
                current->border_color[2] = 0x707070FFu;
                current->border_color[3] = 0x707070FFu;
                current->padding_top = 2.0f;
                current->padding_bottom = 2.0f;
                current->padding_left = 4.0f;
                current->padding_right = 4.0f;

                type_attr = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "type", 4, &type_len);
                if (type_attr != NULL
                    && (css_name_eq(type_attr, type_len, "checkbox")
                        || css_name_eq(type_attr, type_len, "radio")))
                {
                    current->style_width = 14.0f;
                    current->style_height = 14.0f;
                    current->padding_top = 0.0f;
                    current->padding_right = 0.0f;
                    current->padding_bottom = 0.0f;
                    current->padding_left = 0.0f;
                    if (css_name_eq(type_attr, type_len, "radio")) {
                        current->border_radius[0] = 7.0f;
                        current->border_radius[1] = 7.0f;
                        current->border_radius[2] = 7.0f;
                        current->border_radius[3] = 7.0f;
                    }
                    current->widget_type = css_name_eq(type_attr, type_len, "radio")
                                          ? Q_WIDGET_INPUT_RADIO
                                          : Q_WIDGET_INPUT_CHECK;
                } else if (type_attr != NULL
                           && (css_name_eq(type_attr, type_len, "submit")
                               || css_name_eq(type_attr, type_len, "button")
                               || css_name_eq(type_attr, type_len, "reset")))
                {
                    current->style_width = 90.0f;
                    current->style_height = 24.0f;
                    current->background_color = 0xE0E0E0FFu;
                    current->padding_left = 8.0f;
                    current->padding_right = 8.0f;
                    current->widget_type = Q_WIDGET_INPUT_SUBMIT;
                } else {
                    current->style_width = 140.0f;
                    current->style_height = 22.0f;
                    current->widget_type = Q_WIDGET_INPUT_TEXT;
                }

                value_attr = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "value", 5, &value_len);
                q_box_set_widget_value(current, value_attr, value_len);
                if (current->widget_type == Q_WIDGET_INPUT_CHECK
                    || current->widget_type == Q_WIDGET_INPUT_RADIO)
                {
                    /*
                     * "checked" is a boolean HTML attribute; when written
                     * without a value (e.g. <input checked>), lxb stores it
                     * with a NULL value, so lxb_dom_element_get_attribute()
                     * would incorrectly report it as absent. Use the
                     * presence check instead.
                     */
                    current->widget_checked =
                        lxb_dom_element_has_attribute(el, (const lxb_char_t *) "checked", 7) ? 1 : 0;
                }
            } else if (tag_id == LXB_TAG_BUTTON) {
                const lxb_char_t *value_attr;
                size_t value_len = 0;
                current->is_inline_block = 1;
                current->style_height = 24.0f;
                current->background_color = 0xE0E0E0FFu;
                current->border_width[0] = 1.0f;
                current->border_width[1] = 1.0f;
                current->border_width[2] = 1.0f;
                current->border_width[3] = 1.0f;
                current->border_color[0] = 0x707070FFu;
                current->border_color[1] = 0x707070FFu;
                current->border_color[2] = 0x707070FFu;
                current->border_color[3] = 0x707070FFu;
                current->padding_top = 2.0f;
                current->padding_bottom = 2.0f;
                current->padding_left = 8.0f;
                current->padding_right = 8.0f;
                current->widget_type = Q_WIDGET_BUTTON;
                value_attr = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "value", 5, &value_len);
                q_box_set_widget_value(current, value_attr, value_len);
            } else if (tag_id == LXB_TAG_SELECT) {
                current->widget_type = Q_WIDGET_SELECT;
                current->is_inline_block = 1;
                current->style_width = 120.0f;
                current->style_height = 22.0f;
                current->background_color = 0xFFFFFFFFu;
                current->border_width[0] = 1.0f;
                current->border_width[1] = 1.0f;
                current->border_width[2] = 1.0f;
                current->border_width[3] = 1.0f;
                current->border_color[0] = 0x707070FFu;
                current->border_color[1] = 0x707070FFu;
                current->border_color[2] = 0x707070FFu;
                current->border_color[3] = 0x707070FFu;
                current->padding_top = 2.0f;
                current->padding_bottom = 2.0f;
                current->padding_left = 4.0f;
                current->padding_right = 18.0f;
            } else if (tag_id == LXB_TAG_TEXTAREA) {
                const lxb_char_t *rows_attr;
                const lxb_char_t *cols_attr;
                size_t rows_len = 0;
                size_t cols_len = 0;
                int rows = 2;
                int cols = 20;
                current->widget_type = Q_WIDGET_TEXTAREA;
                current->is_inline_block = 1;
                current->background_color = 0xFFFFFFFFu;
                current->border_width[0] = 1.0f;
                current->border_width[1] = 1.0f;
                current->border_width[2] = 1.0f;
                current->border_width[3] = 1.0f;
                current->border_color[0] = 0x707070FFu;
                current->border_color[1] = 0x707070FFu;
                current->border_color[2] = 0x707070FFu;
                current->border_color[3] = 0x707070FFu;
                current->padding_top = 2.0f;
                current->padding_bottom = 2.0f;
                current->padding_left = 4.0f;
                current->padding_right = 4.0f;

                rows_attr = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "rows", 4, &rows_len);
                cols_attr = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "cols", 4, &cols_len);
                if (rows_attr != NULL) {
                    int parsed = 0;
                    if (css_parse_int(rows_attr, rows_len, &parsed) && parsed > 0) {
                        rows = parsed;
                    }
                }
                if (cols_attr != NULL) {
                    int parsed = 0;
                    if (css_parse_int(cols_attr, cols_len, &parsed) && parsed > 0) {
                        cols = parsed;
                    }
                }
                current->style_width = (float) (cols * 8 + 8);
                current->style_height = (float) (rows * 18 + 8);
            }

            if (style != NULL && style_len > 0) {
                parse_style_attribute(style, style_len, current, doc);
            }
            if (type == Q_BOX_IMAGE) {
                q_box_load_image(doc, current, node);
            }
        }
    }
    else if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_character_data_t *ch_data = (lxb_dom_character_data_t *) node;

        if (ch_data->data.length != 0) {
            /* In normal (non-PRE) flow, whitespace-only text between inline
             * elements collapses to a single space " " that the line-wrapper
             * uses as an inter-element gap signal.  We only do this when an
             * inline container is already the parent's last child; whitespace
             * that would open a brand-new IC (i.e. block-level inter-element
             * whitespace) is dropped to avoid spurious anonymous ICs.
             * In PRE mode the raw text is always preserved. */
            static const char space_char = ' ';
            const char *text_data;
            size_t      text_len;
            int is_ws_only;
            q_box_t *ic;
            q_box_t *text_box;

            is_ws_only = (parent->white_space != Q_WHITE_SPACE_PRE
                          && q_text_is_whitespace(ch_data->data.data,
                                                  ch_data->data.length));
            if (is_ws_only) {
                /* Drop whitespace that would create a new IC at block level */
                if (parent->last_child == NULL
                    || parent->last_child->type != Q_BOX_INLINE_CONTAINER)
                {
                    return 0;
                }
                text_data = &space_char;
                text_len  = 1;
            } else {
                text_data = (const char *) ch_data->data.data;
                text_len  = ch_data->data.length;
            }

            ic = q_ensure_inline_container(parent);
            if (ic == NULL) {
                return -1;
            }

            text_box = q_box_create(Q_BOX_TEXT, node, text_data, text_len);
            if (text_box == NULL) {
                return -1;
            }
            text_box->text_decoration = ic->text_decoration;
            q_box_inherit_text_style(text_box, ic);
            if (q_box_append_child(ic, text_box) != 0) {
                free(text_box);
                return -1;
            }
        }
        /* Text nodes have no DOM children; nothing more to do */
        return 0;
    }

    if (current != NULL && parent != NULL) {
        if (current->type == Q_BOX_IMAGE
            || current->type == Q_BOX_LINE_BREAK
            || current->is_inline_block) {
            q_box_t *ic = q_ensure_inline_container(parent);
            if (ic == NULL || q_box_append_child(ic, current) != 0) {
                free(current);
                return -1;
            }
        } else if (q_box_append_child(parent, current) != 0) {
            free(current);
            return -1;
        }
    }

    child_parent = (current != NULL) ? current : parent;

    for (child = node->first_child; child != NULL; child = child->next) {
        if (q_layout_walk_node(doc, child, child_parent) != 0) {
            return -1;
        }
    }

    return 0;
}

q_box_t *q_layout_build_tree(q_document_t *doc)
{
    lxb_html_document_t *document;
    lxb_html_body_element_t *body;
    lxb_dom_node_t *root_node;
    q_box_t *root;

    if (doc == NULL) {
        return NULL;
    }

    document = q_document_handle(doc);
    if (document == NULL) {
        return NULL;
    }

    body = lxb_html_document_body_element(document);
    if (body != NULL) {
        root_node = lxb_dom_interface_node(body);
    } else {
        root_node = lxb_dom_interface_node(document);
    }

    root = q_box_create(Q_BOX_BLOCK, root_node, NULL, 0);
    if (root == NULL) {
        return NULL;
    }
    root->font_size = 16.0f;
    root->font_weight = 400;
    root->text_color = Q_DEFAULT_TEXT_COLOR;
    root->has_text_color = 1;
    {
        size_t title_len = 0;
        const lxb_char_t *title = lxb_html_document_title(document, &title_len);
        if (title != NULL && title_len > 0u) {
            root->document_title = (char *) malloc(title_len + 1u);
            if (root->document_title != NULL) {
                memcpy(root->document_title, title, title_len);
                root->document_title[title_len] = '\0';
            }
        }
    }

    if (body != NULL) {
        /* WHATWG UA stylesheet: body { margin: 8px } */
        root->margin_top    = 8.0f;
        root->margin_right  = 8.0f;
        root->margin_bottom = 8.0f;
        root->margin_left   = 8.0f;

        /* Apply any inline style on <body> itself (may override UA defaults) */
        {
            lxb_dom_element_t *body_el = lxb_dom_interface_element(
                lxb_dom_interface_node(body));
            size_t style_len = 0;
            const lxb_char_t *style = lxb_dom_element_get_attribute(
                body_el, (const lxb_char_t *) "style",
                sizeof("style") - 1, &style_len);
            if (style != NULL && style_len > 0) {
                parse_style_attribute(style, style_len, root, doc);
            }
        }
    }

    for (root_node = root_node->first_child; root_node != NULL; root_node = root_node->next) {
        if (q_layout_walk_node(doc, root_node, root) != 0) {
            q_layout_free_tree(root);
            return NULL;
        }
    }

    q_table_fixup_anonymous(root);

    return root;
}

void q_layout_free_tree(q_box_t *root)
{
    q_box_t *child;
    q_box_t *next;

    if (root == NULL) {
        return;
    }

    child = root->first_child;
    while (child != NULL) {
        next = child->next_sibling;
        q_layout_free_tree(child);
        child = next;
    }

    q_shaped_run_free(root->run);
    q_image_release(root->image);
    q_image_release(root->background_image);
    free(root->tile);
    free(root->self_tile);
    free(root->document_title);
    free(root->href);
    free(root->widget_value);
    if (root->table != NULL) {
        q_table_free(root->table);
    }
    free(root);
}
