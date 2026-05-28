# Quanton — Stage 2 Implementation Plan

## Status: Stage 1 complete (see `IMPLEMENTATION_PLAN_STAGE1_DONE.md`)

Stage 1 delivered: block layout, inline layout + line wrapping, flex containers,
absolute/fixed positioning, z-index stacking contexts, text shaping and rendering,
box backgrounds and borders, hit testing, event routing, DOM mutation helpers,
dirty-flag incremental relayout, and SDL2 / X11 / PNG backends.

---

## 1. Stage 2 Goals

| Priority | Feature |
|----------|---------|
| **P0** | Scrolling & viewport (root-level and per-element) |
| **P0** | Table layout (`display:table`, HTML `<table>`) — HTML4 core |
| **P1** | Float layout (`float:left/right`, `clear:left/right/both`) |
| **P1** | `overflow:hidden` clipping |
| **P1** | Image support (`<img>`, `background-image`) |
| **P2** | List items (`display:list-item`, `list-style-type`) |
| **P2** | `white-space:pre / pre-wrap / nowrap` |
| **P2** | `text-decoration` (underline / overline / line-through) |
| **P2** | `display:inline-block` |
| **P2** | `vertical-align` for inline content |
| **P3** | `border-radius` — rounded corners |
| **P3** | Basic form widgets (input, button, select — visual only) |

---

## 2. Lexbor APIs leveraged in Stage 2

All CSS structs are in `lexbor/css/property.h`.
All enumerants are in `lexbor/css/property/const.h` and `lexbor/css/value.h`.
Access a computed property from an element via:

```c
const void *raw = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_OVERFLOW_X);
const lxb_css_property_overflow_x_t *ov = raw;
```

### 2.1 Overflow / Scroll

```c
/* const.h enumerants */
LXB_CSS_OVERFLOW_X_VISIBLE  /* 0 — default, no clip */
LXB_CSS_OVERFLOW_X_HIDDEN   /* clip, no scroll */
LXB_CSS_OVERFLOW_X_CLIP     /* same as hidden for us */
LXB_CSS_OVERFLOW_X_SCROLL   /* clip + show scrollbar */
LXB_CSS_OVERFLOW_X_AUTO     /* clip + scrollbar only when needed */

typedef struct { lxb_css_overflow_x_type_t type; } lxb_css_property_overflow_x_t;
typedef struct { lxb_css_overflow_y_type_t type; } lxb_css_property_overflow_y_t;
```

### 2.2 Display — table and inline-block values

```c
/* property/const.h */
LXB_CSS_DISPLAY_TABLE               /* display:table */
LXB_CSS_DISPLAY_TABLE_ROW_GROUP     /* display:table-row-group (tbody) */
LXB_CSS_DISPLAY_TABLE_HEADER_GROUP  /* display:table-header-group (thead) */
LXB_CSS_DISPLAY_TABLE_FOOTER_GROUP  /* display:table-footer-group (tfoot) */
LXB_CSS_DISPLAY_TABLE_ROW           /* display:table-row */
LXB_CSS_DISPLAY_TABLE_CELL          /* display:table-cell */
LXB_CSS_DISPLAY_TABLE_COLUMN_GROUP  /* display:table-column-group */
LXB_CSS_DISPLAY_TABLE_COLUMN        /* display:table-column */
LXB_CSS_DISPLAY_TABLE_CAPTION       /* display:table-caption */
LXB_CSS_DISPLAY_INLINE_BLOCK        /* display:inline-block */
LXB_CSS_DISPLAY_INLINE_TABLE        /* display:inline-table */
LXB_CSS_DISPLAY_LIST_ITEM           /* display:list-item */

typedef struct {
    lxb_css_display_type_t a;   /* outer display */
    lxb_css_display_type_t b;   /* inner display */
    lxb_css_display_type_t c;   /* extra (list-item marker) */
} lxb_css_property_display_t;
```

### 2.3 Float and Clear

```c
LXB_CSS_FLOAT_NONE   LXB_CSS_FLOAT_LEFT   LXB_CSS_FLOAT_RIGHT
typedef struct {
    lxb_css_float_type_t type;
    lxb_css_value_length_type_t length;
    lxb_css_float_type_t snap_type;
} lxb_css_property_float_t;

LXB_CSS_CLEAR_NONE  LXB_CSS_CLEAR_LEFT  LXB_CSS_CLEAR_RIGHT  LXB_CSS_CLEAR_BOTH (== BOTH value)
typedef struct { lxb_css_clear_type_t type; } lxb_css_property_clear_t;
```

### 2.4 White-space

