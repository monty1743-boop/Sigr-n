CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -I../src
LDFLAGS ?= -lm

SRCS = preview.c ../src/glass_picture.c ../src/glass_paint.c ../src/glass_logic.c ../src/glass_body.c

.PHONY: all test clean

all: preview

preview: $(SRCS) ../src/glass.h
	$(CC) $(CFLAGS) -o preview $(SRCS) $(LDFLAGS)

test: preview
	./preview test

clean:
	rm -f preview
