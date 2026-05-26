#include "quanton.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q_DEFAULT_FONT_PATH "/usr/share/fonts/dejavu/DejaVuSans.ttf"

struct q_font {
    char *family;
    char *path;
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

static char *q_strdup(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }

    len = strlen(s);
    copy = (char *) malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, s, len + 1);
    return copy;
}

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

static uint8_t *q_read_file(const char *path, size_t *out_len)
{
    FILE *fp;
    long file_len;
    uint8_t *buf;
    size_t read_len;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    file_len = ftell(fp);
    if (file_len < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buf = (uint8_t *) malloc((size_t) file_len);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    read_len = fread(buf, 1, (size_t) file_len, fp);
    fclose(fp);
    if (read_len != (size_t) file_len) {
        free(buf);
        return NULL;
    }

    if (out_len != NULL) {
        *out_len = read_len;
    }
    return buf;
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
        if (font->weight == weight && fabsf(font->size_px - size_px) < 0.01f &&
            strcmp(font->family, family) == 0 && strcmp(font->path, path) == 0) {
            return font;
        }
    }

    return NULL;
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
        q_font_t *font = cache->entries[i];
        free(font->family);
        free(font->path);
        free(font->font_data);
        free(font);
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
    path = (ttf_path != NULL) ? ttf_path : Q_DEFAULT_FONT_PATH;

    font = q_find_font(cache, family, path, size_px, weight);
    if (font != NULL) {
        return font;
    }

    font = (q_font_t *) calloc(1, sizeof(*font));
    if (font == NULL) {
        return NULL;
    }

    font->family = q_strdup(family);
    font->path = q_strdup(path);
    font->font_data = q_read_file(path, &font->font_len);
    font->size_px = size_px;
    font->weight = weight;

    if (font->family == NULL || font->path == NULL || font->font_data == NULL) {
        free(font->family);
        free(font->path);
        free(font->font_data);
        free(font);
        return NULL;
    }

    if (q_cache_reserve(cache, cache->count + 1) != 0) {
        free(font->family);
        free(font->path);
        free(font->font_data);
        free(font);
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

    font->family = q_strdup(family);
    font->path = q_strdup("memory://font");
    font->font_data = (uint8_t *) malloc(len);
    if (font->family == NULL || font->path == NULL || font->font_data == NULL) {
        free(font->family);
        free(font->path);
        free(font->font_data);
        free(font);
        return NULL;
    }

    memcpy(font->font_data, data, len);
    font->font_len = len;
    font->size_px = size_px;
    font->weight = weight;

    if (q_cache_reserve(cache, cache->count + 1) != 0) {
        free(font->family);
        free(font->path);
        free(font->font_data);
        free(font);
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
        if ((family_name == NULL || strcmp(font->family, family_name) == 0) &&
            font->weight == weight && fabsf(font->size_px - size_px) < 0.01f) {
            return font;
        }
    }

    return q_font_load(cache,
                       (family_name != NULL) ? family_name : "sans-serif",
                       Q_DEFAULT_FONT_PATH,
                       size_px,
                       weight);
}

static size_t q_utf8_codepoint_count(const char *text, size_t len)
{
    size_t i;
    size_t count = 0;

    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char) text[i];
        if ((c & 0xC0U) != 0x80U) {
            ++count;
        }
    }

    return count;
}

float q_font_measure(q_font_t *font, const char *text, size_t len)
{
    size_t i;
    float advance = 0.0f;

    if (font == NULL || text == NULL) {
        return 0.0f;
    }

    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char) text[i];
        if ((c & 0xC0U) == 0x80U) {
            continue;
        }

        if (c == ' ') {
            advance += font->size_px * 0.33f;
        } else {
            advance += font->size_px * 0.60f;
        }
    }

    return advance;
}

q_shaped_run_t *q_font_shape_run(q_font_t *font, const char *text, size_t len)
{
    q_shaped_run_t *run;
    size_t glyph_count;
    size_t i;
    size_t g = 0;

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
    run->ascender = font->size_px * 0.8f;
    run->descender = -font->size_px * 0.2f;
    run->line_gap = font->size_px * 0.2f;

    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char) text[i];
        if ((c & 0xC0U) == 0x80U) {
            continue;
        }

        run->glyphs[g].codepoint = c;
        run->glyphs[g].x_advance = (c == ' ') ? font->size_px * 0.33f : font->size_px * 0.60f;
        run->total_advance += run->glyphs[g].x_advance;
        ++g;
    }

    return run;
}

void q_shaped_run_free(q_shaped_run_t *run)
{
    if (run == NULL) {
        return;
    }

    free(run->glyphs);
    free(run);
}
