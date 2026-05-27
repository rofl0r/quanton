CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -I. -Ilexbor/source -Ithird_party

LEXBOR_LIBS = \
-llexbor-css \
-llexbor-html \
-llexbor-dom \
-llexbor-encoding \
-llexbor-engine \
-llexbor-ns \
-llexbor-punycode \
-llexbor-selectors \
-llexbor-style \
-llexbor-tag \
-llexbor-unicode \
-llexbor-url \
-llexbor-utils \
-llexbor-core

LDFLAGS = -lm
LDFLAGS_LEXBOR = -L build/make/lib $(LEXBOR_LIBS)

# SDL2 compile/link flags (auto-detected via sdl2-config; falls back to -lSDL2)
SDL2_CFLAGS  := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LDFLAGS := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(SDL2_CFLAGS),)
SDL2_CFLAGS  := -I/usr/include/SDL2 -D_REENTRANT
SDL2_LDFLAGS := -lSDL2
endif

SRC = \
src/resource/resource.c \
src/integration/lexbor_shim.c \
src/layout/box_tree.c \
src/layout/block_layout.c \
src/paint/paint.c \
src/paint/composite.c \
src/font/font.c \
third_party/libschrift/schrift.c

OBJ = $(SRC:.c=.o)

all: lexbor_all libquanton.a

# Build lexbor using its own makefile with correct paths
lexbor_all:
	$(MAKE) -f lexbor.mak TOPSRC=$(CURDIR)/lexbor all

libquanton.a: $(OBJ)
	ar rcs $@ $(OBJ)

# ── Optional backend targets ─────────────────────────────────────────────
# These are not built by default; link -lX11 or SDL2 when using them.
#
#   make libquanton-x11.a   (requires libX11-dev)
#   make libquanton-sdl2.a  (requires libSDL2-dev)

SRC_X11  = src/backend/x11/x11_backend.c
SRC_SDL2 = src/backend/sdl2/sdl2_backend.c

libquanton-x11.a: $(OBJ) src/backend/x11/x11_backend.o
	ar rcs $@ $(OBJ) src/backend/x11/x11_backend.o

libquanton-sdl2.a: $(OBJ) src/backend/sdl2/sdl2_backend.o
	ar rcs $@ $(OBJ) src/backend/sdl2/sdl2_backend.o

src/backend/x11/x11_backend.o: $(SRC_X11) include/quanton.h
	$(CC) $(CFLAGS) -c -o $@ $(SRC_X11)

src/backend/sdl2/sdl2_backend.o: $(SRC_SDL2) include/quanton.h
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -c -o $@ $(SRC_SDL2)

# ─────────────────────────────────────────────────────────────────────────

test: lexbor_all tests/test_quanton.c libquanton.a include/quanton.h
	$(CC) $(CFLAGS) -o $@ tests/test_quanton.c libquanton.a $(LDFLAGS) $(LDFLAGS_LEXBOR)
	./test

clean:
	rm -f $(OBJ) libquanton.a libquanton-x11.a libquanton-sdl2.a test output.png
	rm -f src/backend/x11/x11_backend.o src/backend/sdl2/sdl2_backend.o
	rm -rf build lexbor/build

.PHONY: all test clean lexbor_all
