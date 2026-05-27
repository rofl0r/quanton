#ifndef QUANTON_H
#define QUANTON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations for lexbor types used by the shim API. */
typedef struct lxb_html_document lxb_html_document_t;
typedef struct lxb_dom_node lxb_dom_node_t;
typedef struct lxb_dom_element lxb_dom_element_t;
typedef struct lxb_css_rule_declaration lxb_css_rule_declaration_t;

typedef struct q_document    q_document_t;
typedef struct q_box         q_box_t;
typedef struct q_font        q_font_t;
typedef struct q_font_cache  q_font_cache_t;
typedef struct q_image       q_image_t;
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
char *q_url_resolve(const char *base_url, const char *ref);
uint8_t *q_resource_load(const char *url, size_t *out_len);
void q_resource_free(uint8_t *buf);

/* task 2: lexbor integration shim */
q_document_t *q_document_create(void);
void q_document_destroy(q_document_t *doc);
int q_document_load_url(q_document_t *doc, const char *url);
int q_document_load_html(q_document_t *doc, const char *html, size_t len, const char *base_url);
lxb_html_document_t *q_document_handle(q_document_t *doc);
const char *q_document_base_url(const q_document_t *doc);
const lxb_css_rule_declaration_t *q_document_get_computed_style(const q_document_t *doc,
                                                                const lxb_dom_node_t *node);

/* task 4: layout pass 1 box tree builder */
typedef enum q_box_type {
    Q_BOX_BLOCK,
    Q_BOX_IMAGE,
    Q_BOX_TEXT,
    Q_BOX_INLINE_CONTAINER, /* anonymous block wrapping consecutive inline content */
    Q_BOX_LINE              /* one wrapped line inside an inline container */
} q_box_type_t;

/* CSS position property values (Q_POSITION_STATIC == 0 matches calloc zero) */
typedef enum q_position_type {
    Q_POSITION_STATIC   = 0,
    Q_POSITION_RELATIVE = 1,
    Q_POSITION_ABSOLUTE = 2,
    Q_POSITION_FIXED    = 3
} q_position_type_t;

/* CSS overflow property values (Q_OVERFLOW_VISIBLE == 0 matches calloc zero) */
typedef enum q_overflow_type {
    Q_OVERFLOW_VISIBLE = 0,
    Q_OVERFLOW_HIDDEN  = 1,
    Q_OVERFLOW_SCROLL  = 2,
    Q_OVERFLOW_AUTO    = 3,
    Q_OVERFLOW_CLIP    = 4,
} q_overflow_type_t;

struct q_box {
    q_box_type_t     type;
    int              is_flex_container;
    q_position_type_t position;  /* CSS position: static/relative/absolute/fixed */
    int              has_z_index; /* 1 when z-index explicitly set */
    int              z_index;     /* CSS z-index value */
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
    q_image_t *image;
    float border_width[4];
    uint32_t border_color[4];
    uint32_t background_color;
    uint8_t *tile;
    int tile_w;
    int tile_h;
    /* CSS overflow clipping */
    q_overflow_type_t overflow_x;   /* Q_OVERFLOW_VISIBLE = default (calloc zero) */
    q_overflow_type_t overflow_y;
    /* Explicit CSS dimensions / offsets (NaN = not set, use normal flow) */
    float style_top;
    float style_right;
    float style_bottom;
    float style_left;
    float style_width;
    float style_height;
};

/* ── Dirty flags for incremental relayout ── */
typedef enum q_dirty_flags {
    Q_DIRTY_STYLE  = 1 << 0,  /* recompute computed styles */
    Q_DIRTY_LAYOUT = 1 << 1,  /* rebuild box tree + measure */
    Q_DIRTY_PAINT  = 1 << 2,  /* repaint tiles */
} q_dirty_flags_t;

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
    q_dirty_flags_t     dirty_flags;       /* accumulated dirty bits */
};

q_box_t *q_layout_build_tree(q_document_t *doc);
void q_layout_free_tree(q_box_t *root);
void q_layout_measure(q_box_t *box, float containing_w, float containing_h);
void q_layout_position(q_box_t *box, float origin_x, float origin_y);
void q_layout_position_absolute(q_box_t *root);
void q_layout_line_wrap(q_box_t *inline_container);
void q_paint_box(q_box_t *box);
void q_paint_fill_rect(uint8_t *pixels, int buf_w, int buf_h,
                       int x, int y, int w, int h, uint32_t color);