```c
LXB_CSS_WHITE_SPACE_NORMAL
LXB_CSS_WHITE_SPACE_PRE
LXB_CSS_WHITE_SPACE_NOWRAP
LXB_CSS_WHITE_SPACE_PRE_WRAP
LXB_CSS_WHITE_SPACE_PRE_LINE
typedef struct { lxb_css_white_space_type_t type; } lxb_css_property_white_space_t;
```

### 2.5 Text Decoration

```c
typedef struct {
    lxb_css_text_decoration_line_t  line;   /* underline/overline/line-through */
    lxb_css_text_decoration_style_t style;  /* solid/dashed/dotted/double/wavy */
    lxb_css_value_color_t           color;
} lxb_css_property_text_decoration_t;
```

### 2.6 Vertical Align

```c
typedef struct {
    lxb_css_vertical_align_type_t         type;   /* top/middle/bottom/baseline/sub/super */
    lxb_css_property_alignment_baseline_t alignment;
    lxb_css_property_baseline_shift_t     shift;
} lxb_css_property_vertical_align_t;
```

### 2.7 Line Height

```c
typedef lxb_css_value_number_length_percentage_t lxb_css_property_line_height_t;
/* .type == LXB_CSS_VALUE__LENGTH → .data.length.value in specified unit */
/* .type == LXB_CSS_VALUE__NUMBER → multiplier of font-size */
/* .type == LXB_CSS_VALUE_NORMAL  → use font metrics */
```

### 2.8 List Style

```c
/* property/const.h */
LXB_CSS_DISPLAY_LIST_ITEM
/* list-style-type, list-style-position are separate CSS properties;
 * they are parsed by lexbor but their property IDs must be looked up via
 * lxb_style_id_by_name(doc, "list-style-type", 15) at runtime because they
 * are not in the default LXB_CSS_PROPERTY_* enum. */
```

### 2.9 HTML element interfaces for tables

```c
#include "lexbor/html/interfaces/table_element.h"         /* lxb_html_table_element_t */
#include "lexbor/html/interfaces/table_row_element.h"     /* lxb_html_table_row_element_t */
#include "lexbor/html/interfaces/table_cell_element.h"    /* lxb_html_table_cell_element_t */
/* colspan, rowspan are read as plain string attributes via:
 * lxb_dom_element_get_attribute(el, "colspan", 7, &len)
 * lxb_dom_element_get_attribute(el, "rowspan", 7, &len)
 */
```

---

## 3. New / Extended Data Structures

### 3.1 Extended `q_box_t`

```c
struct q_box {
    /* ── existing fields (unchanged) ── */
    q_box_type_t     type;
    int              is_flex_container;
    q_position_type_t position;
    int              has_z_index;
    int              z_index;
    float            x, y, width, height;
    lxb_dom_node_t  *dom_node;
    struct q_box    *parent, *first_child, *last_child, *next_sibling, *prev_sibling;
    const char      *text;
    size_t           text_len;
    q_shaped_run_t  *run;
    float            border_width[4];
    uint32_t         border_color[4];
    uint32_t         background_color;
    uint8_t         *tile;
    int              tile_w, tile_h;
    float            style_top, style_right, style_bottom, style_left;
    float            style_width, style_height;

    /* ── Stage 2 additions ── */

    /* Float layout */
    q_float_type_t   float_type;      /* Q_FLOAT_NONE/LEFT/RIGHT */
    q_clear_type_t   clear_type;      /* Q_CLEAR_NONE/LEFT/RIGHT/BOTH */

    /* Overflow / scroll container */
    q_overflow_type_t overflow_x;     /* Q_OVERFLOW_VISIBLE/HIDDEN/SCROLL/AUTO */
    q_overflow_type_t overflow_y;
    float            scroll_x;        /* current scroll offset (document px) */
    float            scroll_y;

    /* White-space behaviour for text children */
    q_white_space_t  white_space;     /* Q_WS_NORMAL/PRE/NOWRAP/PRE_WRAP */

    /* Text decoration */
    uint8_t          text_decoration; /* Q_TD_NONE/UNDERLINE/OVERLINE/LINE_THROUGH (bitmask) */
    uint32_t         text_decoration_color;

    /* Inline-block / list-item */
    int              is_inline_block; /* 1 when display:inline-block */
    int              is_list_item;    /* 1 when display:list-item */

    /* Image payload (Q_BOX_IMAGE) */
    uint8_t         *img_pixels;      /* decoded RGBA8, owned; NULL for non-image */
    int              img_w, img_h;

    /* Table layout (Q_BOX_TABLE) — points to an externally-owned q_table_t */
    struct q_table  *table;           /* non-NULL only for Q_BOX_TABLE */

    /* Border radius (px, order: top-left, top-right, bottom-right, bottom-left) */
    float            border_radius[4];
};
```

