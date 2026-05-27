#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include "third_party/libschrift/schrift.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define Q_DEFAULT_FONT_PATH "/usr/share/fonts/dejavu/DejaVuSans.ttf"

static const char *q_default_font_path(void)
{
    static const char *paths[] = {
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (access(paths[i], R_OK) == 0) {
            return paths[i];
        }
    }

    return Q_DEFAULT_FONT_PATH;
}

struct q_font {
    char *family;
    char *path;
    SFT_Font *sft_font;
    SFT sft;
    uint8_t *font_data;
    size_t font_len;
    float size_px;
    int weight;
};

struct q_font_cache {
    q_font_t **entries;
    size_t count;
    size_t capacity;
};

static int q_cache_reserve(q_font_cache_t *cache, size_t needed)
{
    q_font_t **new_entries;
    size_t new_capacity;

    if (needed <= cache->capacity) {
        return 0;
    }

    new_capacity = (cache->capacity == 0) ? 8 : cache->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    new_entries = (q_font_t **) realloc(cache->entries, new_capacity * sizeof(*new_entries));
    if (new_entries == NULL) {
        return -1;
    }

    cache->entries = new_entries;
    cache->capacity = new_capacity;
    return 0;
}

static int q_utf8_next(const char *text, size_t len, size_t *idx, uint32_t *out)
{
    unsigned char c0;

    if (*idx >= len) {
        return 0;
    }

    c0 = (unsigned char) text[*idx];
    if ((c0 & 0x80U) == 0) {
        *out = c0;
        *idx += 1;
        return 1;
    }

    if ((c0 & 0xE0U) == 0xC0U && *idx + 1 < len) {
        unsigned char c1 = (unsigned char) text[*idx + 1];
        if ((c1 & 0xC0U) == 0x80U) {
            *out = ((uint32_t) (c0 & 0x1FU) << 6) | (uint32_t) (c1 & 0x3FU);
            *idx += 2;
            return 1;
        }
    }

    if ((c0 & 0xF0U) == 0xE0U && *idx + 2 < len) {
        unsigned char c1 = (unsigned char) text[*idx + 1];
        unsigned char c2 = (unsigned char) text[*idx + 2];
        if ((c1 & 0xC0U) == 0x80U && (c2 & 0xC0U) == 0x80U) {
            *out = ((uint32_t) (c0 & 0x0FU) << 12)
                 | ((uint32_t) (c1 & 0x3FU) << 6)
                 | (uint32_t) (c2 & 0x3FU);
            *idx += 3;
            return 1;
        }
    }

    if ((c0 & 0xF8U) == 0xF0U && *idx + 3 < len) {
        unsigned char c1 = (unsigned char) text[*idx + 1];
        unsigned char c2 = (unsigned char) text[*idx + 2];
        unsigned char c3 = (unsigned char) text[*idx + 3];
        if ((c1 & 0xC0U) == 0x80U && (c2 & 0xC0U) == 0x80U && (c3 & 0xC0U) == 0x80U) {
            *out = ((uint32_t) (c0 & 0x07U) << 18)
                 | ((uint32_t) (c1 & 0x3FU) << 12)
                 | ((uint32_t) (c2 & 0x3FU) << 6)
                 | (uint32_t) (c3 & 0x3FU);
            *idx += 4;
            return 1;
        }
    }

    *out = c0;
    *idx += 1;
    return 1;
}

static size_t q_utf8_codepoint_count(const char *text, size_t len)
{
    size_t i = 0;
    size_t count = 0;
    uint32_t codepoint;

    while (q_utf8_next(text, len, &i, &codepoint)) {
        (void) codepoint;
        ++count;
    }

    return count;
}

static q_font_t *q_find_font(q_font_cache_t *cache,
                             const char *family,
                             const char *path,
                             float size_px,
                             int weight)
{
    size_t i;

    for (i = 0; i < cache->count; ++i) {
        q_font_t *font = cache->entries[i];
        if (font->weight == weight && fabsf(font->size_px - size_px) < 0.01f
            && strcmp(font->family, family) == 0 && strcmp(font->path, path) == 0)
        {
            return font;
        }
    }

    return NULL;
}

