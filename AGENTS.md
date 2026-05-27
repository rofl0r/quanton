# Quanton Agent Notes

## Repository setup

The `lexbor` directory is a git submodule. Clone the repo recursively:

```sh
git clone --recurse-submodules <url>
```

Or after a plain clone:

```sh
git submodule update --init --recursive
```

## Build & test commands

```sh
make all        # build libquanton.a (also builds lexbor)
make test       # no graphics backend; headless unit test
make test_png   # headless, writes PNG files, auto-runs (needs libpng-dev)
make test_x11   # compile-only; requires a live X display to run
make test_sdl2  # compile-only; requires a live display to run
```

## CFLAGS preferences

- Do **not** add `-Werror`.  Warnings are informational; the build must not fail on them.
- Debug builds use `-O0 -g3 -gdwarf-3` (already the Makefile default).
- When validating code changes locally, override CFLAGS to include AddressSanitizer:

```sh
CFLAGS="-std=c99 -Wall -Wextra -O0 -g3 -gdwarf-3 -fsanitize=address -fno-omit-frame-pointer -Iinclude -I. -Ilexbor/source -Ithird_party" \
LDFLAGS="-lm -fsanitize=address" \
make test
```

  (The ASAN `-fsanitize=address` flag must appear in both CFLAGS *and* LDFLAGS so the runtime is linked.)
