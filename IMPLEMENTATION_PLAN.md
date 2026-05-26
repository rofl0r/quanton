# Quanton — Lightweight Electron Replacement in C
## Implementation Plan & API Specification

---

## 1. Guiding Principles

* **Reuse lexbor maximally.** DOM, CSS cascade, selector matching, HTML parsing, and CSS property structs are all provided. We write nothing that lexbor already does.
* **Every layout box keeps a pointer to its originating `lxb_dom_node_t`**; we never duplicate attribute storage.
* **Box-based rendering.** Each block box is a self-contained pixel tile. Tiles can be handed to a GPU as textures.
* **Swappable backend.** Window creation and event polling are behind a thin vtable; X11 and SDL2 are two implementations.
* **No network.** Only `file://` and `app://` (embedded resource) URL schemes are supported.

---

## 2. Module Map

```
quanton/
  src/
    resource/     -- file loader + embedded resource registry
    layout/       -- box tree, block/inline layout, text shaping
    paint/        -- rasterizer (fills, borders, text glyphs into RGBA tiles)
    composite/    -- assembles tiles into a viewport pixmap
    font/         -- libschrift wrapper (font cache, glyph cache, text measurement)
    event/        -- input event routing, hit testing, DOM event dispatch
    dom_api/      -- user-facing DOM mutation + query helpers
    backend/
      x11/        -- X11 window + event backend
      sdl2/       -- SDL2 window + event backend
    app/          -- top-level celerity_view lifecycle
  include/
    quanton.h    -- single public header for framework users
```

---

## 3. Data Structures

### 3.1 Application context

```c
/* celerity_ctx  — one per process */
typedef struct celerity_ctx {
    /* lexbor objects — own one document per view */
    /* font system */
    q_font_cache_t       *font_cache;
    /* backend vtable (filled in at init time) */
    const q_backend_vt_t *backend;
    void                   *backend_ctx;   /* opaque, owned by backend */
} celerity_ctx_t;
```

### 3.2 View (≈ Electron BrowserWindow)

```c
typedef struct celerity_view {
    celerity_ctx_t     *ctx;

    /* lexbor document */
    lxb_html_document_t *document;

    /* root of our layout tree (rebuilt on relayout) */
    struct q_box      *layout_root;

    /* viewport geometry */
    int                  vp_width, vp_height;

    /* composited frame buffer (RGBA8, vp_width × vp_height) */
    uint8_t             *framebuffer;

    /* user callbacks */
    q_event_handler_fn on_event;
    void                *on_event_userdata;

    /* backend window handle */
    void                *window_handle;
} celerity_view_t;
```

### 3.3 Computed style (thin wrapper — most data stays in lexbor)

We only cache the few things the layout engine computes repeatedly:

```c
typedef struct q_computed_style {
    /* back-pointer into lexbor's style system */
    const lxb_css_memory_t    *css_mem;   /* not owned */
    const lxb_css_rule_declaration_t *decl; /* not owned */

    /* resolved (px) values — filled by q_style_resolve() */
    float display;           /* Q_DISPLAY_BLOCK / INLINE / FLEX / NONE */
    float width_px;          /* NaN = auto */
    float height_px;         /* NaN = auto */
    float min_width_px, max_width_px;
    float min_height_px, max_height_px;
    float margin[4];         /* top right bottom left, px */
    float padding[4];
    float border_width[4];
    uint32_t border_color[4];/* RGBA8 packed */
    uint32_t background_color;
    uint32_t color;
    float font_size_px;
    float line_height_px;    /* NaN = normal → font-derived */
    int   font_weight;       /* 100–900 */
    int   text_align;        /* Q_TEXT_ALIGN_LEFT/CENTER/RIGHT/JUSTIFY */
    int   position;          /* Q_POS_STATIC/RELATIVE/ABSOLUTE/FIXED */
    float top_px, right_px, bottom_px, left_px;  /* NaN = auto */
    int   overflow_x, overflow_y;
    int   z_index;
    int   visibility;
    float opacity;
    /* flex */
    int   flex_direction, flex_wrap;
    float flex_grow, flex_shrink;
    /* font family list is queried on demand from lexbor */
} q_computed_style_t;
```

### 3.4 Layout box