static void q_font_destroy(q_font_t *font)
{
    if (font == NULL) {
        return;
    }

    free(font->family);
    free(font->path);

    if (font->sft_font != NULL) {
        sft_freefont(font->sft_font);
    }

    free(font->font_data);
    free(font);
}

q_font_cache_t *q_font_cache_create(void)
{
    return (q_font_cache_t *) calloc(1, sizeof(q_font_cache_t));
}

void q_font_cache_destroy(q_font_cache_t *cache)
{
    size_t i;

    if (cache == NULL) {
        return;
    }

    for (i = 0; i < cache->count; ++i) {
        q_font_destroy(cache->entries[i]);
    }

    free(cache->entries);
    free(cache);
}

q_font_t *q_font_load(q_font_cache_t *cache,
                      const char *family_name,
                      const char *ttf_path,
                      float size_px,
                      int weight)
{
    const char *path;
    const char *family;
    q_font_t *font;

    if (cache == NULL) {
        return NULL;
    }

    family = (family_name != NULL) ? family_name : "sans-serif";
    path = (ttf_path != NULL) ? ttf_path : q_default_font_path();

    font = q_find_font(cache, family, path, size_px, weight);
    if (font != NULL) {
        return font;
    }

    font = (q_font_t *) calloc(1, sizeof(*font));
    if (font == NULL) {
        return NULL;
    }

    font->family = strdup(family);
    font->path = strdup(path);
    font->sft_font = sft_loadfile(path);
    font->size_px = size_px;
    font->weight = weight;

    if (font->family == NULL || font->path == NULL || font->sft_font == NULL) {
        q_font_destroy(font);
        return NULL;
    }

    font->sft.font = font->sft_font;
    font->sft.xScale = size_px;
    font->sft.yScale = size_px;
    font->sft.xOffset = 0.0;
    font->sft.yOffset = 0.0;
    font->sft.flags = SFT_DOWNWARD_Y;

    if (q_cache_reserve(cache, cache->count + 1) != 0) {
        q_font_destroy(font);
        return NULL;
    }

    cache->entries[cache->count++] = font;
    return font;
}

q_font_t *q_font_load_mem(q_font_cache_t *cache,
                          const char *family_name,
                          const void *data,
                          size_t len,
                          float size_px,
                          int weight)
{
    q_font_t *font;
    const char *family;

    if (cache == NULL || data == NULL || len == 0) {
        return NULL;
    }

    family = (family_name != NULL) ? family_name : "sans-serif";

    font = (q_font_t *) calloc(1, sizeof(*font));
    if (font == NULL) {
        return NULL;
    }

    font->family = strdup(family);
    font->path = strdup("memory://font");
    font->font_data = (uint8_t *) malloc(len);
    if (font->family == NULL || font->path == NULL || font->font_data == NULL) {
        q_font_destroy(font);
        return NULL;
    }

    memcpy(font->font_data, data, len);
    font->font_len = len;
    font->sft_font = sft_loadmem(font->font_data, len);
    font->size_px = size_px;
    font->weight = weight;

    if (font->sft_font == NULL) {
        q_font_destroy(font);
        return NULL;
    }

    font->sft.font = font->sft_font;
    font->sft.xScale = size_px;
    font->sft.yScale = size_px;
    font->sft.xOffset = 0.0;
    font->sft.yOffset = 0.0;
    font->sft.flags = SFT_DOWNWARD_Y;

    if (q_cache_reserve(cache, cache->count + 1) != 0) {
        q_font_destroy(font);
        return NULL;
    }

    cache->entries[cache->count++] = font;
    return font;
}

