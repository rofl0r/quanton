#ifndef QUANTON_H
#define QUANTON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "lexbor/core/types.h"

/* Forward declarations for lexbor types used by the shim API. */
typedef struct lxb_html_document lxb_html_document_t;
typedef struct lxb_dom_node lxb_dom_node_t;
typedef struct lxb_dom_element lxb_dom_element_t;
typedef struct lxb_css_rule_declaration lxb_css_rule_declaration_t;

typedef struct q_document    q_document_t;
typedef struct q_box         q_box_t;
typedef struct q_table       q_table_t;
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
    Q_EVENT_FOCUS,
    Q_EVENT_BLUR,
    Q_EVENT_CHANGE,
} q_event_type_t;

typedef struct q_event {
    q_event_type_t  type;
    int             mouse_x, mouse_y;
    int             mouse_button;   /* 0=left 1=mid 2=right */
    int             wheel_delta;
    uint32_t        key_sym;
    uint32_t        key_mod;        /* shift=1 ctrl=2 alt=4 */
    int             key_repeat;     /* >=1 for coalesced keydown repeats */
    int             new_width, new_height;
    lxb_dom_node_t *target;         /* deepest DOM node at mouse pos */
    q_box_t        *target_box;
} q_event_t;

/*
 * Normalized key codes carried in q_event_t.key_sym for KEY_DOWN/KEY_UP
 * events. Backends are responsible for translating their own native key
 * codes (X11 KeySym, SDL_Keycode, ...) into these values before dispatch;
 * printable ASCII characters (0x20..0x7E) are passed through unchanged.
 */
#define Q_KEY_BACKSPACE 0x08u
#define Q_KEY_DELETE    0x7Fu
#define Q_KEY_LEFT      0x1001u
#define Q_KEY_RIGHT     0x1002u
#define Q_KEY_UP        0x1003u
#define Q_KEY_DOWN      0x1004u
#define Q_KEY_PAGEUP    0x1005u
#define Q_KEY_PAGEDOWN  0x1006u
#define Q_KEY_HOME      0x1007u
#define Q_KEY_END       0x1008u
#define Q_KEY_ENTER     0x0Du

typedef void (*q_event_handler_fn)(quanton_view_t *view,
                                   const q_event_t *event,
                                   void *userdata);

