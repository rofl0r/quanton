# Quanton — Stage 3 Implementation Plan

## Status: Stage 2 complete (see `IMPLEMENTATION_PLAN_STAGE2_DONE.md`)

Stage 2 delivered: table layout (colspan/rowspan, thead/tbody/tfoot, border-collapse),
float layout (float:left/right, clear), overflow:hidden clipping, scrollable
containers with scrollbars, image support (`<img>`, background-image), list items
with markers, white-space:pre/nowrap, text-decoration, display:inline-block,
vertical-align, border-radius, and basic form widget visuals.

---

## 1. Guiding Principles (inherited + extended)

* **Reuse lexbor maximally.** DOM, CSS cascade, selector matching, HTML parsing,
  and CSS property structs are all provided. We write nothing that lexbor already
  does.
* **Every layout box keeps a pointer to its originating `lxb_dom_node_t`**; we
  never duplicate attribute storage.
* **Box-based rendering.** Each block box is a self-contained pixel tile. Tiles
  can be handed to a GPU as textures.
* **Swappable backend.** Window creation and event polling are behind a thin
  vtable; X11 and SDL2 are two implementations.
* **No network.** Only `file://` and `app://` (in-memory resource registry) URL
  schemes are supported.
* **Reuse existing layout rules.** Heading elements `<h1>`–`<h6>` are just block
  boxes with a different default font size and bold weight — no new box type
  needed. `<b>`/`<strong>`/`<i>`/`<em>` are inherited inline styles. `<hr>` is a
  block box with a border and zero height.
* **Complexity budget per item.** Each item below carries an estimate:
  `S` = Small (< half a day), `M` = Medium (1–2 days), `L` = Large (3–5 days).

---

## 2. Stage 3 Goals

| Priority | Feature | Complexity |
|----------|---------|------------|
| **P0** | Heading elements `<h1>`–`<h6>` (font-size + bold defaults) | S |
| **P0** | `<b>` / `<strong>` — bold text | S |
| **P0** | `<i>` / `<em>` — italic text (font slant via libschrift) | S |
| **P0** | `<hr>` — horizontal rule | S |
| **P0** | `<a href>` — hyperlinks (visual + click navigation) | M |
| **P1** | `font-size`, `font-weight`, `font-style` CSS properties | M |
| **P1** | `color` CSS property for text | M |
| **P1** | `<code>` / `<pre>` / `<kbd>` — monospace font rendering | S |
| **P1** | `<blockquote>` — indented block with left margin | S |
| **P1** | `<s>` / `<del>` — strikethrough (already have text-decoration machinery) | S |
| **P1** | `<sup>` / `<sub>` — superscript/subscript (vertical-align already present) | S |
| **P2** | Functional text input widgets (keyboard focus + caret + text entry) | L |
| **P2** | Functional `<button>` (click fires DOM event, app handles it) | M |
| **P2** | Functional `<select>` (click opens a dropdown painted by quanton) | L |
| **P2** | `<textarea>` functional (multi-line edit) | L |
| **P2** | `<input type="checkbox">` / `<input type="radio">` (toggleable) | M |
| **P3** | CSS `margin: auto` horizontal centering | M |
| **P3** | CSS `min-width` / `max-width` / `min-height` / `max-height` | M |
| **P3** | CSS `text-align` (left / center / right / justify) | M |
| **P3** | `app://` in-memory resource registry | M |
| **P3** | Named anchor scrolling (`<a href="#section">` jumps to id) | S |
| **P3** | `<title>` element mapped to window title | S |
| **P3** | External CSS stylesheet loading (`<link rel="stylesheet">`) | M |

---

## 3. Design Notes — Shared Code, No Duplication

### 3.1 Headings map onto existing block + font machinery

`<h1>`–`<h6>` require no new box type.  In `q_layout_build_tree_node()`
(box_tree.c), when the tag is `lxb_tag_H1` … `lxb_tag_H6`, emit a normal
`Q_BOX_BLOCK` (just as `<p>` does), but set two extra fields on it:

```c
box->font_size   = heading_font_sizes[level - 1]; /* {2.0, 1.5, 1.17, 1.0, 0.83, 0.67} em */
box->font_weight = 700;
box->margin_top  = box->font_size * 0.67f;
box->margin_bottom = box->font_size * 0.67f;
```

These already-planned fields (`font_size`, `font_weight`) are consumed by the
text measurement path in `q_layout_measure_text()`.

### 3.2 Bold / italic — font selection, not new box types

