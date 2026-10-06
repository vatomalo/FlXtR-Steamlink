CC ?= cc
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

all: build/greenlink
build/greenlink: src/shell.c src/font.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/shell.c -o $@ $(SDL_LIBS)

test: build/greenlink
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/shell_test.c -o build/shell-test $(SDL_LIBS)
	./build/shell-test
	SDL_VIDEODRIVER=dummy ./build/greenlink --screenshot build/preview.bmp

.PHONY: all test
