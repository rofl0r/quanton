# Quanton — Hyperlink Architecture: Design Brainstorm

## Context

Quanton currently has no navigation concept.  A view loads exactly one document
and stays there.  Stage 3 adds `<a href>` support, which immediately raises the
question: *what happens when the user clicks a link?*

Two classes of link destination exist:

1. **Dumb resources** — files (images, plain HTML, CSS) that quanton can render
   as-is without any C code driving them.
2. **Smart applets** — C code that constructs or mutates HTML in response to
   user interaction (think a PHP-like server-side page, but in-process).

The following sections brainstorm both a URL-scheme approach and two callback
routing models, then weigh the pros and cons of each.

---

## 1. URL Schemes

Quanton currently plans to support:

| Scheme | Meaning |
|--------|---------|
| `file://` | Filesystem file |
| `app://` | In-memory resource registered with `q_app_resource_register()` |

### 1.1 Inferring "dumb vs smart" from URL scheme

One option is to reserve a *third* scheme, e.g. `cgi://`, for smart applets:

```
<a href="cgi://fileopen?path=/home/user">Open</a>
```

**Pros:**
- Zero ambiguity: scheme is the discriminator.  The caller never has to guess.
- `file://` and `app://` stay purely data-oriented; `cgi://` is always code.
- A future in-process "CGI" registry (`q_cgi_register("fileopen", handler_fn)`)
  fits cleanly here.

**Cons:**
- Non-standard scheme; existing HTML from the web would never use it.
- Requires a third registry separate from the `app://` resource registry.
- The designer must decide two things when writing a link: scheme *and* path.

### 1.2 Inferring from file extension

Designate a fixed extension, e.g. `.qcgi` or `.applet`, as "smart":

```
<a href="file:///apps/filebrowser.qcgi">Browse files</a>
```

**Pros:**
- Works with existing `file://` and `app://` schemes.
- Familiar convention (like CGI scripts in early web servers).
- Can glob `*.qcgi` under a directory to enumerate available applets.

**Cons:**
- Extension-based dispatch is fragile and surprising.
  A file named `foo.qcgi` is treated as an applet; `foo.html` is not.
- The app developer must use the right extension; easy to forget.
- Does not naturally extend to `app://` resources.

### 1.3 Leave it entirely to the host application

Quanton fires `on_navigate(view, href, userdata)` for every non-anchor link
click and does nothing else.  The host application reads `href`, decides whether
to load a new document (`q_document_load_html`), call an applet function, or
ignore the click.

**Pros:**
- Maximum flexibility; quanton imposes no policy.
- No new URL scheme or extension convention needed.
- The application can mix strategies: `file://` links navigate documents,
  `app://` links invoke handlers, custom schemes (`myapp://`) do anything.

**Cons:**
- Every application must implement its own dispatch table.
- More boilerplate in user code.
- Easy to forget to handle navigation, leaving links silently inert.

**This is the recommended approach for now** (see §4).

---

## 2. Routing Models

### 2.1 Flask-style route registration

The host registers named handler functions with URL-pattern matchers:

```c
q_router_t *router = q_router_create();
q_router_add(router, "file://*",             file_navigate_handler, NULL);
q_router_add(router, "app://fileopen",        fileopen_handler,      NULL);
q_router_add(router, "app://settings",        settings_handler,      NULL);
view.router = router;
```

When a link is clicked quanton calls `q_router_dispatch(view, href)` which
finds the first matching pattern and calls its handler.

```c
typedef void (*q_route_handler_t)(quanton_view_t *view,
                                  const char *href,
                                  const q_params_t *params,
                                  void *userdata);
```

`q_params_t` carries parsed URL query parameters (e.g. `?path=/home`).

**Pros:**
- Clean separation of concerns: routing logic lives in one place.
- URL-parameter parsing (`?key=value`) is provided by the framework.
- Easy to add middleware (auth, logging) in the future.
- Familiar to anyone who has used Flask, Express, etc.
- Applet handlers naturally look like mini request-handlers.

**Cons:**
- Requires a router module (~200 loc) to implement pattern matching +
  param parsing.
