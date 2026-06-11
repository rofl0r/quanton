# LEXBOR-API

Quick reference for the bundled `lexbor` submodule.

## Umbrella headers

- `lexbor/core/lexbor.h` — memory hooks (`lexbor_malloc`, `lexbor_realloc`, `lexbor_calloc`, `lexbor_free`) and custom allocator setup.
- `lexbor/dom/dom.h` — DOM base types, node/element/document interfaces, collections, exceptions, and shadow DOM support.
- `lexbor/html/html.h` — HTML parser/tree/tokenizer/serializer entry points plus per-element interface headers under `html/interfaces/`.
- `lexbor/css/css.h` — CSS parser, stylesheet, declarations, rules, properties, values, units, syntax, and selector parsing helpers.
- `lexbor/selectors/selectors.h` — selector engine (`lxb_selectors_t`) and search/match callbacks.

## Main subsystems

- **Core**: allocation, strings, buffers, hash/maps, dynamic objects, parsing helpers, logging, and low-level utilities.
- **DOM**: `lxb_dom_node_t`, `lxb_dom_element_t`, `lxb_dom_document_t`, attribute/character-data/text/comment/document-fragment interfaces, collections, exceptions.
- **HTML**: tokenizer, tree builder, tag tables, serialization, encoding hooks, and generated element interfaces for HTML tags.
- **CSS**: rule/declaration/property/value representation, parser, tokenizer, syntax state machines, selector syntax, and at-rule support.
- **Selectors**: node search and selector matching over DOM trees, with options for root matching and first-match behavior.
- **Style**: computed-style plumbing and element-specific style interfaces.
- **Other modules**: encoding, URL, Unicode, punycode, tag lookup, ports, and assorted utilities.

## Common types

- `lxb_status_t` — lexbor status/result code.
- `lexbor_str_t`, `lexbor_mraw_t`, `lexbor_dobject_t` — string, memory, and dynamic object helpers used across subsystems.
- `lxb_css_selector_t`, `lxb_css_selector_list_t`, `lxb_css_rule_*`, `lxb_css_style_*` — CSS AST and serialization structures.
- `lxb_dom_*` and `lxb_html_*` interface structs — the primary tree/document objects consumed by Quanton.

## Practical notes

- Most public APIs live in the umbrella headers above; the rest are per-module or per-element implementation details.
- The `html/interfaces/` and `style/html/interfaces/` trees contain generated wrappers for specific HTML elements.
- If you need a symbol, search by module first (`core`, `dom`, `html`, `css`, `selectors`, `style`).
