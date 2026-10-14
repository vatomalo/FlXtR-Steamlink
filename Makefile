CC ?= cc
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

all: build/greenlink build/greenlink-catalog
build/greenlink-catalog: src/catalog.c src/romsgames.h src/games.h src/kissanime.h src/megaplay.h src/archive.h src/tv_catalog.h src/tv_progress.h src/tv_history.h src/browse_filters.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/catalog.c -o $@ $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
build/greenlink: src/shell.c src/child_process.h src/api_setup.h src/phone_features.h src/party_questions.h src/wifi_pairing.h src/vendor/qrcodegen.c src/vendor/qrcodegen.h src/font.h src/video_layout.h src/tv_schedule.h src/tv_airing.h src/tv_progress.h src/tv_history.h src/browse_filters.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/shell.c -o $@ $(SDL_LIBS) -lSDL2_image -lcurl

test: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/tv_test.c -o build/tv-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/tv-test
	$(CC) $(CFLAGS) tests/tv_channel_test.c -o build/tv-channel-test -ljson-c
	./build/tv-channel-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/archive_test.c -o build/archive-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/archive-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/playback_menu_test.c -o build/playback-menu-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/playback-menu-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/shell_test.c -o build/shell-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/shell-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/auto_server_test.c -o build/auto-server-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/auto-server-test
	$(CC) $(CFLAGS) tests/disk_buffer_test.c -o build/disk-buffer-test $$(pkg-config --cflags --libs libavformat libavcodec libavutil) -lpthread
	./build/disk-buffer-test
	$(CC) $(CFLAGS) tests/subtitle_text_test.c -o build/subtitle-text-test
	./build/subtitle-text-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/kissanime_test.c -o build/kissanime-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/kissanime-test
	python3 tests/games_test.py
	python3 tests/catalog_test.py
	python3 tests/update_test.py
	python3 tests/update_package_test.py
	python3 tests/api_setup_test.py
	$(CC) $(CFLAGS) tests/phone_state_test.c -o build/phone-state-test
	./build/phone-state-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/phone_playback_test.c -o build/phone-playback-test $(SDL_LIBS)
	./build/phone-playback-test
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

# Focused launch/performance tests, independent of legacy playback UI fixtures.
test-games: all
	$(CC) $(CFLAGS) tests/child_exec_test.c -o build/child-exec-test
	./build/child-exec-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/shutdown_test.c -o build/shutdown-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/shutdown-test
	python3 tests/games_test.py
	python3 tests/game_watchdog_test.py
.PHONY: test-games

test-romsgames: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/romsgames_test.c -o build/romsgames-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/romsgames-test
	node --check assets/phone.js
	node tests/romsgames_phone_test.js

.PHONY: test-romsgames