- Pattern syntax must be defined and documented.
- Slightly more setup code for simple single-document applications.
- Wildcard matching adds complexity (glob vs. prefix vs. regex).

### 2.2 Single `on_navigate` callback + manual dispatch

One callback, app does its own routing in a switch/if-else:

```c
static void on_navigate(quanton_view_t *view, const char *href, void *ud)
{
    if (strncmp(href, "file://", 7) == 0) {
        load_file(view, href + 7);
    } else if (strcmp(href, "app://settings") == 0) {
        open_settings(view);
    } else if (strcmp(href, "app://fileopen") == 0) {
        open_filebrowser(view);
    }
}
view.on_navigate = on_navigate;
view.on_navigate_userdata = &app;
```

**Pros:**
- Zero framework code.  One new field on `quanton_view_t`.
- Completely transparent; no hidden magic.
- The same pattern the filebrowser demo already uses for click events
  (`q_event_find_delegate` + manual attribute inspection).
- Trivially extensible: `data-action` attributes can add additional semantic
  information without changing the framework.

**Cons:**
- Every application reinvents the dispatch loop.
- URL parameter parsing (`?key=val`) must be done by the app.
- Harder to build generic link-following behaviour (e.g., auto-navigate all
  `file://` links without writing the same handler in every app).

---

## 3. The `q_event_find_delegate` Pattern — Explained

The filebrowser uses this call:

```c
delegate = q_event_find_delegate(event->target, "data-sort");
```

`q_event_find_delegate(node, attr_name)` walks **up the DOM tree** from the
hit-tested node (`event->target`) until it finds an ancestor element (or the
node itself) that has the named attribute.

Why is this useful?

When a user clicks on a table header (`<th>`), the actual hit-tested node might
be a text node or a deeply nested `<span>` inside the `<th>`.  Rather than
requiring every leaf node to carry the `data-sort` attribute, you set it once on
the `<th>` and let the walk bubble up to find it.  This is a lightweight
equivalent of JavaScript's **event delegation** pattern:

```html
<!-- Only the <th> carries data-sort; the click can land on a child node -->
<th data-sort="name">
  <span class="sort-arrow">▲</span> Name
</th>
```

The function stops at the first ancestor with the attribute, so you can nest
delegated handlers and the inner-most one wins.  If no ancestor has the
attribute, it returns `NULL` and the event is ignored.

This pattern is intentionally simple: it replaces the JavaScript idiom of
calling `element.closest('[data-sort]')` and is O(depth) in DOM tree depth.

---

## 4. Recommended Design for Stage 3

1. **Implement `on_navigate` callback only** (§2.2) for Stage 3.  It costs one
   field on `quanton_view_t` and ~20 lines in event.c.  This unblocks real
   navigation without committing to a router API.

2. **No new URL scheme for applets yet.**  Applications can use `app://` URLs
   with the existing resource registry for data, and any URL string for applet
   dispatch — the framework is indifferent.

3. **Named-anchor links** (`href="#id"`) are handled internally by quanton
   (scroll to the target element) and do not fire `on_navigate`.

4. **Revisit routing in Stage 4** once there are two or three real demo
   applications that need it.  At that point the recurring patterns in their
   `on_navigate` handlers will reveal exactly what the router needs to support.

5. **Extension or `cgi://` scheme** can be added later as a thin wrapper over
   `on_navigate` once the routing model is clearer.  The filebrowser's
   `data-sort` delegation pattern shows that a `data-action` attribute on
   links combined with `q_event_find_delegate` is often sufficient without any
   URL scheme at all.

---

## 5. Summary of Pros / Cons

| Approach | Flexibility | Framework code | App boilerplate | URL portability |
|---|---|---|---|---|
| Fixed extension (`.qcgi`) | Low | S | Low | Poor |
| New `cgi://` scheme | Medium | S | Low | Poor |
| `on_navigate` callback | High | Minimal | Medium | Good |
| Flask-style router | High | M–L | Low | Good |

For an embedded C UI toolkit the callback approach wins short-term because it
imposes the least on applications that are simple.  A router can be added as an
opt-in helper library later without breaking any existing code.
