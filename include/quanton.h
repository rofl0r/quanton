#ifndef QUANTON_H
#define QUANTON_H

#include <stddef.h>
#include <stdint.h>

/* Forward declarations for lexbor types used by the shim API. */
typedef struct lxb_html_document lxb_html_document_t;
typedef struct lxb_dom_node lxb_dom_node_t;
typedef struct lxb_css_rule_declaration lxb_css_rule_declaration_t;

typedef struct q_document q_document_t;
typedef struct q_box q_box_t;
typedef struct q_font q_font_t;
typedef struct q_font_cache q_font_cache_t;

typedef struct q_glyph {
    uint32_t codepoint;
    float x_advance;
    float x_offset;
    float y_offset;
} q_glyph_t;

typedef struct q_shaped_run {
    q_glyph_t *glyphs;
    size_t count;
    float total_advance;
    float ascender;
    float descender;
    float line_gap;
    q_font_t *font;
} q_shaped_run_t;

/* task 1: resource loader */
uint8_t *q_resource_load(const char *url, size_t *out_len);
void q_resource_free(uint8_t *buf);

/* task 2: lexbor integration shim */
q_document_t *q_document_create(void);
void q_document_destroy(q_document_t *doc);
int q_document_load_url(q_document_t *doc, const char *url);
int q_document_load_html(q_document_t *doc, const char *html, size_t len, const char *base_url);
lxb_html_document_t *q_document_handle(q_document_t *doc);
const lxb_css_rule_declaration_t *q_document_get_computed_style(const q_document_t *doc,
                                                                const lxb_dom_node_t *node);

/* task 4: layout pass 1 box tree builder */
typedef enum q_box_type {
    Q_BOX_BLOCK,
    Q_BOX_TEXT
} q_box_type_t;

struct q_box {
    q_box_type_t type;
    float x;
    float y;
    float width;
    float height;
    lxb_dom_node_t *dom_node;
    struct q_box *parent;
    struct q_box *first_child;
    struct q_box *last_child;
    struct q_box *next_sibling;
    struct q_box *prev_sibling;
    const char *text;
    size_t text_len;
    q_shaped_run_t *run;
    float border_width[4];
    uint32_t border_color[4];
    uint32_t background_color;
    uint8_t *tile;
    int tile_w;
    int tile_h;
};

q_box_t *q_layout_build_tree(q_document_t *doc);
void q_layout_free_tree(q_box_t *root);
void q_layout_measure(q_box_t *box, float containing_w, float containing_h);
void q_layout_position(q_box_t *box, float origin_x, float origin_y);
void q_paint_box(q_box_t *box);
void q_paint_fill_rect(uint8_t *pixels, int buf_w, int buf_h,
                       int x, int y, int w, int h, uint32_t color);
void q_paint_borders(q_box_t *box);
void q_paint_composite(uint8_t *dst, int dst_w, int dst_h,
                       const uint8_t *src, int src_w, int src_h,
                       int dx, int dy);

/* task 3: font cache + libschrift wrapper surface */
q_font_cache_t *q_font_cache_create(void);
void q_font_cache_destroy(q_font_cache_t *cache);
q_font_t *q_font_load(q_font_cache_t *cache,
                      const char *family_name,
                      const char *ttf_path,
                      float size_px,
                      int weight);
q_font_t *q_font_load_mem(q_font_cache_t *cache,
                          const char *family_name,
                          const void *data,
                          size_t len,
                          float size_px,
                          int weight);
q_font_t *q_font_match(q_font_cache_t *cache,
                       const char *family_name,
                       float size_px,
                       int weight);
float q_font_measure(q_font_t *font, const char *text, size_t len);
q_shaped_run_t *q_font_shape_run(q_font_t *font, const char *text, size_t len);
void q_shaped_run_free(q_shaped_run_t *run);

#endif
