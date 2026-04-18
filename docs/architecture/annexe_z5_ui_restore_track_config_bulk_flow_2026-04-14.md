# ui_restore_track_config_bulk flow (aligned)
Statut documentaire: annexe utile (non canonique), alignee sur le code au 2026-04-18.

## Entree

- `ui_restore_track_config_bulk(...)` dans `Src/UI/ui_core.c`.

## Flux effectif

1. Validation snapshot (all-or-nothing):
- pointeurs non nuls,
- bornes family/type/source,
- validite type/family,
- unicite familles input,
- unicite famille master,
- normalisation compat DX7 legacy.

2. Apply snapshot UI:
- ecrit `g_ui_track_state.track_configs[]`,
- ecrit `g_ui_track_state.track_midi_channel[]` (clamp 1..16),
- ecrit `g_ui_track_state.track_midi_source[]`.

3. Post-restore global explicite:
- `ui_core_post_restore_global_sync()`
  - construit `ui_system_sync_make_request_restore_bulk()`,
  - appelle `ui_system_sync_apply_track_context_change(...)` (runtime-only),
  - puis `ui_active_track_sync_after_track_structure_change(1U)`.

## Ordre runtime/UI apres apply (contrat reel)

1. runtime reconfigure (`ui_system_sync_internal`):
- `track_runtime_invalidate_all()`
- `ui_core_sync_audio_runtime_enables()` (via adapteur)
- `keyboard_runtime_sync_track_focus_context()` (via adapteur)

2. UI post-reconfig:
- `ui_active_track_sync_full_after_reconfigure()`:
  - `ui_active_track_sync_mirror()`
  - `param_registry_sync_ui_for_active_track()`
- `ui_edit_context_sync_active_track(0U)`

## Clarification de frontiere

- `ui_system_sync_internal`: runtime-only.
- full sync UI post-restore: `ui_active_track_sync_*`.
- pas de resync implicite via `active_page->tick()`.