`<b>` / `<strong>` set `font_weight = 700` on the inline container; the font
cache already accepts a weight parameter and will pick the Bold face when
available.  `<i>` / `<em>` set `font_style = Q_FONT_STYLE_ITALIC`; libschrift
supports slant/italic face selection if the font provides a separate face file.
If only one face is available, a software skew transform (shear the glyph
bitmaps by ~12°) is used as fallback.

### 3.3 `<hr>` — one-liner in box_tree.c

A `<hr>` element becomes a `Q_BOX_BLOCK` with:

```c
box->style_height     = 0.0f;
box->border_width[0]  = 1.0f; /* top */
box->border_color[0]  = 0x888888FFu;
box->margin_top       = 4.0f;
box->margin_bottom    = 4.0f;
```

No new box type, no new paint code.

### 3.4 `<code>` / `<kbd>` / `<tt>` — monospace font family

These set `font_family = "monospace"` on the inline container.  The font cache
already resolves `"monospace"` to a system monospace font (e.g., DejaVu Sans
Mono).

### 3.5 `<blockquote>` — margin/padding defaults

A `<blockquote>` tag is emitted as `Q_BOX_BLOCK` with `margin_left = 40px` and
`margin_right = 40px` applied as defaults in box_tree.c, matching browser UA
stylesheet.  No new code.

### 3.6 `<s>` / `<del>` — reuse text-decoration bitmask

Set `text_decoration |= Q_TD_LINE_THROUGH` in box_tree.c for `<s>` and `<del>`
tags.  Already implemented in paint.c.

### 3.7 `<sup>` / `<sub>` — reuse vertical-align

Set `vertical_align = Q_VERTICAL_ALIGN_SUPER` (resp. `_SUB`) and reduce
`font_size` by 0.75× for the inline box.

---

## 4. New / Extended Data Structures

### 4.1 Font properties on `q_box_t`

```c
struct q_box {
    /* ... existing Stage 2 fields ... */

    /* Stage 3: per-box font overrides */
    float           font_size;          /* NaN = inherit */
    int             font_weight;        /* 0 = inherit; 400 = normal; 700 = bold */
    q_font_style_t  font_style;         /* Q_FONT_STYLE_NORMAL / ITALIC / OBLIQUE */
    char           *font_family;        /* NULL = inherit "sans-serif" */

    /* Stage 3: text colour */
    uint32_t        color;              /* RGBA8; 0 = inherit (black default) */
    int             has_color;          /* 1 when color was explicitly set */

    /* Stage 3: text-align */
    q_text_align_t  text_align;         /* Q_TEXT_ALIGN_LEFT/CENTER/RIGHT/JUSTIFY */

    /* Stage 3: hyperlink */
    char           *href;               /* NULL or malloc'd href string */

    /* Stage 3: widget state */
    q_widget_type_t widget_type;        /* Q_WIDGET_NONE/INPUT_TEXT/BUTTON/… */
    int             widget_focused;     /* 1 when this widget has keyboard focus */
    char           *widget_value;       /* owned; text content of input/textarea */
    size_t          widget_value_len;
    size_t          widget_caret;       /* caret position in value */
    int             widget_checked;     /* for checkbox/radio */
};
```

### 4.2 New enumerants

```c
typedef enum {
    Q_FONT_STYLE_NORMAL  = 0,
    Q_FONT_STYLE_ITALIC  = 1,
    Q_FONT_STYLE_OBLIQUE = 2,
} q_font_style_t;

typedef enum {
    Q_TEXT_ALIGN_LEFT    = 0,
    Q_TEXT_ALIGN_CENTER  = 1,
    Q_TEXT_ALIGN_RIGHT   = 2,
    Q_TEXT_ALIGN_JUSTIFY = 3,
} q_text_align_t;

typedef enum {
    Q_WIDGET_NONE         = 0,
    Q_WIDGET_INPUT_TEXT   = 1,
    Q_WIDGET_INPUT_SUBMIT = 2,
    Q_WIDGET_INPUT_CHECK  = 3,
    Q_WIDGET_INPUT_RADIO  = 4,
    Q_WIDGET_BUTTON       = 5,
    Q_WIDGET_SELECT       = 6,
    Q_WIDGET_TEXTAREA     = 7,
} q_widget_type_t;
```

### 4.3 Keyboard focus list in `quanton_view_t`

```c
struct quanton_view {
    /* ... existing fields ... */
    q_box_t  *focused_widget;   /* currently focused interactive box, or NULL */
};
```

A single pointer is sufficient; Tab key cycles focus through all widget boxes
in document order.

---

