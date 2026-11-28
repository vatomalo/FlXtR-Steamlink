# Player flight prototype

The original Flammie Drum starts a separate Mode 7 world-map ride. This prototype instead lets the active party fly around the current outdoor field map, using the game's actor height and movement/camera updates.

## Controls

Press **L2 + R2 + Start** together to take off. Press the same chord to land. The game consumes the chord until all three buttons are released, so it cannot also open the pause menu. D-pad movement and character animation remain native.

Flight is available only where the game's Drum-allowed flag is set and the indoor flag is clear. The party's active heroes are lifted together. During flight, the implementation holds their native Z height and temporarily sets the game's no-clip flag; landing restores their previous Z and no-clip bit. Map transitions, save/load, reset, and game unload end flight automatically.

## Implementation and limits

This is an experimental in-process feature in the Turbo core proxy. `src/som_flight.h` owns the flight state. The input hook handles the chord, and a pre/post core-frame hook holds actor height while the SNES game continues to own horizontal movement, animation, and camera tracking. It does not invoke Flammie or change the native Drum path.

The no-clip bit is global to the game, so flight can affect collision for other entities while active. It may allow movement through boundaries or map exits. This needs actual Steam Link gameplay validation before treating it as finished. Flight is not enabled in other cores or games.

## References

- The [Secret of Mana manual](https://www.nintendo.co.jp/clvs/manuals/common/pdf/CLV-P-SABRE.pdf) describes the Drum's world view and its indoor restriction.
- The [Secret of Mana RAM map](https://wiki.superfamicom.org/seiken-densetsu-2) documents the relevant actor and field state locations used by the proxy.
