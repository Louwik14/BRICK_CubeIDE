# Sample RAM playback modes

`brick6_sampler_ram_mode_t` is the single authority for Sample RAM playback mode and HOLD policy. The persisted values `ONE=0`, `REVERSE_ONE=1`, `LOOP=2`, and `PINGPONG=3` remain unchanged; `HOLD_ONE=4`, `HOLD_LOOP=5`, and `HOLD_PINGPONG=6` are appended, so the current persistence format needs no migration.

| UI label | Playback | Note off |
| --- | --- | --- |
| `ONE` | Forward once | Releases the track VCA; the voice also ends at the sample end. |
| `REV.ONE` | Reverse once | Releases the track VCA; the voice also ends at the sample start. |
| `LOOP` | Forward loop, including the live loop marker | Releases the track VCA. |
| `PINGPONG` | Alternating loop | Releases the track VCA. |
| `H.ONE` | Forward once | Ignored; the voice ends naturally at the sample end. |
| `H.LOOP` | Forward loop, including the live loop marker | Ignored. |
| `H.PINGPONG` | Alternating loop | Ignored. |

HOLD adds no separate latch or gate state. A new trigger replaces the monophonic Sample RAM voice through the existing declick path. Stop/panic still hard-stops it, and sample replacement keeps the existing safe replacement/declick behavior. Switching live from HOLD to a normal mode releases the VCA immediately when no physical gate remains held; live START, END, LOOP, and MODE reconciliation remains active.