## 5. Feature Implementation Details

### 5.1 `font-size`, `font-weight`, `font-style`, `color` — CSS property reading

In `q_layout_build_tree_node()` (box_tree.c), after creating the box, read the
corresponding lexbor computed CSS properties:

```c
/* font-size */
const lxb_css_property_font_size_t *fs = lxb_dom_element_css_property_by_id(
    el, LXB_CSS_PROPERTY_FONT_SIZE);
if (fs && fs->type == LXB_CSS_VALUE__LENGTH)
    box->font_size = css_length_to_px(fs->data.length, parent_font_size);

/* font-weight */
const lxb_css_property_font_weight_t *fw = lxb_dom_element_css_property_by_id(
    el, LXB_CSS_PROPERTY_FONT_WEIGHT);
if (fw) box->font_weight = css_font_weight_to_int(fw);

/* color */
const lxb_css_property_color_t *col = lxb_dom_element_css_property_by_id(
    el, LXB_CSS_PROPERTY_COLOR);
if (col) { box->color = css_color_to_rgba8(col); box->has_color = 1; }
```

Pass `font_size` and `font_weight` down to `q_font_match()` in
`q_layout_measure_text()`.

**Note:** Inheritance — iterate up the `q_box_t` parent chain to resolve
inherited values (font-size, color) when the box has NaN / 0 placeholders.  A
small `q_box_resolve_inherited_font()` helper centralises this.

Complexity: **M** (requires touching box_tree.c, block_layout.c, font.c,
paint.c).

### 5.2 `<a href>` — Hyperlinks

Visual rendering:
- Emit `<a>` as an inline container (already the default for inline elements).
- Apply UA stylesheet defaults: `color: #0000EE; text-decoration: underline`.
  These become defaults in box_tree.c when tag == `LXB_TAG_A` and no overriding
  CSS is present.

Navigation on click:
- In `q_event_dispatch()` (event.c), after hit-testing to find the target box,
  walk up the box tree to find the nearest ancestor with `href != NULL`.
- If found, call a new callback `view->on_navigate(view, href, userdata)`.
  The host application decides how to handle the URL (load a new document, open
  a file, call an applet handler, etc.).

```c
/* New callback on quanton_view_t */
void (*on_navigate)(quanton_view_t *view, const char *href, void *userdata);
```

Named anchors (`href="#id"`): call `q_view_scroll_into_view()` for the element
with the matching `id` attribute.  No callback fired.

External/applet navigation: fully delegated to `on_navigate`; quanton does not
interpret the URL.

Complexity: **M**

### 5.3 Text-align

In `q_layout_position()` (block_layout.c) for `Q_BOX_LINE` children:

After computing total content width of the line (sum of child widths), compute
`offset_x` based on `text_align` of the containing inline container:

```c
float line_content_w = sum of child widths on this line;
float offset_x = 0.0f;
if (align == Q_TEXT_ALIGN_CENTER)
    offset_x = (container_w - line_content_w) * 0.5f;
else if (align == Q_TEXT_ALIGN_RIGHT)
    offset_x = container_w - line_content_w;
else if (align == Q_TEXT_ALIGN_JUSTIFY && !is_last_line)
    /* distribute remaining space equally between gaps */
    gap = (container_w - line_content_w) / (child_count - 1);
```

Complexity: **M** (touches block_layout.c + inline_layout.c; justify requires
gap computation per line).

### 5.4 `margin: auto` centering

In `q_layout_measure()` (block_layout.c), when a block child has:
- explicit `style_width` set, and
- both `margin_left` and `margin_right` are NaN (meaning `auto`),

compute `auto_margin = (containing_w - child->width) * 0.5f` and set both
margins to that value.

Complexity: **M**

### 5.5 `min-width` / `max-width` / `min-height` / `max-height`

Add four `float` fields to `q_box_t` (`style_min_width`, `style_max_width`,
`style_min_height`, `style_max_height`), populate from CSS in box_tree.c, then
clamp the resolved `box->width` / `box->height` in `q_layout_measure()`:

```c
if (!isnan(box->style_max_width)  && box->width  > box->style_max_width)  box->width  = box->style_max_width;
if (!isnan(box->style_min_width)  && box->width  < box->style_min_width)  box->width  = box->style_min_width;
```

Complexity: **M**

### 5.6 Functional form widgets

Widgets that were "visual only" in Stage 2 are made interactive:

**Text input (`<input type="text">`, `<textarea>`)**