### 3.2 New box type enumerants

```c
typedef enum q_box_type {
    Q_BOX_BLOCK,
    Q_BOX_TEXT,
    Q_BOX_INLINE_CONTAINER,
    Q_BOX_LINE,
    /* Stage 2 */
    Q_BOX_IMAGE,             /* <img> or CSS background-image */
    Q_BOX_TABLE,             /* display:table or <table> */
    Q_BOX_TABLE_SECTION,     /* thead/tbody/tfoot */
    Q_BOX_TABLE_ROW,         /* tr */
    Q_BOX_TABLE_CELL,        /* td / th */
    Q_BOX_TABLE_CAPTION,     /* caption */
    Q_BOX_TABLE_COLUMN,      /* col */
    Q_BOX_FLOAT,             /* float:left or float:right — lives in float list */
    Q_BOX_INLINE_BLOCK,      /* display:inline-block */
    Q_BOX_LIST_ITEM,         /* display:list-item */
    Q_BOX_LIST_MARKER,       /* the bullet/counter box */
} q_box_type_t;
```

### 3.3 Float type / clear type

```c
typedef enum {
    Q_FLOAT_NONE  = 0,
    Q_FLOAT_LEFT  = 1,
    Q_FLOAT_RIGHT = 2,
} q_float_type_t;

typedef enum {
    Q_CLEAR_NONE  = 0,
    Q_CLEAR_LEFT  = 1,
    Q_CLEAR_RIGHT = 2,
    Q_CLEAR_BOTH  = 3,
} q_clear_type_t;
```

### 3.4 Overflow type

```c
typedef enum {
    Q_OVERFLOW_VISIBLE = 0, /* default */
    Q_OVERFLOW_HIDDEN  = 1,
    Q_OVERFLOW_SCROLL  = 2,
    Q_OVERFLOW_AUTO    = 3,
    Q_OVERFLOW_CLIP    = 4,
} q_overflow_type_t;
```

### 3.5 White-space type

```c
typedef enum {
    Q_WS_NORMAL   = 0,
    Q_WS_PRE      = 1,
    Q_WS_NOWRAP   = 2,
    Q_WS_PRE_WRAP = 3,
    Q_WS_PRE_LINE = 4,
} q_white_space_t;
```

### 3.6 Table structure  `q_table_t`  (`src/layout/table_layout.c`)

```c
/* One entry in the column descriptor array */
typedef struct q_table_col {
    float      min_width;   /* max of all cells' min-content width in column */
    float      max_width;   /* max of all cells' max-content width */
    float      final_width; /* resolved width after table layout */
    int        is_fixed;    /* 1 if an explicit px width was set */
} q_table_col_t;

/* One entry in the row descriptor array */
typedef struct q_table_row {
    float      height;      /* resolved row height */
    q_box_t   *box;         /* back-pointer to Q_BOX_TABLE_ROW */
} q_table_row_t;

/* Colspan/rowspan span occupancy grid: row-major, col_count columns */
typedef struct q_table_span {
    int row;   int col;   int rowspan;   int colspan;
    q_box_t *cell_box;
} q_table_span_t;

typedef struct q_table {
    int             col_count;
    int             row_count;
    q_table_col_t  *cols;       /* [col_count], owned */
    q_table_row_t  *rows;       /* [row_count], owned */
    q_table_span_t *spans;      /* [span_count], owned */
    int             span_count;
    int             border_collapse; /* 1 = collapse, 0 = separate */
    float           border_spacing_x;
    float           border_spacing_y;
    float           caption_height;  /* 0 if no caption */
} q_table_t;
```

### 3.7 Float context  `q_float_ctx_t`  (per block formatting context)

```c
typedef struct q_float_entry {
    q_box_t                *box;    /* the floating box */
    struct q_float_entry   *next;
} q_float_entry_t;

typedef struct q_float_ctx {
    q_float_entry_t *left_floats;   /* list, sorted by y */
    q_float_entry_t *right_floats;
} q_float_ctx_t;
/* Embedded in q_box_t for block containers (zero-initialised = no floats) */
```

### 3.8 Image cache  `q_image_cache_t`  (`src/image/image.c`)

```c
typedef struct q_image {
    char     *url;          /* owned copy of source URL */
    uint8_t  *pixels;       /* decoded RGBA8, owned */
    int       w, h;
    int       ref_count;
} q_image_t;

typedef struct q_image_cache {
    q_image_t **entries;
    size_t      count, capacity;
} q_image_cache_t;
```

### 3.9 Scroll state in view and dirty flags

