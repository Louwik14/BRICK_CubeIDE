# Z3 - Param / Modulation / Control

## 1. Perimetre

Zone Z3 (coeur):
- `Src/Param/param_registry.c`
- `Inc/Param/param_registry.h`
- `Src/Param/param_store.c`
- `Inc/Param/param_store.h`
- `Src/Mod/mod_lfo_v1.c`
- `Inc/Mod/mod_lfo_v1.h`

Dependances de preuve strictes:
- Z2: `track_runtime_get_param_rule`, `track_runtime_get_effective_param_status`, bind/runtime ctx.
- Z1 (point d'insertion uniquement): `brick6_audio_runtime_dsp` appelle `mod_lfo_v1_process_block`.
- Z4/Z6 (usage ecriture): `seq_param_iface` (plocks), `pattern_live_ram` (restore snapshot).
- Z5 (source edits): `ui_param`, `ui_core`.

## 2. Autorite d'ecriture (etat stabilise)

Familles d'autorite:
- `global-only`:
  - API cible: `param_set`.
  - `param_store.active[]`: verite runtime globale.
  - Pas de dependance Z2 directe.

- `track-aware`:
  - API cible: `param_registry_apply_track_value`.
  - Variante RT autorisee: `param_registry_apply_track_value_rt_fast` (modulation uniquement).
  - `param_store.active[]`: miroir UI du contexte d'edition (pas verite runtime track).
  - Dependance Z2 requise (rules/status/bind).

- `LFO-owned`:
  - Config LFO: `mod_lfo_v1_set_track_param`.
  - Modulation runtime: `param_registry_apply_track_value_rt_fast`.
  - Release: restaure la base, jamais la derniere valeur modulee.

- `legacy-physical` (`PARAM_MIX_TRACK0..3_*`):
  - API: `param_set` -> `param_apply_legacy_mix_track_value`.
  - Statut: legacy assume / compat minimale / ilot borne.

## 3. Statut des chemins sensibles

- `control_router_set_param`:
  - Statut: dormant legacy.
  - Aucun caller actif trouve dans `Src/`.
  - Hors stabilisation immediate Z3.

- Restore LFO (Z6 via `pattern_live_apply_snapshot`):
  - Autorite unique: `mod_lfo_v1_set_track_param`.
  - Double write via `param_registry_apply_track_value(PARAM_LFO*)` supprime.

- Coexistence base write / modulation RT:
  - Contrat: un write track-aware autoritatif resynchronise la base LFO active (`mod_lfo_v1_resync_base_on_authoritative_write`).
  - A la release (dest change / depth=0 / dest non supportee): restauration de cette base.

## 4. Flux runtime (condense)

1. Ecriture base:
- global: `param_set`.
- track-aware: `param_registry_apply_track_value`.

2. Modulation:
- Tick control-rate depuis audio bloc.
- Capture base via `param_registry_get_track_value`.
- Apply module via `_rt_fast`.
- Release -> write base via `_rt_fast`.

3. Restore snapshot:
- globals: `param_set`.
- track-aware: `param_registry_apply_track_value`.
- LFO config: `mod_lfo_v1_set_track_param` uniquement.

## 5. Invariants a ne pas casser

- Clamp min/max avant write effectif.
- `PARAM_LFO*` = params de config modulation, pas params runtime directs.
- `param_registry_apply_track_value_rt_fast` reserve a la modulation RT.
- Release LFO doit restaurer la base (et non la derniere valeur modulee).
- `param_store.active[]`:
  - global-only: verite runtime.
  - track-scoped UI: miroir UI.
- `PARAM_MIX_TRACK0..3_*` reste un ilot legacy physique borne.

## 6. Dette technique restante (bornee)

- Coexistence maintenue `param_set` (global/legacy) vs `param_registry_apply_track_value` (track-aware).
- `param_registry.c` reste monolithique (metadata + dispatch + cache + filtres + bindings).
- Ilot legacy `PARAM_MIX_TRACK0..3_*` toujours vivant (UI mute/restore/boot defaults), mais confine.

## 7. Impact sur cartographie globale

- Aucun changement necessaire dans `ARCHITECTURE_GLOBAL.md`.
- La frontiere Z3/Z2 reste: Z2 autorise/contraint, Z3 applique.
