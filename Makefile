CC ?= cc
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

all: build/greenlink build/greenlink-catalog
build/greenlink-catalog: src/catalog.c
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/catalog.c -o $@ $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c
build/greenlink: src/shell.c src/font.h src/video_layout.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/shell.c -o $@ $(SDL_LIBS)

test: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/shell_test.c -o build/shell-test $(SDL_LIBS)
	./build/shell-test
	python3 tests/catalog_test.py
	SDL_VIDEODRIVER=dummy ./build/greenlink --screenshot build/preview.bmp

.PHONY: all test