```c
/* extend q_dirty_flags_t */
typedef enum q_dirty_flags {
    Q_DIRTY_STYLE   = 1 << 0,
    Q_DIRTY_LAYOUT  = 1 << 1,
    Q_DIRTY_PAINT   = 1 << 2,
    Q_DIRTY_SCROLL  = 1 << 3,  /* NEW: scroll offset changed, composite only */
} q_dirty_flags_t;

/* extend quanton_view_t */
struct quanton_view {
    /* ... existing fields ... */
    float            scroll_x;          /* root document scroll offset */
    float            scroll_y;
    float            doc_width;         /* full document width (may exceed vp) */
    float            doc_height;        /* full document height */
    q_image_cache_t *image_cache;       /* shared across documents in this view */
};
```

---

## 4. New Module: `src/layout/table_layout.c`

### 4.1 Box tree — recognising table elements

In `q_layout_build_tree()` (box_tree.c), extend the display-value switch to
emit Q_BOX_TABLE, Q_BOX_TABLE_SECTION, Q_BOX_TABLE_ROW, Q_BOX_TABLE_CELL, and
Q_BOX_TABLE_CAPTION.  HTML element tags `<table>`, `<thead>`, `<tbody>`,
`<tfoot>`, `<tr>`, `<td>`, `<th>`, `<caption>`, `<col>`, `<colgroup>` are
recognised by their tag IDs (via `lxb_tag_id_by_name()`) as a fallback when no
`display` CSS overrides them, matching HTML's implicit display mapping.

**Anonymous table box fixup** — run a post-processing pass after the initial box
tree is built.  The CSS table model requires that table-internal boxes are
wrapped correctly:

1. A `Q_BOX_TABLE_ROW` whose parent is not a table section → insert an
   anonymous `Q_BOX_TABLE_SECTION`.
2. A `Q_BOX_TABLE_CELL` whose parent is not a `Q_BOX_TABLE_ROW` → insert an
   anonymous `Q_BOX_TABLE_ROW`.
3. Text / block content that is a direct child of a table → wrap in an
   anonymous `Q_BOX_TABLE_CELL` + row + section.

```c
/* Perform anonymous-box fixup for table internals.
 * Called once after q_layout_build_tree() returns. */
void q_table_fixup_anonymous(q_box_t *table_box);
```

### 4.2 Measure pass  `q_table_measure()`

```c
/*
 * 1. Build the grid: walk thead/tbody/tfoot/tr/td children, record
 *    (row, col, rowspan, colspan) for each cell (respecting any
 *    already-occupied slots from earlier cells with colspan/rowspan).
 * 2. Resolve explicit column widths (col.width attribute / CSS width on <col>).
 * 3. Pass 1 — colspan=1 cells:
 *    for each cell, call q_layout_measure(cell, INFINITY, INFINITY) to get
 *    min-content and max-content widths.
 *    Update col.min_width / col.max_width.
 * 4. Pass 2 — cells with colspan > 1: distribute excess to spanned columns.
 * 5. Compute table width:
 *    - explicit table width → distribute to columns proportionally.
 *    - auto table width    → sum of col.max_width + border-spacing.
 * 6. For each row: call q_layout_measure(cell, col_width, 0) for all cells,
 *    row.height = max(cell heights) + border-spacing.
 * 7. Handle rowspan > 1: distribute excess height to spanned rows.
 * 8. Store table->col_count, row_count, final widths/heights.
 */
void q_table_measure(q_box_t *table_box, float containing_w);
```

### 4.3 Position pass  `q_table_position()`

```c
/*
 * Walk rows then cells, assign final (x,y) using cumulative col/row offsets.
 * border-collapse: merge shared borders (use the "winner" border style).
 * border-separate: insert border-spacing gaps.
 * caption: placed above or below table (caption-side: top/bottom).
 */
void q_table_position(q_box_t *table_box, float origin_x, float origin_y);
```

---

## 5. New Module: `src/layout/float_layout.c`

Float layout is integrated into the existing block layout pass.

### 5.1 Key algorithm changes in `q_layout_measure` (block_layout.c)

When measuring a block formatting context (BFC), maintain a `q_float_ctx_t`:

```c
/* Returns the left edge available for content at vertical position y,
 * given all active left-floats that overlap [y, y+line_h). */
float q_float_ctx_left_edge(const q_float_ctx_t *ctx, float y, float line_h);

/* Returns the right edge available at y. */
float q_float_ctx_right_edge(const q_float_ctx_t *ctx, float y, float line_h,
                              float containing_w);

/* The y position at which all floats of the given side have cleared. */
float q_float_ctx_clear_y(const q_float_ctx_t *ctx, q_clear_type_t clear);

/* Register a new float into the context. */
void  q_float_ctx_add(q_float_ctx_t *ctx, q_box_t *float_box,
                      q_float_type_t side, float origin_y);
```

