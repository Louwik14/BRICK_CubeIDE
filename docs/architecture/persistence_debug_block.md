# Persistence debug block (temporary)

`g_persist_dbg` is a retained, contiguous 120-byte block (30 little-endian
32-bit words) in `.data.persist_debug`. `volatile`, `used`, live instrumentation
references, and the initialized magic keep it visible in Release/LTO builds.

| Offset | Word | Meaning |
|---:|---|---|
| 0x00 | magic | `0x50444247` (`PDBG`) |
| 0x04 | version | layout version, currently 3 |
| 0x08 | sequence | incremented for each top-level operation |
| 0x0c | op | operation enum |
| 0x10 | stage | most recent stage enum |
| 0x14 | status | latest status/result |
| 0x18 | first_error_stage | first failing stage, latched |
| 0x1c | first_error_code | first error, latched |
| 0x20..0x24 | bank, slot | Pattern bank/slot or Project slot |
| 0x28 | workspace_owner | current exclusive workspace owner |
| 0x2c..0x30 | candidate_phase, publish | Pattern candidate phase and publication flag |
| 0x34..0x38 | current_pattern, pending_pattern | packed `bank << 16 | slot` identities |
| 0x3c..0x40 | request_generation, boundary_generation | candidate identities |
| 0x44..0x4c | active_track, selected_track, ui_revision | UI view |
| 0x50 | ui_sync | central global-restore UI-sync counter |
| 0x54 | commit_done | Project Pattern-bank commit boundary crossed |
| 0x58..0x64 | detail0..detail3 | stage-specific details |
| 0x68 | transport_running | transport state read by the consumer |
| 0x6c..0x70 | apply_attempted, apply_result | apply call count and codec result |
| 0x74 | decision_reason | last relevant candidate decision |

Operations: 0 NONE, 1 PATTERN_SAVE, 2 PATTERN_LOAD, 3 PATTERN_APPLY,
4 PROJECT_SAVE, 5 PROJECT_LOAD, 6 PROJECT_BLANK.

Stages: 0 NONE, 1 ENTER, 2 POLICY, 3 WORKSPACE, 4 PATH, 5 MOUNT,
6 OPEN, 7 SIZE, 8 READ, 9 WRITE, 10 ENCODE, 11 DECODE, 12 VALIDATE,
13 CANDIDATE, 14 ASYNC, 15 APPLY, 16 BANK_STAGE, 17 BANK_COMMIT,
18 PUBLISH, 19 SEQ_SYNC, 20 UI_SYNC, 21 CLOSE, 22 SUCCESS, 23 FAIL.

Errors: 0 NONE, 1 POLICY, 2 WORKSPACE, 3 PATH, 4 MOUNT, 5 FILESYSTEM,
6 CODEC, 7 VALIDATE, 8 BANK, 9 APPLY, 10 MEDIA, 11 INTERNAL.
Codec and product-specific results can also appear directly as a
signed error code.

Candidate decisions: 0 NONE, 1 PREFLIGHT_BLOCKED,
2 TRANSPORT_STOPPED_APPLY, 3 APPLY_FAILED, 4 TRANSPORT_RUNNING_PENDING,
5 APPLY_SUCCEEDED, 6 WAIT_BOUNDARY.

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
x/30wx &g_persist_dbg
```

## Pattern candidate audit

Le recall possede un seul candidat avec quatre phases exclusives: `EMPTY`,
`REQUESTED`, `LOADING` et `PENDING`. La completion Storage ne publie plus un
etat READY intermediaire: elle verifie la generation de requete, puis applique
immediatement si le transport est arrete ou arme le meme candidat pour la
boundary si le transport tourne.

Un refus de `audio_state_snapshot_control_preflight()` conserve le candidat
`PENDING` pour un poll ulterieur. Un nouveau recall incremente la generation et
remplace logiquement le precedent; une ancienne lecture physique peut terminer
son cleanup mais sa generation ne peut plus publier. STOP vide le candidat en
une operation. `PREFLIGHT_BLOCKED`, `WAIT_BOUNDARY`, `APPLY_FAILED` et
`APPLY_SUCCEEDED` restent les signatures de decision utiles.
