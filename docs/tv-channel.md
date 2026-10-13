# TV channel mode

TV joins the scheduled episode at its current wall-clock position in Oslo time. A 44-minute episode beginning at 15:00 starts around 30 minutes in when TV is opened at 15:30. Shorter episodes advance the lineup; runtime metadata is used, with marked estimates when unavailable. The saved channel position is shared across restarts rather than being a personal pause/resume bookmark.

Episodes already playing finish before genres change. Marathon blocks continue the same series in episode order, including season rollover. An exhausted/unavailable series falls back to another show. Settings > TV Schedule lets A cycle between start hour, genre and marathon; Left/Right edits the selected field.

Default daily lineup:

| Oslo time | Programming | Format |
|---|---|---|
| 00–06 | Mystery | Marathon |
| 06–09 | Kids | Variety |
| 09–11 | Comedy | Variety |
| 11–13 | Family | Variety |
| 13–15 | Documentary | Variety |
| 15–17 | Crime / drama | Marathon |
| 17–19 | Action / adventure | Marathon |
| 19–21 | Sci-fi | Marathon |
| 21–23 | Sci-fi / adult animation | Variety |
| 23–00 | Animation | Marathon |

The old untouched default schedule upgrades automatically. Custom hours and genres are preserved.

On Steam Link, the next episode is selected and resolved before the commercial. A separate player process preloads compressed packets into its bounded disk buffer while the ad plays, and opens the hardware decoder only after the ad exits. Cancelling also terminates the waiting process. Server failures still fall back automatically, preferring Flixer server 5. Linux understands the handoff but defers loading until the ad ends.

Clock alignment is based on metadata, not a real broadcaster. Offline interludes are budgeted at two minutes; joining during that gap starts the next episode. Actual ad length and metadata errors can shift the lineup. During uninterrupted viewing, episodes always start from the beginning and finish naturally. Sources must support seeking for join-in-progress playback.

Validation: mocked catalog tests cover 30-minute joins, rejoining the same episode, short episodes, interludes, season rollover and overtime. Shell tests cover preload-before-ad ordering, release after the ad, cancellation cleanup, settings and completion progress. ARM player compilation passes. Full hardware ad-to-episode playback needs a live test when the device is idle.
