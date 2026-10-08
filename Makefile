CC ?= cc
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

all: build/greenlink build/greenlink-catalog
build/greenlink-catalog: src/catalog.c src/kissanime.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/catalog.c -o $@ $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c
build/greenlink: src/shell.c src/font.h src/video_layout.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/shell.c -o $@ $(SDL_LIBS)

test: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/playback_menu_test.c -o build/playback-menu-test $(SDL_LIBS)
	./build/playback-menu-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/shell_test.c -o build/shell-test $(SDL_LIBS)
	./build/shell-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/auto_server_test.c -o build/auto-server-test $(SDL_LIBS)
	./build/auto-server-test
	$(CC) $(CFLAGS) tests/disk_buffer_test.c -o build/disk-buffer-test $$(pkg-config --cflags --libs libavformat libavcodec libavutil) -lpthread
	./build/disk-buffer-test
	$(CC) $(CFLAGS) tests/subtitle_text_test.c -o build/subtitle-text-test
	./build/subtitle-text-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/kissanime_test.c -o build/kissanime-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c
	./build/kissanime-test
	python3 tests/catalog_test.py
	python3 tests/update_test.py
	$(CC) $(CFLAGS) tests/resolver_test.c -o build/resolver-test -lcurl -ljson-c -lcrypto
	./build/resolver-test
	SDL_VIDEODRIVER=dummy ./build/greenlink --screenshot build/preview.bmp

.PHONY: all test
