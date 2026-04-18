# z5_ui_system_sync_boundary
Statut documentaire: annexe de transition, alignee sur l'etat code au 2026-04-18.

## Contrat final de frontiere

- `ui_system_sync_internal` est **runtime-only**.
- `ui_system_sync_internal` ne fait pas de sync UI (`param_store`, `param_registry_sync_ui_for_active_track`, page/bank/param).
- La sync UI post-reconfig est declenchee explicitement par `ui_active_track_sync_after_track_structure_change(...)`.

## Ce que couvre `ui_system_sync_internal`

Pipeline runtime applique par requete (`ui_system_sync_request_t`):
- invalidation runtime globale (`track_runtime_invalidate_all`),
- sync enables runtime audio (`track_enable(...)` via callback adapteur UI),
- notification clavier post-runtime (`keyboard_runtime_sync_track_focus_context`),
- ordre runtime explicite (`invalidate->enables` ou `enables->invalidate`).

API:
- `ui_system_sync_make_request_restore_bulk()`
- `ui_system_sync_make_request_track_family_change(active_track_touched)`
- `ui_system_sync_make_request_track_type_change(active_track_touched)`
- `ui_system_sync_apply_track_context_change(...)`

## Ce que `ui_system_sync_internal` ne couvre plus

- Selection legere de track active.
- Sync d'edit context UI.
- Projection mirror actif `UI_CFG_*`.
- Full sync post-restore global.

## Call-sites reels dans `ui_core`

- `ui_set_track_family(...)`:
  - requete runtime family-change,
  - puis `ui_active_track_sync_after_track_structure_change(active_track_touched)`.
- `ui_set_track_type(...)`:
  - requete runtime type-change,
  - puis `ui_active_track_sync_after_track_structure_change(active_track_touched)`.
- `ui_restore_track_config_bulk(...)`:
  - `ui_core_post_restore_global_sync()` -> requete restore bulk,
  - puis `ui_active_track_sync_after_track_structure_change(1U)`.

La selection de track (`ui_core_set_active_track`) reste hors `ui_system_sync_internal`:
- focus UI (`active_track`) + sync explicite de contexte UI.

## Contrat de separation (resume)

1. Selection legere de track: `ui_core_set_active_track` (UI focus/context seulement).
2. Sync edit context: `ui_edit_context_sync_active_track(...)`.
3. Mirror actif: `ui_active_track_sync_mirror*`.
4. Reconfig runtime lourde: `ui_system_sync_internal`.
5. Full sync post-reconfig: `ui_active_track_sync_full_after_reconfigure()`.
6. Full sync post-restore global: `ui_active_track_sync_full_after_global_restore()`.