q_font_t *q_font_match(q_font_cache_t *cache,
                       const char *family_name,
                       float size_px,
                       int weight)
{
    size_t i;

    if (cache == NULL) {
        return NULL;
    }

    for (i = 0; i < cache->count; ++i) {
        q_font_t *font = cache->entries[i];
        if ((family_name == NULL || strcmp(font->family, family_name) == 0)
            && font->weight == weight
            && fabsf(font->size_px - size_px) < 0.01f)
        {
            return font;
        }
    }

    return q_font_load(cache,
                       (family_name != NULL) ? family_name : "sans-serif",
                       q_default_font_path(),
                       size_px,
                       weight);
}

float q_font_measure(q_font_t *font, const char *text, size_t len)
{
    size_t i = 0;
    float advance = 0.0f;
    SFT_Glyph prev = 0;
    int has_prev = 0;

    if (font == NULL || text == NULL) {
        return 0.0f;
    }

    while (i < len) {
        SFT_Glyph glyph;
        SFT_GMetrics metrics;
        uint32_t cp;

        if (!q_utf8_next(text, len, &i, &cp)) {
            break;
        }

        if (sft_lookup(&font->sft, (SFT_UChar) cp, &glyph) < 0
            || sft_gmetrics(&font->sft, glyph, &metrics) < 0)
        {
            advance += font->size_px * 0.6f;
            has_prev = 0;
            continue;
        }

        if (has_prev) {
            SFT_Kerning kerning;
            if (sft_kerning(&font->sft, prev, glyph, &kerning) == 0) {
                advance += (float) kerning.xShift;
            }
        }

        advance += (float) metrics.advanceWidth;
        prev = glyph;
        has_prev = 1;
    }

    return advance;
}

q_shaped_run_t *q_font_shape_run(q_font_t *font, const char *text, size_t len)
{
    q_shaped_run_t *run;
    size_t glyph_count;
    size_t i = 0;
    size_t g = 0;
    SFT_Glyph prev = 0;
    int has_prev = 0;
    SFT_LMetrics lmetrics;

    if (font == NULL || text == NULL) {
        return NULL;
    }

    glyph_count = q_utf8_codepoint_count(text, len);

    run = (q_shaped_run_t *) calloc(1, sizeof(*run));
    if (run == NULL) {
        return NULL;
    }

    run->glyphs = (q_glyph_t *) calloc(glyph_count, sizeof(*run->glyphs));
    if (run->glyphs == NULL) {
        free(run);
        return NULL;
    }

    run->count = glyph_count;
    run->font = font;

    if (sft_lmetrics(&font->sft, &lmetrics) == 0) {
        run->ascender = (float) lmetrics.ascender;
        run->descender = (float) lmetrics.descender;
        run->line_gap = (float) lmetrics.lineGap;
    }

    while (i < len && g < glyph_count) {
        uint32_t cp;
        SFT_Glyph glyph;
        SFT_GMetrics metrics;
        float x_advance;
        float x_offset = 0.0f;

        if (!q_utf8_next(text, len, &i, &cp)) {
            break;
        }

        if (sft_lookup(&font->sft, (SFT_UChar) cp, &glyph) < 0
            || sft_gmetrics(&font->sft, glyph, &metrics) < 0)
        {
            x_advance = font->size_px * 0.6f;
            has_prev = 0;
        }
        else {
            x_advance = (float) metrics.advanceWidth;
            if (has_prev) {
                SFT_Kerning kerning;
                if (sft_kerning(&font->sft, prev, glyph, &kerning) == 0) {
                    x_offset = (float) kerning.xShift;
                }
            }

            prev = glyph;
            has_prev = 1;
        }

        run->glyphs[g].codepoint = cp;
        run->glyphs[g].x_offset = x_offset;
        run->glyphs[g].y_offset = 0.0f;
        run->glyphs[g].x_advance = x_advance;
        run->total_advance += x_offset + x_advance;
        ++g;
    }

    run->count = g;
    return run;
}

static uint8_t q_color_r(uint32_t color) { return (uint8_t) ((color >> 24) & 0xFFu); }
static uint8_t q_color_g(uint32_t color) { return (uint8_t) ((color >> 16) & 0xFFu); }
static uint8_t q_color_b(uint32_t color) { return (uint8_t) ((color >> 8) & 0xFFu); }
static uint8_t q_color_a(uint32_t color) { return (uint8_t) (color & 0xFFu); }

