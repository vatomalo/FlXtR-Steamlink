CC ?= cc
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

all: build/greenlink build/greenlink-catalog
build/greenlink: src/power_activity.h src/usb_setup_ui.h src/mtv_weights_ui.h src/mtv_weights.h
build/greenlink-catalog: src/rom_state_preferences.h src/rom_state_runtime.h
build/greenlink-catalog: src/catalog.c src/rom_cover_preferences.h src/rom_cheat_runtime.h src/series_navigation.h src/response_cache.h src/romsgames.h src/games.h src/game_scaling.h src/rom_core_preferences.h src/core_geometry.h src/vendor/libretro.h src/kissanime.h src/megaplay.h src/archive.h src/tv_catalog.h src/tv_schedule.h src/tv_progress.h src/tv_history.h src/tv_play_history.h src/game_output.h src/browse_filters.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/catalog.c -o $@ $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
build/greenlink: src/shell.c src/rom_assets_ui.h src/desktop_theme.h src/youtube_video.h src/music_ui.h src/child_process.h src/platform_process.h src/platform_files.h src/platform_paths.h src/platform_net.h src/platform_pipe.h src/api_setup.h src/phone_features.h src/server_preferences.h src/mtv_preferences.h src/preview_inset.h src/party_questions.h src/wifi_pairing.h src/vendor/qrcodegen.c src/vendor/qrcodegen.h src/font.h src/video_layout.h src/tv_schedule.h src/tv_airing.h src/tv_progress.h src/tv_history.h src/tv_play_history.h src/game_output.h src/settings_tabs.h src/settings_services.h src/rom_core_preferences.h src/game_scaling.h src/browse_filters.h
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
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/tv_source_failure_test.c -o build/tv-source-failure-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/tv-source-failure-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/library_preferences_test.c -o build/library-preferences-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/library-preferences-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/preview_settings_test.c -o build/preview-settings-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/preview-settings-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/rom_settings_test.c -o build/rom-settings-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/rom-settings-test
	python3 tests/rom_launch_preferences_test.py
	python3 tests/rom_assets_test.py
	python3 tests/state_guard_test.py
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/rom_assets_ui_test.c -o build/rom-assets-ui-test $(SDL_LIBS) -lSDL2_image -lcurl
	SDL_VIDEODRIVER=dummy ./build/rom-assets-ui-test
	$(CC) $(CFLAGS) tests/disk_buffer_test.c -o build/disk-buffer-test $$(pkg-config --cflags --libs libavformat libavcodec libavutil) -lpthread
	./build/disk-buffer-test
	$(CC) $(CFLAGS) tests/subtitle_text_test.c -o build/subtitle-text-test
	./build/subtitle-text-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/kissanime_test.c -o build/kissanime-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/kissanime-test
	python3 tests/games_test.py
	python3 tests/playstation_iso_test.py
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

test-response-cache: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/response_cache_test.c -o build/response-cache-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/response-cache-test

.PHONY: test-response-cache

test-series: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/series_navigation_test.c -o build/series-navigation-test $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	./build/series-navigation-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/next_episode_test.c -o build/next-episode-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/next-episode-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/series_settings_test.c -o build/series-settings-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/series-settings-test

.PHONY: test-series

# Native display scaling and bounded music metadata/worker tests.
test-music-layout: all
	$(CC) $(CFLAGS) tests/hardware_layout_test.c -ldl -o build/hardware-layout-test
	./build/hardware-layout-test
	$(CC) $(CFLAGS) tests/viewport_startup_test.c -ldl -o build/viewport-startup-test
	./build/viewport-startup-test
	python3 tests/music_test.py

.PHONY: test-music-layout

# Portable platform foundation and native Windows application.
test-platform:
	bash scripts/test-platform.sh

windows-platform:
	bash scripts/build-windows.sh

test-platform-shell: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/platform_shell_test.c -o build/platform-shell-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/platform-shell-test

.PHONY: test-platform windows-platform test-platform-shell

test-desktop-ipc:
	python3 tests/desktop_player_ipc_test.py

.PHONY: test-desktop-ipc

windows:
	python3 scripts/build-windows-app.py

.PHONY: windows

test-desktop-cores: all
	python3 tests/core_manager_test.py
	$(CC) $(CFLAGS) -DFLXTR_DESKTOP $(SDL_CFLAGS) src/catalog.c -o build/desktop-core-catalog $(SDL_LIBS) -lSDL2_image -lcurl -ljson-c -lcrypto
	$(CC) $(CFLAGS) tests/retroarch_fixture.c -o build/retroarch-fixture
	python3 tests/desktop_games_test.py --catalog build/desktop-core-catalog --retroarch build/retroarch-fixture --tools tools

.PHONY: test-desktop-cores

test-rom-menu: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/rom_menu_test.c -o build/rom-menu-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/rom-menu-test
	python3 tests/games_test.py

.PHONY: test-rom-menu

test-settings: all
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/settings_tabs_test.c -o build/settings-tabs-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/settings-tabs-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/samba_service_test.c -o build/samba-service-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/samba-service-test
	python3 tests/samba_install_test.py

.PHONY: test-settings

test-mtv:
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc tests/mtv_weights_test.c -o build/mtv-weights-test
	./build/mtv-weights-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/mtv_query_test.c -o build/mtv-query-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/mtv-query-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/mtv_weights_ui_test.c -o build/mtv-weights-ui-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/mtv-weights-ui-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/mtv_preload_test.c -o build/mtv-preload-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/mtv-preload-test
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/youtube_recovery_test.c -o build/youtube-recovery-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/youtube-recovery-test

.PHONY: test-mtv

test-power:
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/power_activity_test.c -o build/power-activity-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/power-activity-test
	python3 tests/power_activity_test.py

.PHONY: test-power

test-usb:
	mkdir -p build
	python3 tests/usb_setup_test.py
	$(CC) $(CFLAGS) $(SDL_CFLAGS) tests/usb_setup_ui_test.c -o build/usb-setup-ui-test $(SDL_LIBS) -lSDL2_image -lcurl
	./build/usb-setup-ui-test

.PHONY: test-usb