```c
typedef enum {
    Q_BOX_BLOCK,
    Q_BOX_INLINE_CONTAINER,  /* anonymous block wrapping inline content */
    Q_BOX_LINE,              /* one wrapped line inside an inline container */
    Q_BOX_TEXT,              /* a run of shaped text inside a line */
    Q_BOX_IMAGE,             /* replaced element */
    Q_BOX_FLEX_CONTAINER,
    Q_BOX_FLEX_ITEM,
} q_box_type_t;

typedef struct q_box {
    q_box_type_t    type;

    /* geometry — all in document px, relative to parent content edge */
    float             x, y;          /* position of border box origin */
    float             width, height; /* border box */

    /* back-pointer to DOM — NEVER NULL (anonymous boxes point to parent) */
    lxb_dom_node_t   *dom_node;      /* not owned */

    /* back-pointer to computed style — NULL for anonymous boxes */
    q_computed_style_t *style;     /* not owned, lives in view pool */

    /* tree links */
    struct q_box   *parent;
    struct q_box   *first_child;
    struct q_box   *last_child;
    struct q_box   *next_sibling;
    struct q_box   *prev_sibling;

    /* rendering payload — type-specific */
    union {
        struct {
            /* Q_BOX_TEXT */
            const lxb_char_t *text;    /* pointer into DOM text node data */
            size_t            text_len;
            q_shaped_run_t *run;     /* glyph array, owned */
        } text;
        struct {
            /* Q_BOX_LINE */
            float baseline_y;          /* relative to line box top */
        } line;
        struct {
            /* Q_BOX_IMAGE */
            uint8_t          *pixels;  /* RGBA8, owned */
            int               img_w, img_h;
        } image;
    } u;

    /* paint tile (RGBA8, width×height, owned, NULL until paint pass) */
    uint8_t          *tile;
} q_box_t;
```

### 3.5 Shaped text run

```c
/* One glyph in a shaped run */
typedef struct {
    SFT_Glyph  glyph;
    float      x_advance;   /* px */
    float      x_offset;    /* for kerning/positioning */
    float      y_offset;
} q_glyph_t;

typedef struct q_shaped_run {
    q_glyph_t *glyphs;
    size_t       count;
    float        total_advance;  /* sum of x_advance */
    float        ascender;       /* from lmetrics, px */
    float        descender;
    float        line_gap;
    /* font handle used for rendering */
    q_font_t  *font;           /* not owned */
} q_shaped_run_t;
```

### 3.6 Font cache entry

```c
typedef struct q_font {
    SFT_Font    *sft_font;        /* owned */
    SFT         sft;              /* configured SFT context for a given px size */
    float        size_px;
    int          weight;          /* nominal — libschrift doesn't do synthetic bold */
    char        *family;          /* owned copy */
    /* glyph metric cache (open-addressing hash: SFT_Glyph → SFT_GMetrics) */
    /* ... implementation detail */
} q_font_t;

typedef struct q_font_cache {
    q_font_t  **entries;
    size_t        count, capacity;
} q_font_cache_t;
```

### 3.7 Backend vtable

```c
typedef struct q_backend_vt {
    /* create a native window, store handle in view->window_handle */
    int  (*create_window)(celerity_view_t *view, int w, int h, const char *title);
    /* blit view->framebuffer to screen */
    void (*blit)(celerity_view_t *view);
    /* poll events — calls view->on_event for each */
    void (*poll_events)(celerity_view_t *view);
    /* destroy window */
    void (*destroy_window)(celerity_view_t *view);
} q_backend_vt_t;

extern const q_backend_vt_t q_backend_x11;
extern const q_backend_vt_t q_backend_sdl2;
```

### 3.8 Input event

