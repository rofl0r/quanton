CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O0 -g3 -gdwarf-3 -Iinclude -I. -Ilexbor/source -Ithird_party

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
src/layout/inline_layout.c \
src/event/event.c \
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

SRC_X11  = src/backend/x11/x11_backend.c
SRC_SDL2 = src/backend/sdl2/sdl2_backend.c
SRC_PNG  = src/backend/png/png_backend.c

src/backend/x11/x11_backend.o: $(SRC_X11) include/quanton.h
	$(CC) $(CFLAGS) -c -o $@ $(SRC_X11)

src/backend/sdl2/sdl2_backend.o: $(SRC_SDL2) include/quanton.h
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -c -o $@ $(SRC_SDL2)

src/backend/png/png_backend.o: $(SRC_PNG) include/quanton.h
	$(CC) $(CFLAGS) -c -o $@ $(SRC_PNG)

# ─────────────────────────────────────────────────────────────────────────

test: lexbor_all tests/test_quanton.c libquanton.a include/quanton.h
	$(CC) $(CFLAGS) -o $@ tests/test_quanton.c libquanton.a $(LDFLAGS) $(LDFLAGS_LEXBOR)
	./test

# Backend-specific test binaries.
# test_png runs headlessly and auto-executes.
# test_x11 and test_sdl2 are compiled only (require a live display to run).
test_png: lexbor_all tests/test_quanton.c libquanton.a src/backend/png/png_backend.o include/quanton.h
	$(CC) $(CFLAGS) -DQUANTON_BACKEND_PNG -o $@ tests/test_quanton.c \
	    libquanton.a src/backend/png/png_backend.o $(LDFLAGS) $(LDFLAGS_LEXBOR) -lpng
	./$@

test_x11: lexbor_all tests/test_quanton.c libquanton.a src/backend/x11/x11_backend.o include/quanton.h
	$(CC) $(CFLAGS) -DQUANTON_BACKEND_X11 -o $@ tests/test_quanton.c \
	    libquanton.a src/backend/x11/x11_backend.o $(LDFLAGS) $(LDFLAGS_LEXBOR) -lX11

test_sdl2: lexbor_all tests/test_quanton.c libquanton.a src/backend/sdl2/sdl2_backend.o include/quanton.h
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -DQUANTON_BACKEND_SDL2 -o $@ tests/test_quanton.c \
	    libquanton.a src/backend/sdl2/sdl2_backend.o $(LDFLAGS) $(LDFLAGS_LEXBOR) $(SDL2_LDFLAGS)

clean:
	rm -f $(OBJ) libquanton.a \
	    test test_x11 test_sdl2 test_png output.png
	rm -f src/backend/x11/x11_backend.o src/backend/sdl2/sdl2_backend.o \
	    src/backend/png/png_backend.o

clean_lexbor:
	rm -rf build lexbor/build

.PHONY: all test test_x11 test_sdl2 test_png clean clean_lexbor lexbor_all