1. On `Q_EVENT_MOUSE_DOWN` inside a widget box with `widget_type == Q_WIDGET_INPUT_TEXT`:
   set `view->focused_widget = box`; set `Q_DIRTY_PAINT`.
2. On `Q_EVENT_KEY_DOWN` when `focused_widget != NULL`:
   - Printable characters: append to `widget_value` at `widget_caret`; advance caret.
   - Backspace: delete character before caret.
   - Arrow keys: move caret; Home/End clamp.
   - Enter: for `<input>` fire `Q_EVENT_SUBMIT`; for `<textarea>` insert newline.
3. Caret blink: driven by a timer tick (a `Q_EVENT_TIMER` type added to the event
   system, fired every 500 ms from `q_view_update` when a widget is focused).
4. During paint: draw a 1 px vertical line at caret x-position.

The `widget_value` string is authoritative; `q_dom_get_attribute("value")` reads
from it via a thin shim so apps can use `q_dom_get_attribute()` to get the
entered text.

Complexity: **L** (keyboard focus tracking, caret rendering, DOM sync).

**Button (`<button>`, `<input type="submit">`)**

On `Q_EVENT_MOUSE_UP` within the button box bounds, fire a synthetic
`Q_EVENT_MOUSE_CLICK` on the button's DOM node and call `view->on_event`.  The
host application handles the click.  Visual state: draw a "pressed" (darker
background) variant while the button is held (track `widget_focused` for
mouse-down state).

Complexity: **M**

**Checkbox / Radio (`<input type="checkbox/radio">`)**

On `Q_EVENT_MOUSE_UP`: toggle `widget_checked`.  For radio buttons, find all
other `<input type="radio">` with the same `name` attribute in the document and
clear their `widget_checked`.  Fire `Q_EVENT_CHANGE` on the target.

Complexity: **M** (radio group scan requires DOM walk).

**Select (`<select>`)**

On `Q_EVENT_MOUSE_DOWN`: paint a floating dropdown layer (a `Q_BOX_BLOCK` with
`position:absolute` appended to the root) listing all `<option>` children.  On
`Q_EVENT_MOUSE_UP` inside an option: update the selected option, remove the
dropdown layer, fire `Q_EVENT_CHANGE`.

Complexity: **L** (floating overlay layer, option list management).

### 5.7 `app://` in-memory resource registry

```c
typedef struct q_app_resource {
    char           *path;    /* e.g. "/icon.png" */
    const uint8_t  *data;    /* static or owned */
    size_t          size;
    const char     *mime;    /* "image/png", "text/html", … */
    int             owned;   /* 1 → free(data) on destroy */
} q_app_resource_t;

/* Register a resource.  path must begin with '/'. */
int   q_app_resource_register(quanton_ctx_t *ctx, const char *path,
                               const uint8_t *data, size_t size,
                               const char *mime, int take_ownership);

/* Remove a registered resource. */
void  q_app_resource_remove(quanton_ctx_t *ctx, const char *path);

/* Resolve an app:// URL to a registered resource (borrowed pointer). */
const q_app_resource_t *q_app_resource_lookup(quanton_ctx_t *ctx,
                                               const char *url);
```

In `q_resource_load()` (resource.c), when the URL scheme is `app://`, route to
`q_app_resource_lookup()` instead of the filesystem.

Internally the registry uses a simple sorted array + binary search, or a
hash-map if the entry count is expected to be large.

Complexity: **M**

### 5.8 External CSS stylesheet loading

In `q_document_load_html()` (box_tree.c or a new stylesheet.c), after parsing
the HTML, walk `<link rel="stylesheet" href="…">` elements:

1. Resolve the `href` against the document's base URL.
2. Load the CSS file via `q_resource_load()`.
3. Parse it with `lxb_css_stylesheet_create()` + `lxb_css_stylesheet_parse()`.
4. Attach to the document: `lxb_css_selectors_attach_stylesheet(doc, sheet)`.

Lexbor's cascade engine then applies it automatically during property resolution.

Complexity: **M**

### 5.9 `<title>` → window title

In `q_document_load_html()`, after parsing, find the `<title>` element:

```c
lxb_dom_element_t *title_el = lxb_dom_collection_element(
    lxb_html_document_title_element(html_doc), 0);
const char *title_text = lxb_dom_node_text_content(title_el, &len);
if (title_text && ctx->backend->set_title)
    ctx->backend->set_title(&view, title_text);
```

Add `set_title` to `q_backend_vt_t` (no-op for PNG backend).

Complexity: **S**

---

## 6. New / Extended Events

