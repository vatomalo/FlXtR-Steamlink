#!/bin/sh
cd "$(dirname "$0")" || exit 1
# The box has no reliable clock after some cold boots. Keep HTTPS validation on.
if [ "$(date +%Y)" -lt 2024 ] && command -v ntpd >/dev/null 2>&1; then
    ntpd -n -q -p pool.ntp.org >clock.log 2>&1 &
    clock_pid=$!
    tries=0
    while [ "$tries" -lt 10 ] && [ "$(date +%Y)" -lt 2024 ]; do
        sleep 1
        tries=$((tries + 1))
    done
    kill "$clock_pid" 2>/dev/null || true
    wait "$clock_pid" 2>/dev/null || true
fi
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-}"
if [ "$#" -eq 0 ] && [ -f catalog.local.tsv ]; then
    set -- --catalog catalog.local.tsv
fi
# A failed/offline check must never prevent launch. No credentials on the device.
if [ ! -f .no-auto-update ]; then sh ./update.sh >update.log 2>&1 || true; fi
if [ -f samba/.enabled ] && [ -f samba-server.sh ]; then
    sh ./samba-server.sh start >samba-start.log 2>&1 || true
fi
if [ -f tools/state_guard.py ] && [ -f scripts/music-runtime.sh ]; then
    sh scripts/music-runtime.sh tools/state_guard.py seed >state-guard-seed.log 2>&1 &
fi
while :; do
    rm -f game-music-request
    ./greenlink "$@" >>greenlink.log 2>&1
    code=$?
    if [ "$code" -eq 43 ]; then
        # A broken Steam Link audio/video pipeline can trap RetroArch indefinitely,
        # repeatedly printing MV_PE_InputBufWrite failures and ignoring TERM.
        # Recover ONLY when the same error is actively repeating; normal games
        # have no time limit. The watchdog never touches metadata workers.
        # Keep the previous session even when the user immediately launches again.
        [ ! -f game.previous.log ] || mv -f game.previous.log game.older.log
        [ ! -f game.log ] || mv -f game.log game.previous.log
        rm -f state-session.json
        ./greenlink-catalog --run-game >game.log 2>&1 &
        game_pid=$!
        music_guard_pid=
        if [ -f game-music-request ] && [ -f tools/rom_music.py ]; then
            sh scripts/music-runtime.sh tools/rom_music.py "$game_pid" >rom-music.log 2>&1 &
            music_guard_pid=$!
        fi
        state_guard_pid=
        if [ -f tools/state_guard.py ] && [ -f scripts/music-runtime.sh ]; then
            sh scripts/music-runtime.sh tools/state_guard.py watch "$game_pid" >state-guard.log 2>&1 &
            state_guard_pid=$!
        fi
        (
            previous_size=0
            consecutive=0
            while kill -0 "$game_pid" 2>/dev/null; do
                sleep 4
                current_size=$(wc -c <game.log 2>/dev/null) || current_size=0
                # Count only new output: an old error burst followed by healthy
                # logging must not look like a continuing media failure.
                errors=0
                if [ "$current_size" -gt "$previous_size" ]; then
                    errors=$(tail -c "+$((previous_size + 1))" game.log 2>/dev/null | tail -n 64 | grep -c 'MV_PE_InputBufWrite() failed')
                fi
                if [ "$errors" -ge 8 ]; then
                    consecutive=$((consecutive + 1))
                else
                    consecutive=0
                fi
                previous_size=$current_size
                if [ "$consecutive" -ge 3 ]; then
                    printf '%s\n' 'FlXtR watchdog: repeating Steam Link media input failure; recovering RetroArch' >>watchdog.log
                    cp game.log game-failure.log
                    kill -TERM "$game_pid" 2>/dev/null
                    sleep 2
                    kill -KILL "$game_pid" 2>/dev/null
                    break
                fi
            done
        ) &
        watchdog_pid=$!
        wait "$game_pid"
        game_code=$?
        if [ -n "$music_guard_pid" ]; then
            kill -TERM "$music_guard_pid" 2>/dev/null
            wait "$music_guard_pid" 2>/dev/null
        fi
        # If the game ended before Python accepted the handoff, release its player.
        if [ -f game-music-request ]; then
            read music_initial music_auto <game-music-request
            case "$music_initial" in
                ''|*[!0-9]*) ;;
                *) if [ "$(cat "/proc/$music_initial/comm" 2>/dev/null)" = greenlink-audio ]; then
                       kill -TERM "$music_initial" 2>/dev/null
                   fi ;;
            esac
            rm -f game-music-request
        fi
        if [ -n "$state_guard_pid" ]; then wait "$state_guard_pid" || true; fi
        kill "$watchdog_pid" 2>/dev/null
        wait "$watchdog_pid" 2>/dev/null
        [ "$game_code" -eq 0 ] || cp game.log game-failure.log
        printf '%d\n' "$game_code" >game-exit-status
        set -- --library games
        continue
    fi
    [ "$code" -eq 42 ] || exit "$code"
done