Flow:
1. When encountering a child box with `float_type != Q_FLOAT_NONE`:
   - Measure the float independently.
   - Register it with `q_float_ctx_add`.
   - The float is **not** placed in the normal flow child list; it lives in the float context.
2. For all other children in the BFC: query `q_float_ctx_left_edge` /
   `q_float_ctx_right_edge` to narrow the available width for line wrapping.
3. For `clear: left/right/both`: push the child's y past `q_float_ctx_clear_y`.
4. Floats are painted as part of their containing BFC, at their stacking order
   (before in-flow content in the same stacking context).

---

## 6. Overflow — Clipping and Scrolling

### 6.1 `overflow:hidden` clipping

During the **composite pass** (`q_composite_frame` / `q_paint_box`):

- When compositing a child tile onto a parent tile, check the parent box's
  `overflow_x` / `overflow_y`.
- If `HIDDEN` or `CLIP`: pass a clip rectangle (parent's content area) to
  `q_paint_composite_clipped()`.

```c
/* Like q_paint_composite but clips the source rect to [clip_x, clip_x+clip_w) × … */
void q_paint_composite_clipped(uint8_t *dst, int dst_w, int dst_h,
                                const uint8_t *src, int src_w, int src_h,
                                int dx, int dy,
                                int clip_x, int clip_y, int clip_w, int clip_h);
```

### 6.2 Scrollable containers

A box with `overflow_x == Q_OVERFLOW_SCROLL || Q_OVERFLOW_AUTO` (or `_y`) is a
**scroll container**.

Fields added to `q_box_t`: `scroll_x`, `scroll_y` (current scroll offset in
document px).

During the **composite pass**: when blitting a scroll container's content tile
onto its parent, shift the source origin by `(-scroll_x, -scroll_y)` so that
the visible portion of the content is correctly positioned.

Scrollbar rendering (simple):
- If `overflow_y == Q_OVERFLOW_SCROLL`: always draw a vertical scrollbar strip.
- If `overflow_y == Q_OVERFLOW_AUTO`: draw only when content height > box height.
- Scrollbar is a fixed-width (12 px) vertical strip on the right edge, filled
  with a mid-gray track + darker thumb.  The thumb height and position are
  proportional to the visible fraction and scroll_y.

```c
/* Paint a simple scrollbar into box->tile.
 * vert=1 for vertical, vert=0 for horizontal. */
void q_paint_scrollbar(q_box_t *box, int vert);
```

### 6.3 Scroll event handling

In `q_event_dispatch` (event.c): when `type == Q_EVENT_MOUSE_WHEEL`:

1. Hit-test to find the deepest scroll container under the mouse.
2. Update that container's `scroll_y` by `wheel_delta * SCROLL_SPEED` (clamped
   to `[0, content_h - box_h]`).
3. If no scroll container found, update `view->scroll_y` (root document scroll).
4. Set `Q_DIRTY_SCROLL` — triggers a composite-only redraw without full relayout.

### 6.4 Root-level document scroll

`quanton_view_t` gains `scroll_x`, `scroll_y`, `doc_width`, `doc_height`.
During `q_composite_frame`, shift the layout root by `(-view->scroll_x,
-view->scroll_y)` before blitting.

New API:

```c
/* Scroll the root document view by (dx, dy) px.  Clamps to document bounds. */
void q_view_scroll_by(quanton_view_t *view, float dx, float dy);

/* Scroll to an absolute position. */
void q_view_scroll_to(quanton_view_t *view, float x, float y);

/* Scroll an element into view (like scrollIntoView()). */
void q_view_scroll_into_view(quanton_view_t *view, const q_box_t *box);
```

---

## 7. Image Support  (`src/image/image.c`)

### 7.1 Decoder integration

Vendor `stb_image.h` (single-header library, public domain) under
`third_party/stb_image/stb_image.h`.  It decodes PNG, JPEG, GIF (first frame),
BMP, TGA from memory.  Usage:

```c
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

uint8_t *q_image_decode(const uint8_t *buf, size_t len,
                        int *out_w, int *out_h)
{
    int comp;
    /* stbi_load_from_memory always outputs 4-channel RGBA */
    return stbi_load_from_memory(buf, (int)len, out_w, out_h, &comp, 4);
}
void q_image_decode_free(uint8_t *pixels) { stbi_image_free(pixels); }
```

### 7.2 Image cache API

```c
q_image_cache_t *q_image_cache_create(void);
void             q_image_cache_destroy(q_image_cache_t *cache);

/* Load and cache an image from a file:// URL.
 * Returns a borrowed pointer (cache owns it).
 * Returns NULL on error. */
q_image_t *q_image_cache_get(q_image_cache_t *cache, const char *url);

/* Release a reference (decrement ref_count; free if 0). */
void q_image_cache_release(q_image_cache_t *cache, q_image_t *img);
```

