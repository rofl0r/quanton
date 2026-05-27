# Quanton Agent Notes (short)

- `lexbor` is a git submodule: run `git submodule update --init --recursive`.
- Main commands:
  - `make all`
  - `make test`
  - `make test_png` (needs `libpng-dev`)
  - `make test_x11` / `make test_sdl2` (compile-only in headless envs)
- Keep warnings non-fatal: do not add `-Werror`.
- For ASAN validation, pass `-fsanitize=address` in both `CFLAGS` and `LDFLAGS`.
