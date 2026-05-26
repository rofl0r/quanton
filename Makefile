CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -O2 -Iinclude -I.
LDFLAGS = -lm

SRC = \
src/resource/resource.c \
src/integration/lexbor_shim.c \
src/font/font.c

OBJ = $(SRC:.c=.o)

all: libquanton.a

libquanton.a: $(OBJ)
ar rcs $@ $(OBJ)

test: tests/test_quanton.c $(SRC) include/quanton.h
$(CC) $(CFLAGS) -o $@ tests/test_quanton.c $(SRC) $(LDFLAGS)
./test

clean:
rm -f $(OBJ) libquanton.a test

.PHONY: all test clean