### 7.3 `<img>` element handling in box_tree.c

When processing an `<img>` element, create a `Q_BOX_IMAGE` box:

```c
/* In q_layout_build_tree_node(): */
if (tag_id == LXB_TAG_IMG) {
    box->type = Q_BOX_IMAGE;
    const lxb_char_t *src = lxb_dom_element_get_attribute(el,
        (const lxb_char_t *)"src", 3, &src_len);
    if (src) {
        /* resolve relative URL against document base_url */
        char *abs_url = q_url_resolve(doc->base_url, (const char *)src);
        q_image_t *img = q_image_cache_get(view->image_cache, abs_url);
        if (img) {
            box->img_pixels = img->pixels;
            box->img_w = img->w;
            box->img_h = img->h;
        }
        free(abs_url);
    }
}
```

During measure: `box->width = box->img_w; box->height = box->img_h;` (subject
to CSS width/height overrides).

During paint: blit `box->img_pixels` (with scale if necessary) into `box->tile`.

### 7.4 CSS `background-image`

```c
/* Extend q_box_t */
q_image_t *bg_image;           /* NULL = no background image */
q_bg_repeat_t bg_repeat;       /* Q_BG_REPEAT, NO_REPEAT, REPEAT_X, REPEAT_Y */
q_bg_size_t   bg_size;         /* Q_BG_SIZE_AUTO, COVER, CONTAIN, or explicit px */
float         bg_pos_x, bg_pos_y;
```

In `q_paint_box`, if `bg_image != NULL`, tile/cover/contain the decoded pixels
over the background area before painting borders and children.

```c
/* New helper in paint.c */
void q_paint_background_image(q_box_t *box);
```

---

## 8. List Items  (`src/layout/block_layout.c` + `paint.c`)

### 8.1 Box tree

When `display == LXB_CSS_DISPLAY_LIST_ITEM` (or tag is `<li>`):
- Emit `Q_BOX_LIST_ITEM` (behaves like a block).
- Emit a child `Q_BOX_LIST_MARKER` positioned to the left of the list item's
  content area (outside or inside, depending on `list-style-position`).

### 8.2 Marker rendering

```c
/* In paint.c */
void q_paint_list_marker(q_box_t *marker_box, int list_style_type,
                          int ordinal_value,  /* for decimal / alpha */
                          uint32_t color);
```

Supported `list-style-type` values (read from CSS via `lxb_style_id_by_name`):
`none`, `disc`, `circle`, `square`, `decimal`, `lower-alpha`, `upper-alpha`,
`lower-roman`, `upper-roman`.

The marker is painted as:
- `disc` / `circle` / `square` — small filled/unfilled shape.
- `decimal` / `alpha` / `roman` — shaped text run using the body font.

### 8.3 Counter tracking

```c
/* Per-list-context counter (lives in q_float_ctx_t or on the view stack) */
typedef struct { int value; } q_list_counter_t;
```

When building the box tree, maintain a counter stack.  Each `<ol>` push/pops
the counter; each `<li>` increments and assigns `ordinal_value` to its marker.

---

## 9. `white-space:pre` / `nowrap`  (inline_layout.c)

In `q_layout_line_wrap()`, check the containing block's `white_space` field:

- `Q_WS_PRE` / `Q_WS_PRE_WRAP`: honour `\n` in text nodes as hard line breaks
  (produce new `Q_BOX_LINE`); preserve runs of spaces (no collapsing).
- `Q_WS_NOWRAP`: never break lines (except at `<br>` elements).
- `Q_WS_NORMAL` (default): collapse runs of whitespace to a single space; break
  at word boundaries to fit container width.

Extend `q_dom_text_content()` (box_tree.c) to skip whitespace collapsing when
`white_space` is `pre`.

Add `<br>` handling: when encountering a `lxb_tag_BR` element during inline
box tree construction, emit a `Q_BOX_LINE` break marker.

---

## 10. `text-decoration`  (paint.c)

In `q_paint_box`, after rendering the text run, if `box->text_decoration != 0`:
- Calculate `underline_y = baseline + 1 px`.
- Calculate `overline_y = box->y` (top of em box).
- Calculate `line_through_y = baseline - ascender * 0.5`.
- Call `q_paint_fill_rect` with a 1 px height for the line.

```c
/* Bitmask constants */
#define Q_TD_UNDERLINE   (1u << 0)
#define Q_TD_OVERLINE    (1u << 1)
#define Q_TD_LINE_THROUGH (1u << 2)
```

---

## 11. `display:inline-block`  (box_tree.c, block_layout.c)

