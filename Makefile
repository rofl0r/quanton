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

SRC = \
src/resource/resource.c \
src/integration/lexbor_shim.c \
src/layout/box_tree.c \
src/layout/block_layout.c \
src/font/font.c \
third_party/libschrift/schrift.c

OBJ = $(SRC:.c=.o)

all: lexbor_all libquanton.a

# Build lexbor using its own makefile with correct paths
lexbor_all:
	$(MAKE) -f lexbor.mak TOPSRC=$(CURDIR)/lexbor all

libquanton.a: $(OBJ)
	ar rcs $@ $(OBJ)

test: lexbor_all tests/test_quanton.c libquanton.a include/quanton.h
	$(CC) $(CFLAGS) -o $@ tests/test_quanton.c libquanton.a $(LDFLAGS) $(LDFLAGS_LEXBOR)
	./test

clean:
	rm -f $(OBJ) libquanton.a test output.png
	rm -rf build lexbor/build

.PHONY: all test clean lexbor_all
