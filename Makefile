CC ?= cc
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

all: build/greenlink build/greenlink-catalog
build/greenlink-catalog: src/catalog.c src/games.h src/kissanime.h src/megaplay.h src/archive.h src/tv_catalog.h src/tv_progress.h src/tv_history.h src/browse_filters.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/catalog.c -o $@ $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
build/greenlink: src/shell.c src/font.h src/video_layout.h src/tv_schedule.h src/tv_progress.h src/tv_history.h src/browse_filters.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/shell.c -o $@ $(SDL_LIBS) -lcurl

test: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/tv_test.c -o build/tv-test $(SDL_LIBS) -lcurl
	./build/tv-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/archive_test.c -o build/archive-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/archive-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/playback_menu_test.c -o build/playback-menu-test $(SDL_LIBS) -lcurl
	./build/playback-menu-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/shell_test.c -o build/shell-test $(SDL_LIBS) -lcurl
	./build/shell-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/auto_server_test.c -o build/auto-server-test $(SDL_LIBS) -lcurl
	./build/auto-server-test
	$(CC) $(CFLAGS) tests/disk_buffer_test.c -o build/disk-buffer-test $$(pkg-config --cflags --libs libavformat libavcodec libavutil) -lpthread
	./build/disk-buffer-test
	$(CC) $(CFLAGS) tests/subtitle_text_test.c -o build/subtitle-text-test
	./build/subtitle-text-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/kissanime_test.c -o build/kissanime-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/kissanime-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/archive_rom_test.c -o build/archive-rom-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/archive-rom-test
	python3 tests/games_test.py
	python3 tests/catalog_test.py
	python3 tests/update_test.py
	$(CC) $(CFLAGS) tests/resolver_test.c -o build/resolver-test -lcurl -ljson-c -lcrypto
	./build/resolver-test
	SDL_VIDEODRIVER=dummy ./build/greenlink --screenshot build/preview.bmp

.PHONY: all test

linux:
	bash scripts/build-linux.sh

test-linux:
	python3 tests/linux_update_test.py

.PHONY: linux test-linux

psp:
	bash scripts/build-psp.sh

test-psp:
	bash scripts/test-psp-host.sh

.PHONY: psp test-psp