An inline-block box:
- In the outer BFC, it participates in line-wrap as a single atomic unit (like
  a replaced element).
- Internally it establishes its own BFC: measure width/height from its content.
- Emit it as `Q_BOX_INLINE_BLOCK` inside `Q_BOX_INLINE_CONTAINER` /
  `Q_BOX_LINE`; measure it via `q_layout_measure` before line-wrap.

---

## 12. `vertical-align`  (inline_layout.c)

In `q_layout_line_wrap` / `q_layout_position`:

After all boxes in a line are measured, compute the line box height by:
1. Finding the tallest box.
2. For each box, compute its vertical shift based on `vertical_align`:
   - `baseline` (default): place the box's baseline on the line baseline.
   - `top`: align top of box with top of line box.
   - `middle`: centre the box on the line's x-height midpoint.
   - `bottom`: align bottom of box with bottom of line box.
   - `sub` / `super`: offset baseline by a fixed fraction.

---

## 13. `border-radius`  (paint.c)

Add `float border_radius[4]` to `q_box_t` (top-left, top-right, bottom-right,
bottom-left), populated from `border-*-radius` CSS properties.

In `q_paint_box`, when any radius > 0:
- After filling the background rectangle, alpha-mask the corners by drawing
  a filled circle quadrant at each corner (CPU rasterisation with simple
  8-bit coverage anti-aliasing).
- Clip the borders to the same rounded outline.

```c
/* Rasterise a filled rounded rectangle into an RGBA8 buffer. */
void q_paint_fill_rounded_rect(uint8_t *pixels, int buf_w, int buf_h,
                                int x, int y, int w, int h,
                                const float radius[4], uint32_t color);
```

---

## 14. Basic Form Widgets (visual only)

No JavaScript execution, no form submission.  Goals: visually distinguish
interactive elements so they look recognisable.

| Element | Rendering |
|---------|-----------|
| `<input type="text">` | Block box with 1 px inset border, white background, text content from `value` attribute |
| `<input type="checkbox">` | 14×14 px box with check mark if `checked` attribute present |
| `<input type="radio">` | 14×14 px circle with inner dot if `checked` |
| `<input type="submit">`, `<button>` | Raised block with grey gradient background |
| `<select>` | Block box with down-arrow indicator at right |
| `<textarea>` | Multi-line text box respecting `rows`/`cols` |

These are handled in box_tree.c by a `q_form_widget_box()` helper that emits a
specially-styled block box with its tile pre-painted by `q_paint_form_widget()`.

---

## 15. URL Resolution  (`src/resource/resource.c`)

Currently only absolute `file://` URLs work.  Stage 2 adds:

```c
/* Resolve a URL reference (possibly relative) against a base URL.
 * Returns a malloc'd absolute URL string (caller frees).
 * For "file://" bases: constructs the correct path.
 * Uses lexbor's lxb_url_* module for parsing/composing. */
char *q_url_resolve(const char *base_url, const char *ref);
```

This is needed for loading `<img src="...">` and `<link rel="stylesheet" href="...">` with relative paths.

---

## 16. Extended View / Compositor API

```c
/* ── Scroll API ── */
void q_view_scroll_by(quanton_view_t *view, float dx, float dy);
void q_view_scroll_to(quanton_view_t *view, float x,  float y);
void q_view_scroll_into_view(quanton_view_t *view, const q_box_t *box);

/* ── Image cache lifecycle ── */
q_image_cache_t *q_image_cache_create(void);
void             q_image_cache_destroy(q_image_cache_t *cache);

/* ── New dirty flag ── */
/* Q_DIRTY_SCROLL (= 1 << 3) — composite-only, no relayout */

/* ── New event type ── */
Q_EVENT_SCROLL      /* fired when view->scroll_y / scroll container scroll_y changes */
```

---

## 17. Extended `quanton.h` additions

```c
/* Scroll */
void q_view_scroll_by(quanton_view_t *view, float dx, float dy);
void q_view_scroll_to(quanton_view_t *view, float x,  float y);
void q_view_scroll_into_view(quanton_view_t *view, const q_box_t *box);

/* Image cache */
q_image_cache_t *q_image_cache_create(void);
void             q_image_cache_destroy(q_image_cache_t *cache);
q_image_t       *q_image_cache_get(q_image_cache_t *cache, const char *url);
void             q_image_cache_release(q_image_cache_t *cache, q_image_t *img);

/* URL resolve */
char *q_url_resolve(const char *base_url, const char *ref);

/* New box types */
/* Q_BOX_IMAGE, Q_BOX_TABLE, Q_BOX_TABLE_SECTION, Q_BOX_TABLE_ROW,
 * Q_BOX_TABLE_CELL, Q_BOX_TABLE_CAPTION, Q_BOX_TABLE_COLUMN,
 * Q_BOX_FLOAT, Q_BOX_INLINE_BLOCK, Q_BOX_LIST_ITEM, Q_BOX_LIST_MARKER */

/* New dirty flag */
/* Q_DIRTY_SCROLL = 1 << 3 */

/* New event type */
/* Q_EVENT_SCROLL */
```

