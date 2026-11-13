# Secret of Mana: first learn-by-doing combat loop

This checkpoint introduces **persistent online attack-range learning**, not
neural-network weight training or a complete reinforcement-learning agent.
It supplements, rather than replaces, the verified-tile deterministic planner.

## How it works

1. The SkyLoft bridge reads live WRAM. Only an isolated encounter (exactly
   one active enemy, no recruited companion, valid weapon/HP and verified
   field context) is eligible.
2. When the bridge *actually sends* a normal `attack`, it records enemy type,
   equipped weapon, range bucket (16-pixel bands), health and timestamp.
3. It observes later frames. When enemy HP falls or a stable observation
   window elapses, it records a hit/miss, damage and HP lost in SQLite table
   `learned_attack_ranges`.
4. After at least four samples in each of two comparable ranges and at least
   two successful closer attacks, the planner can prefer approaching from a
   demonstrably worse range rather than swinging.
5. Occasional exploration attempts remain possible. If evidence is absent
   or ambiguous, behavior reverts to the original attack policy.

This is a conservative heuristic/bandit-like online learner. Damage attribution
can still be uncertain (other damage sources, delayed damage, enemy regeneration
or rapidly changing states). The learner discards ambiguous cases and does not
overwrite Qwen weights or modify the ROM, emulator, or memory.

Logging: `[learning] enemy=0x00 weapon=sword distance=... damage=... hp_lost=...`

Data lives in the service's `SOM_AI_HISTORY_DB` SQLite database (default
`~/.local/state/flxtr/som-ai.sqlite`), independent of the savestate. Per-enemy
and per-weapon records survive service restarts.

## Update and test on SkyLoft

```sh
cd ~/builds/FlXtR-SOM-AI-Test
git pull --ff-only
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_learning.py' -v
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_personality.py' -v
python3 -m py_compile tools/som_learning.py tools/som_planner.py tools/som_qwen_bridge.py
```

Restart the **SkyLoft** user service to load the updated Python bridge:

```sh
systemctl --user restart flxtr-som-ai.service
journalctl --user -u flxtr-som-ai.service -f
```

No new ARM core code is required for this learning increment. To rebuild the
existing Steam Link proxy binary anyway:

```sh
bash scripts/build-som-ai-proxy.sh
```

That stages `build/som-ai/som_ai_proxy_libretro.so`; it does not deploy it.
The proxy from the earlier personality-bubble change must still be deployed
separately to see bubbles on the Steam Link.

## Next steps

Instrument attacks with native hit events to reduce uncertainty about credit,
learn temporal spacing/charge strategy, then add goal-dependent rewards for
navigation, recovery and exploration. Qwen can read a compact account of the
agent's learned tactics, but **the fast combat loop stays deterministic**.