```c
typedef enum {
    Q_EVENT_MOUSE_MOVE,
    Q_EVENT_MOUSE_DOWN,
    Q_EVENT_MOUSE_UP,
    Q_EVENT_MOUSE_CLICK,
    Q_EVENT_MOUSE_WHEEL,
    Q_EVENT_KEY_DOWN,
    Q_EVENT_KEY_UP,
    Q_EVENT_RESIZE,
    Q_EVENT_CLOSE,
} q_event_type_t;

typedef struct {
    q_event_type_t  type;
    /* mouse */
    int               mouse_x, mouse_y;
    int               mouse_button;   /* 0=left,1=mid,2=right */
    int               wheel_delta;
    /* keyboard */
    uint32_t          key_sym;        /* X11 KeySym or SDL_Keycode */
    uint32_t          key_mod;        /* shift/ctrl/alt bitmask */
    /* resize */
    int               new_width, new_height;
    /* hit test — filled in by event router */
    lxb_dom_node_t   *target;         /* deepest box at mouse position */
    q_box_t        *target_box;
} q_event_t;

typedef void (*q_event_handler_fn)(celerity_view_t *view,
                                     const q_event_t *event,
                                     void *userdata);
```

---

## 4. Layout Engine — Algorithm & Functions

The layout engine runs in three ordered passes over the DOM, producing the box
tree and computing all geometry.

### 4.1 Pass 1 — Build box tree  `q_layout_build_tree()`

```c
/*
 * Recursively walk the lexbor DOM and create q_box_t nodes.
 * - For each lxb_dom_element_t, call lxb_style_resolve (lexbor engine module)
 *   to get the computed style, then wrap it in q_computed_style_t.
 * - display:none → skip subtree entirely.
 * - Inline-level nodes consecutive inside a block parent are grouped into
 *   an anonymous Q_BOX_INLINE_CONTAINER.
 * - Text nodes produce Q_BOX_TEXT leaves inside the inline container.
 */
q_box_t *q_layout_build_tree(celerity_view_t *view);
```

### 4.2 Pass 2 — Compute sizes  `q_layout_measure()`

Top-down width assignment, bottom-up height bubbling.

```c
/*
 * q_layout_measure(box, containing_width, containing_height)
 *
 * For BLOCK boxes:
 *   1. Resolve width:
 *      - explicit px → use it
 *      - percentage → containing_width * pct
 *      - auto → containing_width - margins - borders - paddings
 *   2. Recurse into children with new containing_width.
 *   3. After all children measured, compute own height:
 *      - explicit px → use it
 *      - auto → sum of children heights + padding + borders
 *
 * For INLINE_CONTAINER boxes:
 *   1. Run q_layout_line_wrap() to break text into Q_BOX_LINE children.
 *   2. Height = sum of line heights.
 *
 * For FLEX_CONTAINER:
 *   1. Implement single-line flex algorithm (simplified CSS Flexbox §9):
 *      resolve flex-basis for each item, distribute free space by flex-grow/shrink.
 *
 * For TEXT boxes:
 *   1. Call q_font_shape_run() → q_shaped_run_t.
 *   2. box->width = run->total_advance; box->height = ascender + |descender|.
 *
 * All sizes are stored as box->width, box->height (border box).
 */
void q_layout_measure(q_box_t *box, float containing_w, float containing_h);
```

### 4.3 Pass 3 — Assign positions  `q_layout_position()`

```c
/*
 * q_layout_position(box, origin_x, origin_y)
 *
 * Sets box->x, box->y to the border-box origin in document coordinates.
 *
 * For BLOCK / INLINE_CONTAINER:
 *   - place children sequentially, applying margins (with margin collapsing
 *     between adjacent block siblings).
 *
 * For FLEX_CONTAINER:
 *   - place flex items along main axis according to justify-content,
 *     cross axis according to align-items.
 *
 * For position:absolute / position:fixed:
 *   - resolve against the nearest positioned ancestor (or viewport).
 *   - place in a second pass after normal flow is done.
 *
 * For LINE boxes:
 *   - assign x=0 (or indent for first line), y = cumulative line y.
 *   - apply text-align by shifting text boxes within the line.
 */
void q_layout_position(q_box_t *box, float origin_x, float origin_y);
```

### 4.4 Line wrapping  `q_layout_line_wrap()`

```c
/*
 * Breaks the inline-level children of an inline container into lines.
 * Algorithm (simplified browser.engineering §5 approach):
 *
 *   cursor_x = 0
 *   current_line = new Q_BOX_LINE child
 *   for each text run / replaced element child:
 *     measure its width (via q_font_measure_word or shaped run)
 *     if cursor_x + width > container_width AND current_line is not empty:
 *       close current_line, open new one, reset cursor_x
 *     place child into current_line at cursor_x
 *     cursor_x += width + word_spacing
 *   close final line
 *
 * word-break and overflow-wrap are respected by splitting text runs at
 * character boundaries when a single word exceeds the container width.
 */
void q_layout_line_wrap(q_box_t *inline_container);
```

