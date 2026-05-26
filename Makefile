CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O0 -I.

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
	-llexbor-core \

LDFLAGS = -lm -lpng
LDFLAGS_LEXBOR = -L build/make/lib $(LEXBOR_LIBS)

# Build lexbor using its own makefile with correct paths
lexbor_all:
	$(MAKE) -f lexbor.mak TOPSRC=$(CURDIR)/lexbor all


# Targets
test: test.c libschrift/schrift.c
	$(CC) $(CFLAGS) -o $@ test.c libschrift/schrift.c $(LDFLAGS) $(LDFLAGS_LEXBOR)

clean:
	rm -f test output.png
	rm -rf lexbor/build

.PHONY: clean lexbor_all