```c
typedef enum q_event_type {
    /* ... existing ... */
    Q_EVENT_FOCUS,      /* widget gained keyboard focus */
    Q_EVENT_BLUR,       /* widget lost keyboard focus */
    Q_EVENT_CHANGE,     /* checkbox/radio/select value changed */
    Q_EVENT_SUBMIT,     /* Enter in <input type="text"> or submit button click */
    Q_EVENT_NAVIGATE,   /* <a href> click with non-anchor href */
    Q_EVENT_TIMER,      /* internal: caret blink, not exposed to apps */
} q_event_type_t;
```

---

## 7. New Backend vtable Entry

```c
typedef struct q_backend_vt {
    /* ... existing ... */
    void (*set_title)(quanton_view_t *view, const char *title);
} q_backend_vt_t;
```

---

## 8. Implementation Order (recommended)

| Phase | What to build | Complexity | Files |
|-------|---------------|------------|-------|
| 1 | ~~`font-size` / `font-weight` / `color` CSS reading + inheritance~~ | M | box_tree.c, block_layout.c, font.c, paint.c |
| 2 | ~~`<h1>`–`<h6>` via tag defaults~~ | S | box_tree.c |
| 3 | ~~`<b>` / `<strong>` / `<i>` / `<em>` inline font style~~ | S | box_tree.c |
| 4 | ~~`<hr>` block box default styles~~ | S | box_tree.c |
| 5 | ~~`<code>` / `<kbd>` / `<tt>` monospace font family~~ | S | box_tree.c |
| 6 | ~~`<blockquote>` default margins~~ | S | box_tree.c |
| 7 | ~~`<s>` / `<del>` text-decoration defaults~~ | S | box_tree.c |
| 8 | ~~`<sup>` / `<sub>` vertical-align defaults~~ | S | box_tree.c |
| 9 | ~~`text-align` (left/center/right) in line positioning~~ | M | block_layout.c |
| 10 | ~~`margin: auto` centering~~ | M | block_layout.c |
| 11 | ~~`min/max-width/height`~~ | M | box_tree.c, block_layout.c |
| 12 | `<a href>` visual defaults + `on_navigate` callback | M | box_tree.c, event.c, quanton.h |
| 13 | Named anchor scroll (`href="#id"`) | S | event.c |
| 14 | ~~`<title>` → window title~~ | S | box_tree.c, backend vtable |
| 15 | `app://` resource registry | M | resource.c, quanton.h |
| 16 | External CSS `<link rel="stylesheet">` | M | box_tree.c (or stylesheet.c) |
| 17 | Functional `<button>` click + pressed state | M | event.c, paint.c |
| 18 | Functional checkbox / radio toggle | M | event.c, paint.c |
| 19 | Keyboard focus tracking + Tab cycle | M | event.c, quanton.h |
| 20 | Functional text input (keyboard + caret) | L | event.c, paint.c |
| 21 | `<textarea>` multi-line editing | L | event.c, paint.c |
| 22 | `<select>` dropdown overlay | L | event.c, paint.c, block_layout.c |
| 23 | `text-align: justify` | M | block_layout.c |

---

## 9. Testing Strategy

Each new feature gets:
1. A minimal HTML test file under `tests/html/`.
2. A headless PNG render test added to `test_quanton.c`.
3. Pixel-level or structural assertions.

Suggested new test cases:

| File | Tests |
|------|-------|
| `tests/html/headings.html` | h1–h6 font sizes, margins |
| `tests/html/bold_italic.html` | `<b>`, `<i>`, `<em>`, `<strong>` weight/style |
| `tests/html/hr.html` | `<hr>` renders as a 1 px horizontal line |
| `tests/html/code_block.html` | `<code>` monospace; `<pre>` + `<code>` |
| `tests/html/blockquote.html` | Left/right indentation |
| `tests/html/text_color.html` | CSS `color` on headings and paragraphs |
| `tests/html/font_size.html` | CSS `font-size` in px and em |
| `tests/html/text_align.html` | center, right, justify |
| `tests/html/margin_auto.html` | `margin: 0 auto` centred block |
| `tests/html/min_max_width.html` | `max-width` limiting a block |
| `tests/html/anchor_link.html` | `<a href>` underlined blue text |
| `tests/html/anchor_scroll.html` | `<a href="#id">` scrolls into view |
| `tests/html/app_resource.html` | `<img src="app://...">` via registry |
| `tests/html/form_button.html` | `<button>` click fires event |
| `tests/html/form_checkbox.html` | Checkbox toggle, radio group mutual exclusion |
| `tests/html/form_input.html` | Text entry, caret positioning |