### 4.5 Full relayout entry point

```c
/*
 * Destroys old layout tree, rebuilds from scratch.
 * Called on: initial load, window resize, DOM mutation that affects layout.
 */
void q_layout_relayout(celerity_view_t *view);
```

---

## 5. Font & Text Functions  (`src/font/`)

```c
/* Load a TTF file into the font cache. Returns cached entry if already loaded. */
q_font_t *q_font_load(q_font_cache_t *cache,
                          const char *path, float size_px, int weight);

/* Load a font from an embedded memory blob. */
q_font_t *q_font_load_mem(q_font_cache_t *cache,
                              const void *data, size_t len,
                              float size_px, int weight);

/*
 * Find the best matching font for a CSS font-family list + size + weight.
 * Walks lxb_css_property_font_family_t linked list, tries each family name
 * against registered fonts, falls back to a built-in default.
 */
q_font_t *q_font_match(q_font_cache_t *cache,
                           const lxb_css_property_font_family_t *families,
                           float size_px, int weight);

/*
 * Measure the advance width of a UTF-8 string without full shaping.
 * Useful for quick line-wrap decisions.
 * Uses sft_lookup + sft_gmetrics + sft_kerning per codepoint pair.
 */
float q_font_measure(q_font_t *font, const lxb_char_t *text, size_t len);

/*
 * Full shaping: produce a q_shaped_run_t for a UTF-8 string.
 * Iterates codepoints, calls sft_lookup/sft_gmetrics/sft_kerning,
 * populates glyph array with advances and offsets.
 * Caller must q_shaped_run_free() the result.
 */
q_shaped_run_t *q_font_shape_run(q_font_t *font,
                                     const lxb_char_t *text, size_t len);

void q_shaped_run_free(q_shaped_run_t *run);

/*
 * Render a shaped run into an RGBA8 buffer at (dest_x, dest_y).
 * For each glyph: calls sft_render into a temporary SFT_Image (alpha mask),
 * then alpha-composites with `color` onto `pixels`.
 */
void q_font_render_run(const q_shaped_run_t *run,
                         uint32_t color,        /* RGBA8 */
                         uint8_t *pixels,       /* destination tile */
                         int tile_w, int tile_h,
                         int dest_x, int dest_y);
```

---

## 6. Paint Pass  (`src/paint/`)

Each box gets its own `tile` (RGBA8 buffer exactly `box->width × box->height` px).  
Tiles are painted independently — enabling trivial GPU texture upload later.

```c
/*
 * Recursively paint a box and all descendants.
 * Allocates box->tile, then:
 *   1. q_paint_background()  — fill background-color
 *   2. q_paint_borders()     — draw 4 borders
 *   3. For TEXT boxes: q_font_render_run()
 *   4. For IMAGE boxes: blit decoded pixels
 *   5. Recurse into children, blit child tiles onto parent tile
 *      at (child->x - box->x, child->y - box->y).
 */
void q_paint_box(q_box_t *box);

/* Fill a rectangle in an RGBA8 buffer with a solid color. */
void q_paint_fill_rect(uint8_t *pixels, int buf_w, int buf_h,
                         int x, int y, int w, int h, uint32_t color);

/* Draw box borders (solid style only for v1). */
void q_paint_borders(q_box_t *box);

/*
 * Alpha-composite src onto dst.
 * src and dst are RGBA8 row-major buffers.
 * src is placed at (dx, dy) in dst.
 */
void q_paint_composite(uint8_t *dst, int dst_w, int dst_h,
                         const uint8_t *src, int src_w, int src_h,
                         int dx, int dy);
```

### Compositor

```c
/*
 * Walk the box tree in paint order (respecting z-index stacking contexts),
 * blit all tiles into view->framebuffer.
 * Called after q_paint_box(view->layout_root).
 */
void q_composite_frame(celerity_view_t *view);
```

---

## 7. Event System  (`src/event/`)

