# Persistence debug block (temporary)

`g_persist_dbg` is a retained, contiguous 144-byte block (36 little-endian
32-bit words) in `.data.persist_debug`. `volatile`, `used`, live instrumentation
references, and the initialized magic keep it visible in Release/LTO builds.

| Offset | Word | Meaning |
|---:|---|---|
| 0x00 | magic | `0x50444247` (`PDBG`) |
| 0x04 | version | layout version, currently 2 |
| 0x08 | sequence | incremented for each top-level operation |
| 0x0c | op | operation enum |
| 0x10 | stage | most recent stage enum |
| 0x14 | status | latest status/result |
| 0x18 | first_error_stage | first failing stage, latched |
| 0x1c | first_error_code | first error, latched |
| 0x20..0x24 | bank, slot | Pattern bank/slot or Project slot |
| 0x28..0x2c | lease_owner, workspace_owner | current owners |
| 0x30..0x38 | ready, queue, publish | Boolean publication pipeline state |
| 0x3c | current_pattern | packed `bank << 16 | slot` |
| 0x40 | prepared_pattern | packed `bank << 16 | slot` |
| 0x44 | pattern_revision | boundary/generation identity |
| 0x48..0x50 | active_track, selected_track, ui_revision | UI view |
| 0x54 | ui_sync | central global-restore UI-sync counter |
| 0x58 | commit_done | Project Pattern-bank commit boundary crossed |
| 0x5c..0x68 | detail0..detail3 | stage-specific details |
| 0x6c | ready_consumer_calls | READY-consumer poll count for this operation |
| 0x70..0x74 | take_ready_called, take_ready_result | take call count and last Boolean result |
| 0x78 | transport_running | transport state read by the consumer |
| 0x7c..0x80 | apply_attempted, apply_result | apply call count and codec result |
| 0x84..0x88 | queue_attempted, queue_result | queue-arm call count and last Boolean result |
| 0x8c | decision_reason | last relevant READY-consumer decision |

Operations: 0 NONE, 1 PATTERN_SAVE, 2 PATTERN_LOAD, 3 PATTERN_APPLY,
4 PATTERN_QUEUE, 5 PROJECT_SAVE, 6 PROJECT_LOAD, 7 PROJECT_BLANK.

Stages: 0 NONE, 1 ENTER, 2 POLICY, 3 LEASE, 4 WORKSPACE, 5 PATH,
6 MOUNT, 7 OPEN, 8 SIZE, 9 READ, 10 WRITE, 11 ENCODE, 12 DECODE,
13 VALIDATE, 14 READY, 15 QUEUE, 16 APPLY, 17 BANK_STAGE,
18 BANK_COMMIT, 19 PUBLISH, 20 SEQ_SYNC, 21 UI_SYNC, 22 CLOSE,
23 SUCCESS, 24 FAIL.

Errors: 0 NONE, 1 POLICY, 2 LEASE, 3 WORKSPACE, 4 PATH, 5 MOUNT,
6 FILESYSTEM, 7 CODEC, 8 VALIDATE, 9 BANK, 10 APPLY, 11 MEDIA,
12 INTERNAL. Codec and product-specific results can also appear directly as a
signed error code.

READY decisions: 0 NONE, 1 NO_PENDING, 2 LOAD_REQUEST_REFUSED,
3 NO_READY, 4 STALE_READY, 5 PREFLIGHT_BLOCKED, 6 TAKE_READY_REFUSED,
7 TRANSPORT_STOPPED_APPLY, 8 APPLY_FAILED, 9 TRANSPORT_RUNNING_QUEUE,
10 QUEUE_FAILED, 11 QUEUE_ARMED, 12 APPLY_SUCCEEDED, 13 WAIT_BOUNDARY.

For Pattern I/O, details are normally `{FatFs result, requested bytes,
transferred bytes, buffer capacity}`. For Project decode they are `{codec
result, file size, asset count, pattern count}`. During Project asset rebuild
they are `{asset index, asset count, warning count, commit_done}`. During
Project Save failure they are `{save error, low-level detail, file offset,
encoded size}`.

The single raw read is:

```gdb
shell cls
info address g_persist_dbg
x/36wx &g_persist_dbg
```

## Pattern READY consumer audit

`pattern_load_service()` changes the load state to READY. The only consumers of
`pattern_load_take_ready()` are `pattern_live_queue_slot()` (immediate recall)
and `pattern_live_try_take_pending_ready()`, called by `pattern_live_service()`
once per normal `brick6_app_process()` superloop pass, after the storage
service. Storage loading itself is skipped while a Multi load is pending, but
the READY consumer is not.

For a stopped transport, the consumer requires
`audio_state_snapshot_control_preflight()` before taking READY and applying the
snapshot immediately. A failed preflight intentionally leaves the load READY
and the pending request alive for a later poll. For a running transport it
takes READY, transfers the lease to `PATTERN_QUEUE_READY`, and waits for the
selected track loop boundary before applying. Coordinate mismatch cancels a
stale READY. A STOP transition cancels both in-flight READY and armed queue.

Consequently, saved and empty slots converge on the same consumer after the
backend reaches READY. The leading hypothesis for both observed stopped-
transport cases is repeated `PREFLIGHT_BLOCKED` (snapshot already active or no
free CONTROL-to-AUDIO publication slot). The alternative signatures are now
explicit: `NO_PENDING`, `STALE_READY`, `TAKE_READY_REFUSED`, `APPLY_FAILED`, or
`QUEUE_FAILED`. No APPLY/QUEUE behavior is changed by this instrumentation.