void q_paint_borders(q_box_t *box);
void q_paint_composite(uint8_t *dst, int dst_w, int dst_h,
                       const uint8_t *src, int src_w, int src_h,
                       int dx, int dy);
/* Like q_paint_composite but additionally clips painted pixels to the
 * rectangle [clip_x, clip_x+clip_w) × [clip_y, clip_y+clip_h) on dst.
 * Used to implement overflow:hidden / overflow:clip. */
void q_paint_composite_clipped(uint8_t *dst, int dst_w, int dst_h,
                                const uint8_t *src, int src_w, int src_h,
                                int dx, int dy,
                                int clip_x, int clip_y, int clip_w, int clip_h);

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

/* ── Image cache / decoder ── */
q_image_t *q_image_load_url(const char *url);
void q_image_release(q_image_t *image);
const uint8_t *q_image_pixels(const q_image_t *image);
int q_image_width(const q_image_t *image);
int q_image_height(const q_image_t *image);

/* ── Compositor ── */

/*
 * Walk the layout tree in paint order, blit all box tiles into
 * view->framebuffer.  Must be called after q_paint_box(view->layout_root).
 * Allocates view->framebuffer if it is NULL.
 */
void q_composite_frame(quanton_view_t *view);

/* ── View update (dirty-flag processing) ── */

/*
 * Mark a DOM subtree dirty; will be processed on the next q_view_update().
 * Passing NULL for node marks the entire document dirty.
 */
void q_dom_mark_dirty(quanton_view_t *view,
                      lxb_dom_node_t *node,
                      q_dirty_flags_t flags);

/*
 * Process all pending dirty flags: rebuild box tree, re-measure, re-position,
 * repaint and composite as needed.  Does nothing if dirty_flags == 0.
 */
void q_view_update(quanton_view_t *view);

/*
 * Force a synchronous full relayout + repaint, regardless of dirty state.
 */
void q_view_refresh(quanton_view_t *view);

/* ── DOM mutation helpers ── */

/* Set an attribute on an element; schedules Q_DIRTY_LAYOUT. */
int q_dom_set_attr(quanton_view_t *view,
                   lxb_dom_element_t *el,
                   const char *name, const char *value);

/* Remove an attribute; schedules Q_DIRTY_LAYOUT. */
int q_dom_remove_attr(quanton_view_t *view,
                      lxb_dom_element_t *el,
                      const char *name);

/*
 * Set element.textContent — replaces all children with a single text node;
 * schedules Q_DIRTY_LAYOUT.
 */
int q_dom_set_text_content(quanton_view_t *view,
                            lxb_dom_element_t *el,
                            const char *text, size_t len);

/* Append a new child element; schedules Q_DIRTY_LAYOUT. */
lxb_dom_element_t *q_dom_append_element(quanton_view_t *view,
                                         lxb_dom_element_t *parent,
                                         const char *tag_name);

/* Remove a node from the tree; schedules Q_DIRTY_LAYOUT. */
int q_dom_remove_node(quanton_view_t *view, lxb_dom_node_t *node);

/* CSS class helpers (built on q_dom_set_attr). */
void q_dom_add_class(quanton_view_t *view,
                     lxb_dom_element_t *el, const char *cls);
void q_dom_remove_class(quanton_view_t *view,
                        lxb_dom_element_t *el, const char *cls);
bool q_dom_has_class(lxb_dom_element_t *el, const char *cls);

/* querySelector using lexbor's selectors module. */
lxb_dom_element_t *q_dom_query_selector(quanton_view_t *view,
                                         const char *selector);

/* querySelectorAll — fills out[], returns match count. */
size_t q_dom_query_selector_all(quanton_view_t *view,
                                 const char *selector,
                                 lxb_dom_element_t **out, size_t out_max);

/* ── Backend vtable instances ── */
extern const q_backend_vt_t q_backend_x11;
extern const q_backend_vt_t q_backend_sdl2;
extern const q_backend_vt_t q_backend_png;

#endif
