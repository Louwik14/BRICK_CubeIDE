# Persistence debug block (temporary)

`g_persist_dbg` is a retained, contiguous 108-byte block (27 little-endian
32-bit words) in `.data.persist_debug`. `volatile`, `used`, live instrumentation
references, and the initialized magic keep it visible in Release/LTO builds.

| Offset | Word | Meaning |
|---:|---|---|
| 0x00 | magic | `0x50444247` (`PDBG`) |
| 0x04 | version | layout version, currently 1 |
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
x/27wx &g_persist_dbg
```