```c
/*
 * Hit test: find the deepest box whose border box contains (x,y).
 * Returns NULL if no box hit.
 */
q_box_t *q_hit_test(q_box_t *root, int x, int y);

/*
 * Route a raw backend event:
 *   1. For mouse events: run q_hit_test, fill event->target / event->target_box.
 *   2. Dispatch a DOM event on the target DOM node using lexbor's DOM event API
 *      (lxb_dom_event_*) so that lexbor-level listeners fire.
 *   3. Call view->on_event(view, event, userdata) for the app-level callback.
 */
void q_event_dispatch(celerity_view_t *view, q_event_t *event);

/*
 * Convenience: walk the DOM ancestor chain from `node` and return the
 * nearest ancestor (or self) that has a given data attribute set.
 * Useful for implementing event delegation patterns.
 * Uses lxb_dom_element_attr_by_name() — no custom attribute storage needed.
 */
lxb_dom_node_t *q_event_find_delegate(lxb_dom_node_t *node,
                                        const char *attr_name);
```

---

## 8. DOM Mutation API  (`src/dom_api/`)

All mutations go through lexbor's existing DOM API.  
We expose thin wrappers that additionally schedule a relayout/repaint.

```c
typedef enum {
    Q_DIRTY_STYLE   = 1 << 0,  /* recompute computed styles */
    Q_DIRTY_LAYOUT  = 1 << 1,  /* rebuild box tree + measure */
    Q_DIRTY_PAINT   = 1 << 2,  /* repaint tiles */
} q_dirty_flags_t;

/* Mark a subtree dirty; will be processed on next q_view_update(). */
void q_dom_mark_dirty(celerity_view_t *view,
                        lxb_dom_node_t *node,
                        q_dirty_flags_t flags);

/* Set an attribute on an element and schedule appropriate dirty. */
lxb_status_t q_dom_set_attr(celerity_view_t *view,
                              lxb_dom_element_t *el,
                              const char *name, const char *value);

/* Remove an attribute. */
lxb_status_t q_dom_remove_attr(celerity_view_t *view,
                                 lxb_dom_element_t *el,
                                 const char *name);

/* Set element.textContent — replaces all children with a single text node. */
lxb_status_t q_dom_set_text_content(celerity_view_t *view,
                                      lxb_dom_element_t *el,
                                      const char *text, size_t len);

/* Append a new child element. */
lxb_dom_element_t *q_dom_append_element(celerity_view_t *view,
                                          lxb_dom_element_t *parent,
                                          const char *tag_name);

/* Remove a node from the tree. */
lxb_status_t q_dom_remove_node(celerity_view_t *view,
                                 lxb_dom_node_t *node);

/*
 * CSS class helpers.
 * These manipulate the `class` attribute string
 * and are built on top of q_dom_set_attr.
 */
void q_dom_add_class(celerity_view_t *view,
                       lxb_dom_element_t *el, const char *cls);
void q_dom_remove_class(celerity_view_t *view,
                          lxb_dom_element_t *el, const char *cls);
bool q_dom_has_class(lxb_dom_element_t *el, const char *cls);

/* querySelector using lexbor's selectors module */
lxb_dom_element_t *q_dom_query_selector(celerity_view_t *view,
                                          const char *selector);

/* querySelectorAll — fills `out` array, returns count */
size_t q_dom_query_selector_all(celerity_view_t *view,
                                  const char *selector,
                                  lxb_dom_element_t **out, size_t out_max);
```

---

## 9. Resource Loader  (`src/resource/`)

```c
/*
 * Supported URL schemes:
 *   file:///path/to/file     -- maps directly to filesystem
 *   app://resource/name      -- looks up name in embedded resource table
 *
 * Returns a malloc'd buffer + length. Caller frees.
 * Returns NULL on error.
 */
uint8_t *q_resource_load(const char *url, size_t *out_len);

/*
 * Register an embedded resource (e.g. bundled HTML/CSS/font compiled in
 * via xxd or similar). Call before q_view_load_url().
 */
void q_resource_register(const char *name,
                           const void *data, size_t len);

/* Free a buffer returned by q_resource_load. */
void q_resource_free(uint8_t *buf);
```

---

## 10. Top-Level View API (public `quanton.h`)

This is what framework users see.