---

## 18. Implementation Order (recommended)

| Phase | What to build | New files |
|-------|---------------|-----------|
| 1 | URL resolver (`q_url_resolve`) | `src/resource/resource.c` (extend) |
| 2 | Image decoder + cache | `src/image/image.c`, `third_party/stb_image/stb_image.h` |
| 3 | `<img>` box support (measure + paint) | extend box_tree.c, paint.c |
| 4 | `overflow:hidden` clipping in compositor | extend paint.c |
| 5 | Root-level scroll + `Q_DIRTY_SCROLL` | extend composite.c, event.c, quanton.h |
| 6 | Scrollable containers + scrollbar rendering | extend block_layout.c, paint.c |
| 7 | Float context + float layout | `src/layout/float_layout.c` |
| 8 | `clear` property support | extend block_layout.c |
| 9 | Table anonymous box fixup | `src/layout/table_layout.c` |
| 10 | Table measure pass (no colspan/rowspan) | table_layout.c |
| 11 | Table position pass | table_layout.c |
| 12 | Table colspan / rowspan support | table_layout.c |
| 13 | `border-collapse` | table_layout.c, paint.c |
| 14 | `white-space:pre` / nowrap | extend inline_layout.c, box_tree.c |
| 15 | `<br>` line break handling | extend box_tree.c, inline_layout.c |
| 16 | `display:inline-block` | extend box_tree.c, block_layout.c |
| 17 | `vertical-align` | extend inline_layout.c |
| 18 | `text-decoration` | extend paint.c |
| 19 | List items + markers | extend box_tree.c, block_layout.c, paint.c |
| 20 | `border-radius` | extend paint.c |
| 21 | `background-image` | extend box_tree.c, paint.c |
| 22 | Form widgets (visual) | extend box_tree.c, paint.c |

---

## 19. Testing Strategy

Each new feature gets:
1. A minimal HTML test file under `tests/html/`.
2. A headless PNG render test in `test_quanton.c` (added to the `make test_png` targets).
3. Pixel-level assertions for at least one known region of the output.

Suggested new test cases:

| File | Tests |
|------|-------|
| `tests/html/table_basic.html` | Simple 2×2 table, cell borders, background colours |
| `tests/html/table_colspan.html` | colspan=2 cell, correct column widths |
| `tests/html/table_thead_tfoot.html` | thead/tbody/tfoot sections |
| `tests/html/float_text_wrap.html` | Left float, text wrapping around it |
| `tests/html/overflow_hidden.html` | Overflow hidden clipping |
| `tests/html/scroll_container.html` | overflow:auto with scrollbar |
| `tests/html/img_element.html` | `<img>` with PNG source |
| `tests/html/list_ol_ul.html` | `<ol>` and `<ul>` with default markers |
| `tests/html/whitespace_pre.html` | `<pre>` block preserving whitespace |
| `tests/html/inline_block.html` | `display:inline-block` boxes side by side |
| `tests/html/text_decoration.html` | Underline and strikethrough |

---

## 20. What We Get "for Free" from lexbor in Stage 2

| Feature | lexbor path |
|---------|-------------|
| Table display-type constants | `css/property/const.h` → `LXB_CSS_DISPLAY_TABLE*` |
| Overflow constants | `css/property/const.h` → `LXB_CSS_OVERFLOW_X_*` |
| Float / clear types | `css/property/const.h` → `LXB_CSS_FLOAT_*`, `LXB_CSS_CLEAR_*` |
| White-space types | `css/property/const.h` → `LXB_CSS_WHITE_SPACE_*` |
| text-decoration struct | `css/property.h` → `lxb_css_property_text_decoration_t` |
| vertical-align struct | `css/property.h` → `lxb_css_property_vertical_align_t` |
| line-height struct | `css/property.h` → `lxb_css_property_line_height_t` |
| Table HTML element types | `html/interfaces/table_element.h` etc. |
| URL parsing for relative URLs | `lexbor/url/url.h` → `lxb_url_*` |
| HTML tag IDs for `<table>`,`<tr>`,`<td>`,`<img>`,`<br>`,`<li>` | `html/tag.h` → `LXB_TAG_TABLE` etc. |

We do **not** need to implement parsing of any of these CSS properties or HTML
element types ourselves.
