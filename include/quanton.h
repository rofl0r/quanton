#ifndef QUANTON_H
#define QUANTON_H

#include <stddef.h>
#include <stdint.h>

/* Forward declarations for lexbor types used by the shim API. */
typedef struct lxb_html_document lxb_html_document_t;
typedef struct lxb_dom_node lxb_dom_node_t;
typedef struct lxb_css_rule_declaration lxb_css_rule_declaration_t;

typedef struct q_document    q_document_t;
typedef struct q_box         q_box_t;
typedef struct q_font        q_font_t;
typedef struct q_font_cache  q_font_cache_t;
typedef struct quanton_ctx   quanton_ctx_t;
typedef struct quanton_view  quanton_view_t;

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

/* ── Event system ── */
typedef enum q_event_type {
    Q_EVENT_MOUSE_MOVE,
    Q_EVENT_MOUSE_DOWN,
    Q_EVENT_MOUSE_UP,
    Q_EVENT_MOUSE_CLICK,
    Q_EVENT_MOUSE_WHEEL,
    Q_EVENT_KEY_DOWN,
    Q_EVENT_KEY_UP,
    Q_EVENT_RESIZE,
    Q_EVENT_CLOSE,
    Q_EVENT_REDRAW,
} q_event_type_t;

typedef struct q_event {
    q_event_type_t  type;
    int             mouse_x, mouse_y;
    int             mouse_button;   /* 0=left 1=mid 2=right */
    int             wheel_delta;
    uint32_t        key_sym;
    uint32_t        key_mod;        /* shift=1 ctrl=2 alt=4 */
    int             new_width, new_height;
    lxb_dom_node_t *target;         /* deepest DOM node at mouse pos */
    q_box_t        *target_box;
} q_event_t;

typedef void (*q_event_handler_fn)(quanton_view_t *view,
                                   const q_event_t *event,
                                   void *userdata);

/* ── Backend vtable ── */
typedef struct q_backend_vt {
    /* create native window; fills view->window_handle */
    int  (*create_window)(quanton_view_t *view, int w, int h, const char *title);
    /* blit view->framebuffer to screen */
    void (*blit)(quanton_view_t *view);
    /* non-blocking event poll; calls view->on_event for each event */
    void (*poll_events)(quanton_view_t *view);
    /* destroy window */
    void (*destroy_window)(quanton_view_t *view);
} q_backend_vt_t;

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
    Q_BOX_TEXT,
    Q_BOX_INLINE_CONTAINER, /* anonymous block wrapping consecutive inline content */
    Q_BOX_LINE              /* one wrapped line inside an inline container */
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

/* ── Application context (one per process) ── */
struct quanton_ctx {
    q_font_cache_t       *font_cache;
    const q_backend_vt_t *backend;
    void                 *backend_ctx;  /* opaque, owned by backend */
};

/* ── View (≈ Electron BrowserWindow) ── */
struct quanton_view {
    quanton_ctx_t      *ctx;
    q_document_t       *document;
    q_box_t            *layout_root;
    int                 vp_width, vp_height;
    uint8_t            *framebuffer;        /* RGBA8, vp_width × vp_height */
    q_event_handler_fn  on_event;
    void               *on_event_userdata;
    void               *window_handle;     /* opaque backend-specific handle */
    int                 should_close;
};

q_box_t *q_layout_build_tree(q_document_t *doc);
void q_layout_free_tree(q_box_t *root);
void q_layout_measure(q_box_t *box, float containing_w, float containing_h);
void q_layout_position(q_box_t *box, float origin_x, float origin_y);
void q_layout_line_wrap(q_box_t *inline_container);
void q_paint_box(q_box_t *box);
void q_paint_fill_rect(uint8_t *pixels, int buf_w, int buf_h,
                       int x, int y, int w, int h, uint32_t color);
void q_paint_borders(q_box_t *box);
void q_paint_composite(uint8_t *dst, int dst_w, int dst_h,
                       const uint8_t *src, int src_w, int src_h,
                       int dx, int dy);

q_box_t *q_hit_test(q_box_t *root, int x, int y);
void q_event_dispatch(quanton_view_t *view, q_event_t *event);
lxb_dom_node_t *q_event_find_delegate(lxb_dom_node_t *node, const char *attr_name);

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
void q_font_render_run(const q_shaped_run_t *run,
                       uint32_t color,
                       uint8_t *pixels,
                       int tile_w, int tile_h,
                       int dest_x, int dest_y);
void q_shaped_run_free(q_shaped_run_t *run);

/* ── Compositor ── */

/*
 * Walk the layout tree in paint order, blit all box tiles into
 * view->framebuffer.  Must be called after q_paint_box(view->layout_root).
 * Allocates view->framebuffer if it is NULL.
 */
void q_composite_frame(quanton_view_t *view);

/* ── Backend vtable instances ── */
extern const q_backend_vt_t q_backend_x11;
extern const q_backend_vt_t q_backend_sdl2;
extern const q_backend_vt_t q_backend_png;

#endif