```c
#ifndef CELERITY_H
#define CELERITY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lexbor/html/html.h"   /* lxb_html_document_t, lxb_dom_node_t, etc. */

/* ── opaque handles ── */
typedef struct celerity_ctx    celerity_ctx_t;
typedef struct celerity_view   celerity_view_t;
typedef struct q_box         q_box_t;

/* ── event types (see section 7) ── */
/* ... (include the q_event_type_t and q_event_t definitions) ... */

/* ── callbacks ── */
typedef void (*q_event_handler_fn)(celerity_view_t *view,
                                     const q_event_t *event,
                                     void *userdata);

/* ── lifecycle ── */

/*
 * Initialize a Quanton context.
 * backend_name: "x11" or "sdl2"
 */
celerity_ctx_t *celerity_init(const char *backend_name);
void            celerity_shutdown(celerity_ctx_t *ctx);

/*
 * Create a new view (window).
 * width/height: initial viewport in px.
 * title: window title string.
 */
celerity_view_t *celerity_view_create(celerity_ctx_t *ctx,
                                      int width, int height,
                                      const char *title);
void             celerity_view_destroy(celerity_view_t *view);

/*
 * Load and render a local HTML file.
 * url must be "file://..." or "app://..."
 */
int  celerity_view_load_url(celerity_view_t *view, const char *url);

/*
 * Load HTML from a string in memory.
 */
int  celerity_view_load_html(celerity_view_t *view,
                             const char *html, size_t len);

/*
 * Set the event handler. Called for all user input and window events.
 */
void celerity_view_set_event_handler(celerity_view_t *view,
                                     q_event_handler_fn fn,
                                     void *userdata);

/*
 * Main loop step — poll events, process dirty flags, repaint if needed,
 * blit to screen. Call in a loop.
 * Returns false when the view should close.
 */
bool celerity_view_update(celerity_view_t *view);

/*
 * Force a synchronous full relayout + repaint.
 * Usually not needed; use q_dom_mark_dirty instead.
 */
void celerity_view_refresh(celerity_view_t *view);

/* ── DOM access ── */

/* Get the root document node. */
lxb_html_document_t *celerity_view_get_document(celerity_view_t *view);

/* querySelector */
lxb_dom_element_t *celerity_query_selector(celerity_view_t *view,
                                           const char *selector);
size_t             celerity_query_selector_all(celerity_view_t *view,
                                               const char *selector,
                                               lxb_dom_element_t **out,
                                               size_t out_max);

/* ── DOM mutation ── */
int  celerity_set_attr(celerity_view_t *view,
                       lxb_dom_element_t *el,
                       const char *name, const char *value);
int  celerity_remove_attr(celerity_view_t *view,
                          lxb_dom_element_t *el, const char *name);
int  celerity_set_text_content(celerity_view_t *view,
                               lxb_dom_element_t *el,
                               const char *text, size_t len);
lxb_dom_element_t *celerity_append_element(celerity_view_t *view,
                                           lxb_dom_element_t *parent,
                                           const char *tag_name);
int  celerity_remove_node(celerity_view_t *view, lxb_dom_node_t *node);
void celerity_add_class(celerity_view_t *view,
                        lxb_dom_element_t *el, const char *cls);
void celerity_remove_class(celerity_view_t *view,
                           lxb_dom_element_t *el, const char *cls);
bool celerity_has_class(lxb_dom_element_t *el, const char *cls);

/* ── Font registration ── */

/*
 * Register a TTF font file for use by CSS font-family names.
 * family_name: the name used in CSS font-family (e.g. "MyFont").
 */
int celerity_register_font(celerity_ctx_t *ctx,
                           const char *family_name,
                           const char *ttf_path,
                           int weight);   /* 400=regular, 700=bold */

int celerity_register_font_mem(celerity_ctx_t *ctx,
                               const char *family_name,
                               const void *data, size_t len,
                               int weight);

/* ── Resource registry ── */
void celerity_register_resource(const char *app_path,
                                const void *data, size_t len);

#endif /* CELERITY_H */
```

---

## 11. Minimal Usage Example

