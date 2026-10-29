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
    if [ -r ./power-activity.sh ]; then sh ./power-activity.sh --idle >power-activity.log 2>&1 || true; fi
    if [ "$code" -eq 44 ]; then
        # The UI passes a numeric catalog selection, never a shell command.
        game_id=$(cat gamez-request 2>/dev/null) || game_id=
        rm -f gamez-request
        case "$game_id" in
            1) game='Final Fantasy VII Remake' ;;
            2) game='Ghost of Tsushima' ;;
            3) game='Nioh 2 Complete Edition' ;;
            4) game='Minecraft Dungeons' ;;
            5) game='NINJA GAIDEN - Master Collection' ;;
            6) game='Yakuza 3 Remastered' ;;
            7) game='Yakuza 4 Remastered' ;;
            8) game='Yakuza 5 Remastered' ;;
            9) game='Yakuza 6 The Song of Life' ;;
            10) game='Persona 4 Golden' ;;
            11) game='Chrono Trigger' ;;
            12) game='TEKKEN 8' ;;
            13) game='Street Fighter V - Champion Edition' ;;
            14) game='Kingdom Hearts III and Re-Mind' ;;
            15) game='The Witcher 3 - Wild Hunt' ;;
            16) game='Starfield' ;;
            17) game='Black Myth: Wukong' ;;
            18) game='Baldur'\''s Gate 3' ;;
            19) game='Actraiser Renaissance' ;;
            20) game='Castlevania Advance Collection' ;;
            21) game='Middle-Earth - Shadow of Mordor' ;;
            22) game='My Friend Peppa Pig' ;;
            23) game='Ruined King' ;;
            24) game='Sword and Fairy 7' ;;
            25) game='VtM - Reckoning of New York' ;;
            26) game='Dragon Ball Z Kakarot' ;;
            27) game='JUMP FORCE' ;;
            28) game='Killer Instinct' ;;
            29) game='MARVEL vs CAPCOM FC - AC' ;;
            30) game='ONE PUNCH MAN A HERO NOBODY KNOWS' ;;
            31) game='SOULCALIBUR VI Deluxe Edition' ;;
            32) game='The King of Fighters XIV' ;;
            33) game='FFXV' ;;
            34) game='Final Fantasy VII Remake - Crisis Core Reunion' ;;
            35) game='LIVE A LIVE' ;;
            36) game='Light Fairytale Episode 2' ;;
            37) game='SMT III Nocturne HD Remaster' ;;
            38) game='Secret of Mana' ;;
            39) game='Stranger of Paradise - Final Fantasy Origin' ;;
            40) game='The Last Oricru' ;;
            41) game='ToA' ;;
            42) game='Trials of Mana' ;;
            43) game='Valkyrie Elysium' ;;
            44) game='Vampire The Masquerade - Shadows of New York' ;;
            45) game='Vampire The Masquerade Bloodlines - Ultimate' ;;
            46) game='Vampire The Masquerade Coteries of New York Deluxe Edition' ;;
            47) game='Yakuza Kiwami 3 & Dark Ties' ;;
            48) game='Ys IX Monstrum Nox' ;;
            49) game='Beyond a Steel Sky' ;;
            50) game='City of Broken Dreamers Book One' ;;
            51) game='Ghosts n Goblins Resurrection' ;;
            52) game='KAGE Shadow of the Ninja' ;;
            53) game='KATANA KAMI A Way of the Samurai Story' ;;
            54) game='Katamar iDamacy REROLL' ;;
            55) game='Mega Man 11' ;;
            56) game='No.More.Heroes.2.Desperate.Struggle.Build.7018796' ;;
            57) game='No.More.Heroes.Build.7018523' ;;
            58) game='P5S' ;;
            59) game='Pathfinder Wrath of the Righteous' ;;
            60) game='Sakuna - Of Rice and Ruin' ;;
            61) game='Star Wars Jedi - Fallen Order' ;;
            62) game='Steins;Gate' ;;
            63) game='Stranger Things 3 The Game' ;;
            64) game='SuperMash' ;;
            65) game='The Fermi Paradox' ;;
            66) game='The Walking Dead The Telltale Definitive Series' ;;
            67) game='Until We Die' ;;
            68) game='Way of the Samurai 4' ;;
            69) game='Wolcen Lords of Mayhem Bloodtrail' ;;
            *) printf '%s\n' 'Invalid Gamez selection' >moonlight-gamez.log
               set -- --library games
               continue ;;
        esac
        # Sunshine must have an application registered with this exact title.
        (
            cd /home/apps/moonlight || exit 127
            export HOME=/usr/local/moonlight
            export SDL_ENABLE_STEAM_CONTROLLERS=1
            export SENSOR_REPORT_RATE_LIMIT_HZ=0
            exec ./bin/moonlight stream 192.168.10.187 "$game" --1080 --fps 60
        ) >moonlight-gamez.log 2>&1
        printf '%d\n' "$?" >game-exit-status
        set -- --library games
        continue
    fi
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
        power_guard_pid=
        if [ -r ./power-activity.sh ]; then
            sh ./power-activity.sh "$game_pid" >power-activity.log 2>&1 &
            power_guard_pid=$!
        fi
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
        if [ -n "$power_guard_pid" ]; then
            kill -TERM "$power_guard_pid" 2>/dev/null || true
            wait "$power_guard_pid" 2>/dev/null || true
        fi
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
