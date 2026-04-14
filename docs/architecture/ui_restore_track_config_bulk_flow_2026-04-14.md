# ui_restore_track_config_bulk flow (targeted pass)

Date: 2026-04-14
Scope: `ui_restore_track_config_bulk` and helpers called directly from this flow in `Src/UI/ui_core.c`.

## Short flow note
Entry point: `ui_restore_track_config_bulk(...)` (`Src/UI/ui_core.c:1074`).

Observed behavior is split into 4 phases in one function:
1. validate snapshot input and constraints
2. apply UI state arrays (`g_ui_track_state.*`)
3. runtime/system sync (`track_runtime_invalidate_all`, `ui_core_sync_audio_runtime_enables`, `ui_core_sync_active_track_cfg_params`)
4. notification callback (`keyboard_runtime_on_active_track_changed`)

Strict current order after UI writes is:
`track_runtime_invalidate_all` -> `ui_core_sync_audio_runtime_enables` -> `keyboard_runtime_on_active_track_changed` -> `ui_core_sync_active_track_cfg_params`.

## Table: step -> effect type -> zones touched -> critical order
| Step | Effect type | Zones touched | Critical order |
|---|---|---|---|
| Null pointer guard (`family/type/midi_channel/midi_source`) | Validate snapshot | Z5 API boundary only | Must be first (fast fail, no partial state write) |
| Per-track validation loop | Validate snapshot | Z5 rules + helper checks: `ui_track_type_is_valid_for_family`, `ui_track_family_is_input` | Must complete before any state mutation |
| Constraints enforced | Validate snapshot | Input family uniqueness, single master family, enum bounds for family/source | All-or-nothing; any fail returns `false` |
| DX7 compat fold (`dx7_kept`) | Validate+normalize snapshot | Z5 compat policy (historical DX7 multi-instance -> MONOB after first) | Must run before state write for each track |
| Write `g_ui_track_state.track_configs[]` | Apply UI state | Z5 authoritative UI track family/type | Pivot UI mutation block |
| Write `g_ui_track_state.track_midi_channel[]` with clamp 1..16 | Apply UI state | Z5 authoritative UI MIDI channel state | Must stay in same pass as configs for coherent snapshot apply |
| Write `g_ui_track_state.track_midi_source[]` | Apply UI state | Z5 authoritative UI MIDI source state | Must stay in same pass as configs |
| `track_runtime_invalidate_all()` | Runtime/system sync (invalidate) | Z2 runtime binding/cache invalidation | Must happen before sync that assumes refreshed runtime mapping |
| `ui_core_sync_audio_runtime_enables()` | Runtime/system sync (mutation) | Z2 runtime track enables (`track_enable` lanes from current UI families) | Depends on updated UI state; currently before keyboard callback |
| `keyboard_runtime_on_active_track_changed()` | Notification callback | Z2 keyboard runtime local state reset/arp enter rules | Kept before active-track cfg sync in current behavior |
| `ui_core_sync_active_track_cfg_params()` | Runtime/system sync (UI-active publish) | Z3 (`param_store_set_active`, `param_registry_sync_ui_for_active_track`) + Z4 reads (`seq_runtime_get_*`) | Last stage currently; publishes final active-track-visible state |
| Return `true` | Observable API result | Caller | Indicates full apply + sync done |

## Explicit checks requested
- Validation constraints restore: present (null guards, enum bounds, type-family validity, input uniqueness, single master).
- DX7 -> MONOB compat: present and localized (`dx7_kept`, first DX7 kept, next folded).
- Writes into `g_ui_track_state`: full arrays for family/type/midi channel/midi source are rewritten.
- `track_runtime_invalidate_all`: present, post-write sync phase.
- `ui_core_sync_audio_runtime_enables`: present, post-invalidate.
- `keyboard_runtime_on_active_track_changed`: present, post-runtime sync, pre-active-param sync.
- `ui_core_sync_active_track_cfg_params`: present, final publish stage.

## Smallest local extraction point recommended
Keep behavior unchanged, extract only a private local post-apply pipeline helper in `ui_core.c`:
- `ui_core_restore_post_apply_sync_and_notify()` containing exactly:
  1. `track_runtime_invalidate_all();`
  2. `ui_core_sync_audio_runtime_enables();`
  3. `keyboard_runtime_on_active_track_changed();`
  4. `ui_core_sync_active_track_cfg_params();`

Why this is the smallest safe cut:
- no handler/business logic change
- no cross-module change
- preserves strict observable order
- isolates the mixed "runtime/system sync + notification" nucleus from snapshot validation/apply logic.