/* ── Backend vtable ── */
typedef struct q_backend_vt {
    /* create native window; fills view->window_handle */
    int  (*create_window)(quanton_view_t *view, int w, int h, const char *title);
    /* optional direct renderer from box tiles (used by GPU backends) */
    void (*render_view)(quanton_view_t *view);
    /* blit view->framebuffer to screen */
    void (*blit)(quanton_view_t *view);
    /* non-blocking event poll; calls view->on_event for each event */
    void (*poll_events)(quanton_view_t *view);
    /* destroy window */
    void (*destroy_window)(quanton_view_t *view);
    /* update native window title */
    void (*set_title)(quanton_view_t *view, const char *title);
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
    Q_BOX_LINE_BREAK,
    Q_BOX_INLINE_CONTAINER, /* anonymous block wrapping consecutive inline content */
    Q_BOX_LINE,             /* one wrapped line inside an inline container */
    Q_BOX_TABLE,
    Q_BOX_TABLE_SECTION,
    Q_BOX_TABLE_ROW,
    Q_BOX_TABLE_CELL,
    Q_BOX_TABLE_CAPTION
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

typedef enum q_float_type {
    Q_FLOAT_NONE  = 0,
    Q_FLOAT_LEFT  = 1,
    Q_FLOAT_RIGHT = 2,
} q_float_type_t;

typedef enum q_widget_type {
    Q_WIDGET_NONE         = 0,
    Q_WIDGET_INPUT_TEXT   = 1,
    Q_WIDGET_INPUT_SUBMIT = 2,
    Q_WIDGET_INPUT_CHECK  = 3,
    Q_WIDGET_INPUT_RADIO  = 4,
    Q_WIDGET_BUTTON       = 5,
    Q_WIDGET_SELECT       = 6,
    Q_WIDGET_TEXTAREA     = 7,
} q_widget_type_t;

typedef enum q_clear_type {
    Q_CLEAR_NONE  = 0,
    Q_CLEAR_LEFT  = 1,
    Q_CLEAR_RIGHT = 2,
    Q_CLEAR_BOTH  = 3,
} q_clear_type_t;

typedef enum q_white_space_type {
    Q_WHITE_SPACE_NORMAL = 0,
    Q_WHITE_SPACE_NOWRAP = 1,
    Q_WHITE_SPACE_PRE    = 2,
} q_white_space_type_t;

typedef enum q_vertical_align_type {
    Q_VERTICAL_ALIGN_BASELINE = 0,
    Q_VERTICAL_ALIGN_TOP      = 1,
    Q_VERTICAL_ALIGN_MIDDLE   = 2,
    Q_VERTICAL_ALIGN_BOTTOM   = 3,
    Q_VERTICAL_ALIGN_SUB      = 4,
    Q_VERTICAL_ALIGN_SUPER    = 5,
} q_vertical_align_type_t;

typedef enum q_text_align_type {
    Q_TEXT_ALIGN_LEFT   = 0,
    Q_TEXT_ALIGN_CENTER = 1,
    Q_TEXT_ALIGN_RIGHT  = 2,
} q_text_align_type_t;

typedef enum q_background_repeat_type {
    Q_BACKGROUND_REPEAT_REPEAT   = 0,
    Q_BACKGROUND_REPEAT_NO_REPEAT = 1,
    Q_BACKGROUND_REPEAT_REPEAT_X = 2,
    Q_BACKGROUND_REPEAT_REPEAT_Y = 3,
} q_background_repeat_type_t;

typedef enum q_list_style_type {
    Q_LIST_STYLE_NONE = 0,
    Q_LIST_STYLE_DISC = 1,
    Q_LIST_STYLE_DECIMAL = 2,
} q_list_style_type_t;

typedef enum q_font_style {
    Q_FONT_STYLE_NORMAL  = 0,
    Q_FONT_STYLE_ITALIC  = 1,
    Q_FONT_STYLE_OBLIQUE = 2,
} q_font_style_t;

#define Q_TEXT_DECORATION_UNDERLINE    (1u << 0)
#define Q_TEXT_DECORATION_OVERLINE     (1u << 1)
#define Q_TEXT_DECORATION_LINE_THROUGH (1u << 2)

typedef struct q_float_entry {
    struct q_box       *box;
    struct q_float_entry *next;
} q_float_entry_t;

typedef struct q_float_ctx {
    q_float_entry_t *left_floats;
    q_float_entry_t *right_floats;
} q_float_ctx_t;

/* ── Table layout data ── */
typedef struct q_table_col {
    float min_width;
    float max_width;
    float final_width;
    float pct_hint;     /* percentage-width hint (0 = none) */
} q_table_col_t;

typedef struct q_table_row {
    float    height;
    q_box_t *box;
} q_table_row_t;

typedef struct q_table_span {
    int      row;
    int      col;
    int      rowspan;
    int      colspan;
    q_box_t *cell_box;
} q_table_span_t;

struct q_table {
    int             col_count;
    int             row_count;
    q_table_col_t  *cols;
    q_table_row_t  *rows;
    q_table_span_t *spans;
    int             span_count;
    int             border_collapse;
    float           border_spacing;  /* CSS border-spacing (0 when collapsed) */
};

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
    float border_radius[4];
    uint32_t background_color;
    q_image_t *background_image;
    q_background_repeat_type_t background_repeat;
    uint8_t *tile;
    int tile_w;
    int tile_h;
    uint8_t *self_tile;            /* box-only tile (without composited children) */
    int self_tile_w;
    int self_tile_h;
    uint64_t self_tile_revision;   /* incremented each time self_tile contents change */
    /* CSS overflow clipping */
    q_overflow_type_t overflow_x;   /* Q_OVERFLOW_VISIBLE = default (calloc zero) */
    q_overflow_type_t overflow_y;
    float scroll_x;
    float scroll_y;
    /* Explicit CSS dimensions / offsets (NaN = not set, use normal flow) */
    float style_top;
    float style_right;
    float style_bottom;
    float style_left;
    float style_width;
    float style_width_pct;   /* percentage width (NaN = not set) */
    float style_height;
    float style_min_width;
    float style_max_width;
    float style_min_height;
    float style_max_height;
    /* CSS box model spacing */
    float margin_top;
    float margin_right;
    float margin_bottom;
    float margin_left;
    int margin_right_auto;
    int margin_left_auto;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    float flex_gap;
    q_list_style_type_t list_style_type;
    int list_item_index;
    q_float_type_t float_type;
    q_clear_type_t clear_type;
    q_white_space_type_t white_space;
    q_text_align_type_t text_align;
    q_vertical_align_type_t vertical_align;
    uint8_t text_decoration;
    float font_size;          /* NaN = inherit / default */
    int font_weight;          /* 0 = inherit / default */
    q_font_style_t font_style; /* Q_FONT_STYLE_NORMAL=0 = inherit / default */
    const char *font_family;  /* NULL = inherit "sans-serif"; "monospace" = monospace */
    uint32_t text_color;      /* valid when has_text_color != 0 */
    int has_text_color;
    int is_inline_block;
    int table_border_collapse;     /* 1 when border-collapse: collapse */
    float table_border_spacing;    /* CSS border-spacing for TABLE boxes (default 2) */
    char *document_title;          /* only used on root box */
    char *href;                    /* NULL or malloc'd href from <a href="..."> */
    q_widget_type_t widget_type;
    int widget_focused;
    char *widget_value;
    size_t widget_value_len;
    size_t widget_caret;
    size_t widget_sel_anchor;
    size_t widget_sel_focus;
    size_t text_sel_anchor;
    size_t text_sel_focus;
    float widget_scroll_x;
    float widget_scroll_y;
    int widget_checked;
    int widget_pressed;
    int widget_open;
    struct q_table *table;   /* non-NULL for Q_BOX_TABLE after measure */
};