void q_font_render_run(const q_shaped_run_t *run,
                       uint32_t color,
                       uint8_t *pixels,
                       int tile_w, int tile_h,
                       int dest_x, int dest_y)
{
    size_t i;
    float pen_x = 0.0f;
    float baseline_y;
    q_font_t *font;
    uint8_t cr;
    uint8_t cg;
    uint8_t cb;
    uint8_t ca;

    if (run == NULL || run->font == NULL || run->glyphs == NULL
        || pixels == NULL || tile_w <= 0 || tile_h <= 0)
    {
        return;
    }

    font = run->font;
    baseline_y = run->ascender;
    cr = q_color_r(color);
    cg = q_color_g(color);
    cb = q_color_b(color);
    ca = q_color_a(color);

    for (i = 0; i < run->count; ++i) {
        const q_glyph_t *g = &run->glyphs[i];
        SFT_Glyph glyph;
        SFT_GMetrics metrics;
        SFT_Image image;
        uint8_t *glyph_pixels;
        int gx;
        int gy;
        int y;
        int x;

        pen_x += g->x_offset;
        if (sft_lookup(&font->sft, (SFT_UChar) g->codepoint, &glyph) < 0
            || sft_gmetrics(&font->sft, glyph, &metrics) < 0)
        {
            pen_x += g->x_advance;
            continue;
        }

        if (metrics.minWidth <= 0 || metrics.minHeight <= 0) {
            pen_x += g->x_advance;
            continue;
        }

        glyph_pixels = (uint8_t *) calloc((size_t) metrics.minWidth
                                          * (size_t) metrics.minHeight, 1u);
        if (glyph_pixels == NULL) {
            pen_x += g->x_advance;
            continue;
        }

        image.pixels = glyph_pixels;
        image.width = metrics.minWidth;
        image.height = metrics.minHeight;

        if (sft_render(&font->sft, glyph, image) == 0) {
            gx = dest_x + (int) lroundf(pen_x + (float) metrics.leftSideBearing);
            gy = dest_y + (int) lroundf(baseline_y + (float) metrics.yOffset);

            for (y = 0; y < metrics.minHeight; ++y) {
                int dy = gy + y;
                if (dy < 0 || dy >= tile_h) {
                    continue;
                }
                for (x = 0; x < metrics.minWidth; ++x) {
                    int dx = gx + x;
                    uint8_t coverage;
                    unsigned int src_a;
                    size_t dst_idx;
                    unsigned int inv_a;

                    if (dx < 0 || dx >= tile_w) {
                        continue;
                    }

                    coverage = glyph_pixels[y * metrics.minWidth + x];
                    if (coverage == 0u) {
                        continue;
                    }

                    src_a = ((unsigned int) coverage * (unsigned int) ca) / 255u;
                    if (src_a == 0u) {
                        continue;
                    }

                    dst_idx = (size_t) (dy * tile_w + dx) * 4u;
                    inv_a = 255u - src_a;
                    pixels[dst_idx + 0] = (uint8_t) ((cr * src_a + pixels[dst_idx + 0] * inv_a) / 255u);
                    pixels[dst_idx + 1] = (uint8_t) ((cg * src_a + pixels[dst_idx + 1] * inv_a) / 255u);
                    pixels[dst_idx + 2] = (uint8_t) ((cb * src_a + pixels[dst_idx + 2] * inv_a) / 255u);
                    pixels[dst_idx + 3] = (uint8_t) (src_a + (pixels[dst_idx + 3] * inv_a) / 255u);
                }
            }
        }

        free(glyph_pixels);
        pen_x += g->x_advance;
    }
}

void q_shaped_run_free(q_shaped_run_t *run)
{
    if (run == NULL) {
        return;
    }

    free(run->glyphs);
    free(run);
}