```c
#include "quanton.h"

static void on_event(celerity_view_t *view, const q_event_t *ev, void *ud) {
    if (ev->type == Q_EVENT_MOUSE_CLICK && ev->target) {
        /* read any attribute directly from lexbor — no custom storage */
        lxb_dom_element_t *el = lxb_dom_interface_element(ev->target);
        const lxb_char_t *action =
            lxb_dom_element_get_attribute(el,
                (const lxb_char_t *)"data-action", 11, NULL);
        if (action)
            printf("clicked: %s\n", (const char *)action);

        /* toggle a CSS class */
        celerity_add_class(view, el, "active");
    }
    if (ev->type == Q_EVENT_CLOSE)
        *(bool *)ud = false;
}

int main(void) {
    celerity_ctx_t *ctx = celerity_init("x11");
    celerity_register_font(ctx, "sans-serif", "/usr/share/fonts/ttf/DejaVuSans.ttf", 400);

    celerity_view_t *view = celerity_view_create(ctx, 1280, 720, "My App");

    bool running = true;
    celerity_view_set_event_handler(view, on_event, &running);
    celerity_view_load_url(view, "file:///home/user/myapp/index.html");

    while (running)
        celerity_view_update(view);

    celerity_view_destroy(view);
    celerity_shutdown(ctx);
}
```

---

## 12. Implementation Order (recommended)

| Phase | What to build | Notes |
|-------|---------------|-------|
| 1 | Resource loader | trivial — fopen + mmap |
| 2 | lexbor integration shim | parse HTML + CSS, get computed styles |
| 3 | Font cache + libschrift wrapper | `q_font_load`, `q_font_measure`, `q_font_shape_run` |
| 4 | Box tree builder (pass 1) | `q_layout_build_tree` — no geometry yet |
| 5 | Block layout measure + position | `q_layout_measure` + `q_layout_position` for block only |
| 6 | Paint: backgrounds + borders | `q_paint_box` for block boxes |
| 7 | X11 backend + compositor blit | get something on screen |
| 8 | Inline layout + line wrapping | `q_layout_line_wrap`, text run shaping |
| 9 | Text paint | `q_font_render_run` |
| 10 | Event routing + hit test | `q_hit_test`, `q_event_dispatch` |
| 11 | DOM mutation API + dirty tracking | `q_dom_mark_dirty`, incremental relayout |
| 12 | Flex layout | simplified single-line flexbox |
| 13 | SDL2 backend | trivial once backend vtable exists |
| 14 | Absolute/fixed positioning | second-pass positioned elements |
| 15 | z-index stacking contexts | compositor ordering |

---

## 13. What We Get "for Free" from lexbor

| Feature | lexbor module |
|---------|--------------|
| HTML5 parsing (full spec) | `html` (parser, tokenizer, tree builder) |
| DOM tree + navigation | `dom` |
| CSS parsing | `css` (parser, syntax) |
| CSS cascade & inheritance | `style`, `engine` |
| All CSS property structs | `css/property.h` — display, margin, padding, border, font, flex, position, overflow, color, text-*, z-index … |
| CSS selector matching | `selectors` |
| CSS values (lengths, colors, angles …) | `css/value.h`, `css/unit.h` |
| HTML serialization | `html/serialize.h` |
| Unicode & encoding | `unicode`, `encoding` |
| URL parsing | `url` |

We do **not** need to implement any of this ourselves.

---

## 14. Key Design Decisions & Rationale

1. **Per-box tiles instead of a single framebuffer for painting** — enables dirty-box repainting (only repaint boxes that changed) and trivial GPU texture upload. The compositor pass stays simple: blit tiles in tree order.

2. **No custom attribute storage** — event targets and UI state are stored as `data-*` attributes directly in the DOM, readable via `lxb_dom_element_get_attribute`. No parallel attribute dictionaries.

3. **`q_computed_style_t` is a resolved cache** — it is populated from lexbor's CSS property structs (which already store parsed values). `q_style_resolve()` converts lexbor's unit-agnostic values (em, %, px, etc.) to float px values given the current viewport and parent font size. The original lexbor data is not duplicated.

4. **libschrift is used only for TTF glyph rendering and metric queries** — complex shaping (Arabic, Indic) is out of scope for v1. The font cache is keyed on `(family, size_px, weight)`. Glyph metric results are cached per-font to avoid repeated `sft_gmetrics` calls.

5. **`SFT_Image` pixel buffers are stack-allocated scratch space** during `q_font_render_run` — glyph bitmaps are immediately composited onto the box tile and freed; they are not stored long-term.