/* ── Dirty flags for incremental relayout ── */
typedef enum q_dirty_flags {
    Q_DIRTY_STYLE  = 1 << 0,  /* recompute computed styles */
    Q_DIRTY_LAYOUT = 1 << 1,  /* rebuild box tree + measure */
    Q_DIRTY_PAINT  = 1 << 2,  /* repaint tiles */
    Q_DIRTY_SCROLL = 1 << 3,  /* composite-only redraw after scrolling */
    Q_DIRTY_RECOMPOSE = 1 << 4, /* reuse existing child tiles and repaint only the cached viewport */
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
    float               scroll_x, scroll_y;
    float               doc_width, doc_height;
    uint8_t            *framebuffer;        /* RGBA8, vp_width × vp_height */
    q_event_handler_fn  on_event;
    void               *on_event_userdata;
    /* Called when the user clicks an <a href="..."> that is NOT a named anchor.
     * Named anchors (href="#id") are handled internally by scrolling. */
    void              (*on_navigate)(quanton_view_t *view,
                                     const char *href,
                                     void *userdata);
    void               *on_navigate_userdata;
    q_box_t            *focused_widget;   /* currently focused interactive box */
    q_box_t            *active_scroll_box; /* last activated scrollable pane */
    q_box_t            *mouse_select_box;  /* active text-selection drag source */
    q_box_t            *mouse_text_select_box; /* read-only text selection drag source */
    int                 mouse_text_cursor; /* 1 when pointer should be I-beam */
    char               *clipboard_text;    /* internal clipboard buffer */
    size_t              texture_cache_limit_bytes; /* 0 = backend default */
    void               *window_handle;     /* opaque backend-specific handle */
    q_box_t            *drag_scroll_box;   /* active scrollbar-drag target */
    int                 drag_scroll_vertical;
    int                 drag_scroll_last_mouse;
    int                 defer_updates;      /* when set, event dispatch only marks dirty */
    int                 should_close;
    q_dirty_flags_t     dirty_flags;       /* accumulated dirty bits */
};

q_box_t *q_layout_build_tree(q_document_t *doc);
void q_layout_free_tree(q_box_t *root);
const lxb_char_t *q_dom_get_attribute(quanton_view_t *view,
                                      lxb_dom_element_t *el,
                                      const char *name,
                                      size_t *out_len);
void q_layout_measure(q_box_t *box, float containing_w, float containing_h);
void q_layout_position(q_box_t *box, float origin_x, float origin_y);
void q_layout_position_absolute(q_box_t *root);
void q_layout_line_wrap(q_box_t *inline_container);
void q_table_fixup_anonymous(q_box_t *root);
void q_table_measure(q_box_t *table_box, float containing_w);
void q_table_position(q_box_t *table_box, float origin_x, float origin_y);
void q_table_free(q_table_t *t);
float q_float_ctx_left_edge(const q_float_ctx_t *ctx, float y, float line_h);
float q_float_ctx_right_edge(const q_float_ctx_t *ctx, float y, float line_h, float containing_w);
float q_float_ctx_clear_y(const q_float_ctx_t *ctx, q_clear_type_t clear);
float q_float_ctx_next_y(const q_float_ctx_t *ctx, float y, float line_h);
int q_float_ctx_add(q_float_ctx_t *ctx, q_box_t *float_box, q_float_type_t side);
void q_float_ctx_reset(q_float_ctx_t *ctx);
void q_paint_box(q_box_t *box);
void q_paint_box_cached(q_box_t *box);
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
                       int weight,
                       int style);
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
void q_view_scroll_by(quanton_view_t *view, float dx, float dy);
void q_view_scroll_to(quanton_view_t *view, float x, float y);
void q_view_scroll_into_view(quanton_view_t *view, const q_box_t *box);
void q_view_set_texture_cache_limit(quanton_view_t *view, size_t bytes);
size_t q_view_get_texture_cache_limit(const quanton_view_t *view);

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

/* getElementById — finds the first element with the given id attribute. */
lxb_dom_element_t *q_dom_get_element_by_id(quanton_view_t *view,
                                             const char *id);

/*
 * innerHTML setter — parses html as a fragment in el's context, replaces
 * el's children with the result, and marks the view Q_DIRTY_LAYOUT.
 * Returns 0 on success, -1 on failure.
 */
int q_dom_set_inner_html(quanton_view_t *view,
                          lxb_dom_element_t *el,
                          const char *html, size_t len);

/* ── Backend vtable instances ── */
extern const q_backend_vt_t q_backend_x11;
extern const q_backend_vt_t q_backend_sdl2;
extern const q_backend_vt_t q_backend_png;

#endif
