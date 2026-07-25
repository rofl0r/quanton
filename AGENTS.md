# Quanton Agent Notes (short)

- `lexbor` is a git submodule: run `git submodule update --init --recursive`.
- Install system dependencies before building: `sudo apt-get install -y libpng-dev libsdl2-dev`
  (required to compile and run `make test_png` and to evaluate `output*.png` results, and to build the SDL2 backend targets).
- Read `LEXBOR-API.md` for the bundled lexbor reference and `API.md` for Quanton’s public API; treat `API.md` as a living document that may change as the code evolves.
- Main commands:
  - `make all`
  - `make test`
  - `make test_png` (needs `libpng-dev`, headless, auto-runs)
  - `make test_x11` / `make test_sdl2` (compile-only in headless envs)
- Keep warnings non-fatal: do not add `-Werror`.
- For ASAN validation, pass `-fsanitize=address` in both `CFLAGS` and `LDFLAGS`.
- After running `make test_png`, visually inspect the `output*.png` files it writes to verify correct rendering (boxes, text, colors, layout look reasonable).
- Before committing, make sure you didn't check in binaries.
- Keep each logical change in a separate commit.
- Write detailed commit messages that explain the rationale behind each change.
