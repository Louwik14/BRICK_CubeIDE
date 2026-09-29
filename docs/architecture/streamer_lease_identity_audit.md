# Streamer lease identity audit

The physical Multi voice is allocated before its reader is bound. Its array index
(`0..7`) is the Multi reader ID and is stable until that voice is released. A
reader may publish a lease only after this index has passed the domain bound
check. The reader bind rejects an invalid ID before it makes the lease valid.

The lease domains are disjoint: Classic uses slots `0..17` (two reserved IDs
and one per entity), Multi uses `18..25` (one per physical voice), and REC
uses `26..44` (the same entity reader IDs plus one overdub reader). A domain
mapper rejects an out of range reader ID before adding its base. The total
slot count remains below `UINT8_MAX`, which is reserved as an invalid result.
This makes another reader's overwrite impossible for the current owners, so a
reader's local deduplication cannot hide a foreign publication.

Before this change, `BRICK6_SAMPLER_CACHE_VOICE_NONE` was `255` and could reach
`sample_voice_reader_bind_play_plan` on the Multi path. The old Multi base was
`16`; `(uint8_t)(16 + 255)` was `15`, a valid Classic lease slot. The old
Classic count also covered only reader IDs `0..15`, although track IDs `14`
and `15` produce reader IDs `16` and `17`. Those mapped into the Multi domain.
The old REC overdub ID `16` overlapped the REC reader ID for track `14`; REC
track `15` had reader ID `17`, outside the REC count. These three collisions
are fixed by the domain sizes and the dedicated overdub ID.

## Related sentinel audit

Search scope: Sampler, Streamer, page cache, Multi, Classic, REC_SOURCE,
Sampler RAM, Wavetable, AUDIO physical voices, Storage/SD, IPC and track
topology. A targeted `rg` search for `UINT8_MAX`, `UINT16_MAX`, `UINT32_MAX`
and `uint8_t` casts returned 400 candidate lines in the core source/header
directories. The relevant index and identity paths were traced from producer
to consumer; numeric saturation and data conversion matches were excluded.

| Classification | Occurrence | Finding |
| --- | --- | --- |
| BUG REAL, fixed | Multi lease ID `255` | Offset plus narrowing produced Classic slot `15`. |
| BUG REAL, fixed | Classic track IDs `14..15` | Reader IDs `16..17` crossed into Multi slots. |
| BUG REAL, fixed | REC track IDs `14..15` and overdub | Track `14` collided with overdub reader `16`; track `15` was outside the REC domain. |
| FRAGILE MAIS ACTUELLEMENT PROTEGE | `brick6_sampler_runtime_cache_voice_id` | It narrows `2 + track_id`, but the clip entry checks `track_id < SEQ_TRACK_COUNT`; a compile-time assertion now bounds the full domain. |
| SAFE | Multi physical voice index | The allocator returns an element of the eight-voice array; the resulting index is checked before reader bind. |
| SAFE | Sampler RAM and Wavetable async job slot narrowing | Slots are checked against their pool capacity before the asynchronous job stores or narrows them. |
| SAFE | Page-cache deleted-entry and empty-page sentinels | `UINT32_MAX`/`UINT16_MAX` are checked or retained as invalid values before array indexing. |
| SAFE | SD block-device queue ownership | Queue indices are bounded and accompanied by valid-state checks before ownership access. |
| FAUX POSITIF | Numeric `UINT32_MAX` saturation and audio value casts | These values are limits or arithmetic data, not slot identities. |

No further local, demonstrated sentinel-to-valid-identity bug was found in the
examined paths. This is a static audit; hardware playback still needs a new
run to confirm that the original `AUDIO_MISS` has disappeared.
