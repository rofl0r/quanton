# Visual inspection tool (`tools/visual_inspect.c`)

A headless, script-driven tool for visually inspecting quanton's rendering
and interactive (mouse/keyboard) behavior without a live X11/SDL2 display.
It is built on the PNG backend, so it works in any sandboxed/agentic
environment.

It loads a static HTML file, builds/measures/paints the layout tree, then
replays a small text script of synthetic input events (clicks, wheel
scrolling, key presses) against the resulting view. Any point in the script
can save a PNG snapshot and/or dump the box tree to stdout, so an agent
without a display can still verify things like scrollbar visibility,
form-widget caret/focus behavior, or checkbox/radio "checked" rendering.

## Build

```
make visual_inspect
```

(requires `libpng-dev`, same as `make test_png`).

## Usage

```
./visual_inspect <html_file> <script_file> [width] [height]
```

`width`/`height` default to 800x600.

## Script format

One command per line. Blank lines and `#` comments (to end of line) are
ignored. Coordinates are in viewport pixels.

| Command                    | Effect                                                              |
|-----------------------------|----------------------------------------------------------------------|
| `render <path.png>`         | Force a composite + blit and save the current frame to `path.png`   |
| `dump`                      | Print a box-tree dump (type, widget type/value/caret/checked, geometry) |
| `click <x> <y>`              | Synthesize mouse-down + mouse-up + click at (x, y)                   |
| `down <x> <y>`               | Synthesize mouse-down at (x, y)                                       |
| `up <x> <y>`                 | Synthesize mouse-up at (x, y)                                         |
| `move <x> <y>`               | Synthesize mouse-move to (x, y)                                       |
| `wheel <delta> <x> <y>`      | Synthesize a mouse-wheel event with the given delta at (x, y)         |
| `key <name>`                 | Synthesize a key-down; `name` is `left`/`right`/`home`/`end`/`backspace`/`delete`, or a single printable character (e.g. `a`) |

Key names map onto the same `Q_KEY_*` codes (`include/quanton.h`) that the
X11 and SDL2 backends translate their native key codes into, so results
match what real backends would dispatch.

## Example

```
$ cat script.txt
render before.png
click 60 169
key left
key left
key backspace
render after.png
dump

$ ./visual_inspect page.html script.txt
[render] before.png
[render] after.png
[dump]
block x=0 y=0 w=800 h=231
  ...
    block widget=input-text value="editme" caret=4 focused=1 x=8 y=158 w=140 h=22
  ...
```

Then view `before.png`/`after.png` (e.g. with the `view` tool in an agent
session) to visually confirm the change.

## Notes for future agents

- The initial `q_view_update()` call happens automatically before the
  script runs, so the first `render`/`dump` already reflects the fully
  laid-out and painted page.
- Event handlers inside quanton itself (`src/event/event.c`) already call
  `q_view_update()` when a click/keypress/scroll changes state, so results
  from `click`/`key`/`wheel` are immediately visible to a following `render`
  or `dump` without an extra step.
- `render` always forces a fresh composite+blit and writes to the given
  path, regardless of the view's internal dirty flags, so it's safe to call
  more than once with different filenames to get a before/after sequence.
- This is intentionally a small, generic tool (not tied to any one demo);
  point it at any HTML file to inspect layout/paint/interactive behavior.
