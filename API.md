# Quanton API

Public API reference for `include/quanton.h`.

## Core objects

- `quanton_ctx_t` — process-level context with font cache and backend vtable.
- `quanton_view_t` — one rendered view/window: document, layout root, framebuffer, scroll state, dirty flags, backend handle.
- `q_document_t` — Quanton document wrapper around the lexbor HTML document.

## Document and resource loading

- `q_url_resolve(base_url, ref)` — resolve relative references.
- `q_resource_load(url, out_len)` / `q_resource_free(buf)` — load and free file-backed resources.
- `q_document_create()` / `q_document_destroy()` — document lifetime.
- `q_document_load_url()` / `q_document_load_html()` — populate a document from URL or HTML.
- `q_document_handle()` / `q_document_base_url()` / `q_document_get_computed_style()` — access the wrapped lexbor document and computed style.

## Layout and box model

- `q_box_t` — layout tree node with geometry, DOM pointer, paint state, and children.
- `q_table_t` and related `q_table_*` structs — table layout bookkeeping.
- `q_layout_build_tree()` / `q_layout_free_tree()` — box tree construction.
- `q_layout_measure()` / `q_layout_position()` / `q_layout_position_absolute()` / `q_layout_line_wrap()` — layout passes.
- `q_table_fixup_anonymous()` / `q_table_measure()` / `q_table_position()` / `q_table_free()` — table-specific layout.
- `q_float_ctx_t` and `q_float_ctx_*()` — float placement and clearance helpers.

## Paint, composite, hit testing

- `q_paint_box()` / `q_paint_fill_rect()` / `q_paint_borders()` — box rasterization.
- `q_paint_composite()` / `q_paint_composite_clipped()` — framebuffer compositing.
- `q_composite_frame()` — copy the composed scene into the view framebuffer.
- `q_hit_test()` — locate the topmost box at a point.

## Events and view updates

- `q_event_t` / `q_event_type_t` — input and window events.
- `q_event_dispatch()` / `q_event_find_delegate()` — event routing and delegated handling.
- `q_dom_mark_dirty()` — mark DOM subtrees dirty.
- `q_view_update()` / `q_view_refresh()` — incremental or full refresh.
- `q_view_scroll_by()` / `q_view_scroll_to()` / `q_view_scroll_into_view()` — scrolling.
- `q_view_set_texture_cache_limit()` / `q_view_get_texture_cache_limit()` — per-view backend texture-cache cap control (bytes, `0` = backend default).

## DOM mutation helpers

- `q_dom_set_attr()` / `q_dom_remove_attr()` / `q_dom_set_text_content()` — mutate elements and schedule relayout.
- `q_dom_append_element()` / `q_dom_remove_node()` — structural DOM edits.
- `q_dom_add_class()` / `q_dom_remove_class()` / `q_dom_has_class()` — class helpers.
- `q_dom_query_selector()` / `q_dom_query_selector_all()` / `q_dom_get_element_by_id()` — DOM lookup helpers.
- `q_dom_set_inner_html()` — fragment parsing and replacement.

## Fonts and images

- `q_font_cache_t`, `q_font_t`, `q_glyph_t`, `q_shaped_run_t` — font cache and shaping data.
- `q_font_cache_create()` / `q_font_cache_destroy()` — cache lifetime.
- `q_font_load()` / `q_font_load_mem()` / `q_font_match()` — font selection and loading.
- `q_font_measure()` / `q_font_shape_run()` / `q_font_render_run()` / `q_shaped_run_free()` — text measurement and rendering.
- `q_image_load_url()` / `q_image_release()` / `q_image_pixels()` / `q_image_width()` / `q_image_height()` — image loading and access.

## Backend API

- `q_backend_vt_t` — backend vtable for window creation, event polling, teardown, runtime title updates (`set_title`), legacy framebuffer blits (`blit`), and optional backend-driven box-tile presentation (`render_view`).
- `q_backend_x11`, `q_backend_sdl2`, `q_backend_png` — built-in backend instances.

## Interactive controls API contract

- Widget interaction is reported through `view->on_event`:
  - `Q_EVENT_FOCUS` / `Q_EVENT_BLUR` for focus changes.
  - `Q_EVENT_CHANGE` for checkbox/radio value changes.
  - `Q_EVENT_MOUSE_CLICK` for button-like activation.
- Event targets:
  - `event->target_box` points to the hit widget/layout box.
  - `event->target` points to the corresponding DOM node.
- Widget state reads:
  - Use `q_dom_get_attribute(view, el, "value", &len)` for live text/button values.
  - Use `q_dom_get_attribute(view, el, "checked", &len)` for checkbox/radio state.
- Link/navigation handling:
  - Named anchors (`href="#id"`) are handled internally by scrolling.
  - Non-anchor links call `view->on_navigate(view, href, userdata)` if set.

## Important enums and flags

- `q_box_type_t`, `q_position_type_t`, `q_overflow_type_t`, `q_float_type_t`, `q_clear_type_t`
- `q_white_space_type_t`, `q_text_align_type_t`, `q_vertical_align_type_t`, `q_background_repeat_type_t`, `q_list_style_type_t`
- `q_font_style_t`, `q_dirty_flags_t`
- `Q_TEXT_DECORATION_*` bit flags

## Internal helpers worth knowing

- `src/layout/box_tree.c`
  - `parse_style_attribute()` — inline-style parser for layout-relevant CSS (`width/height`, min/max constraints, text-align, margins including `auto`, overflow, typography, borders/background, etc.).
  - `q_box_inherit_text_style()` — inheritance helper for text style and alignment properties across generated box nodes.
  - `q_layout_walk_node()` — DOM→box tree builder applying tag defaults and style parsing.
- `src/layout/block_layout.c`
  - `q_layout_apply_minmax()` — clamps measured dimensions to `min/max-width/height`.
  - `q_layout_measure_text()` / `q_layout_measure_image()` — intrinsic measurement helpers.
  - `q_layout_block_place_float()` / `q_layout_resolve_clear_y()` — float/clear placement logic.
- `src/dom_api/dom_api.c`
  - `q_view_update()` — rebuilds layout/paint and now forwards `<title>` changes to `backend->set_title` when available.

## Notes

- `q_dom_*` helpers generally mark the view dirty and rely on `q_view_update()` to rebuild and repaint.
- `API.md` should be treated as a living document; update it when Quanton’s public surface changes.
