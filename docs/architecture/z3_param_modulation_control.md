# Z3 - Param / Modulation / Control

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z3):
- `Src/Param/param_registry.c`
- `Inc/Param/param_registry.h`
- `Src/Mod/mod_lfo_v1.c`
- `Inc/Mod/mod_lfo_v1.h`
- `Src/Param/param_store.c`
- `Inc/Param/param_store.h`

Elargissements necessaires (preuves de frontieres et contrats):
- `Src/Core/track_runtime.c` + `Inc/Core/track_runtime.h`: autorite des statuts effectifs (`track_runtime_get_param_rule`, `track_runtime_get_effective_param_status`) consommes par Z3.
- `Src/Core/brick6_audio_runtime.c`: point d'insertion reel de modulation (`mod_lfo_v1_process_block(frames)` par bloc audio).
- `Src/Seq/seq_param_iface.c`: preuve d'appel de `param_registry_apply_track_value` pour apply/restore plocks.
- `Src/Param/control_router.c` + `Inc/Param/control_router.h`: autre chemin d'ecriture param + usage staging/commit.

Dependances de Z3 sans appartenir a Z3:
- Engines/audio/mixer/buffer (`microdexed`, `monob`, `drum`, `audio_float`, `mixer`, `brick6_master_buffer`).
- UI (`ui_core`, `ui_param`) comme source d'ecritures et de contexte track actif.
- Seq runtime/model pour tempo/length/div/quant/swing/rec.
- Storage (`pattern_live_ram`) pour restore snapshot de valeurs track/global/LFO.

Exclusions explicites:
- UI rendering/pages ne portent pas l'autorite de stockage/apply.
- Z1 audio IRQ path n'heberge pas les tables registry et le store.
- Z4 clock/scheduler ne porte pas les metadonnees param.

Sous-roles concentres dans `param_registry.c`:
- Metadonnees statiques (`param_registry[PARAM_COUNT]`: bornes/default/apply).
- Dispatch apply global vs track-aware.
- Cache runtime track-aware (`g_param_runtime_track_values/valid`).
- Etat UI filtre par cible physique (`g_filter_ui_state`).
- Synchronisation UI pour track active.

## 2. Autorite(s) de verite

Autorite d'ecriture globale param:
- `param_set(id, value)` (clamp + `param_store_set_active` + dispatch apply via `desc->apply` + chemin legacy `PARAM_MIX_TRACKx_*`).

Autorite d'ecriture track-aware:
- `param_registry_apply_track_value(id, track, value)` (clamp + dispatch selon domaine/rules/runtime bind).
- Variante RT: `param_registry_apply_track_value_rt_fast(id, track, value)` (sans refresh lourd, utilisee par modulation).

Autorite stockage/shadow:
- `param_store.c`:
  - stockage courant: `g_ps.active[]`
  - staging: `g_ps.staging[]`
  - commit bloque-sur-avance-bloc: `param_store_commit_if_block_advanced()`.

Autorite validation/clamp/dispatch:
- clamp central `clamp_value` + bornes `param_registry[id].min/max`.
- dispatch via pointeur `param_registry[id].apply`.
- routage domaine via `track_runtime_get_param_rule` et `track_runtime_get_effective_param_status`.

Autorite modulation LFO:
- `mod_lfo_v1_set_track_param/get_track_param` (etat config LFO track).
- `mod_lfo_v1_process_block` -> `mod_lfo_process_control_tick` applique modulation via `param_registry_apply_track_value_rt_fast`.

Seconde autorite concurrente:
- Oui, deux chemins d'ecriture coexistent:
  - global `param_set` (non track-aware direct)
  - track-aware `param_registry_apply_track_value`.
- Ils ne sont pas equivalents (domaines/rules/targets differents).

## 3. API entrantes

Entrees Z3:
- UI/control: `param_set`, `param_registry_apply_track_value`, batch begin/end.
- Seq plocks: `seq_param_iface_apply_lock/restore_base` -> `param_registry_apply_track_value`.
- Modulation: `mod_lfo_v1_set_track_param` via params LFO et `mod_lfo_v1_process_block` par bloc audio.
- Storage restore: appels `param_set` + `param_registry_apply_track_value` + `mod_lfo_v1_set_track_param`.
- Control router: `control_router_set_param` -> `param_set` + staging/commit.

Contrats implicites timing:
- `mod_lfo_v1_process_block(frames)` doit etre appele regulierement dans le flux audio (control-rate derive du block-rate).
- `param_store_commit_if_block_advanced` depend de `g_audio_block_counter` (commit differe sur avance bloc).
- `param_registry_apply_track_value` evite certains refresh quand `g_param_registry_batch_depth > 0`.

## 4. API sortantes

Sorties Z3 vers runtime:
- Engines tone/colors: `microdexed_synth_set_param`, `monob_synth_set_*`, `drum_synth_set_param_for_instance`.
- Mix/bus/filter/vca: `mixer_set_*`, `audio_float_set_*`, saturation/decimator par track.
- Buffer master: `brick6_master_buffer_set_*`.
- Seq/clock/transport: `seq_runtime_set_*`, `seq_runtime_on_track_length_changed`, `seq_runtime_set_tempo_bpm_milli`, etc.
- UI sync: `param_store_set_active` pour mirroring des valeurs affichees.

Getters non-mutants vs mutables:
- Non-mutants: `param_get`, `param_store_get_active`, `param_registry_get_track_value` (lit cache/ui-state/store/LFO; pas d'apply runtime).
- Mutables: `param_set`, `param_registry_apply_track_value`, `param_registry_apply_track_value_rt_fast`, `mod_lfo_v1_set_track_param`, `param_store_set_*`, `param_store_commit_if_block_advanced`.

## 5. Etats structurants possedes

Etat registry/store:
- `param_registry[PARAM_COUNT]` (`const param_desc_t`)
  - Porte: definition type/min/max/default/apply de chaque param.
  - Ecriture: compile-time statique.
  - Lecture: `param_set`, UI, render labels, routing Z3.

- `g_ps` (`param_store_t` dans `param_store.c`)
  - Champs: `staging[]`, `active[]`, `last_commit_block`, `commit_count`, `dirty`.
  - Ecriture: `param_store_set_staging/active`, commit.
  - Lecture: `param_get`, UI, commit.

Etat track runtime cache:
- `g_param_runtime_track_values[SEQ_TRACK_COUNT][PARAM_COUNT]` (float)
- `g_param_runtime_track_valid[SEQ_TRACK_COUNT][PARAM_COUNT]` (uint8)
  - Ecriture: `param_runtime_cache_set`, `param_registry_init` reset.
  - Lecture: `param_runtime_cache_get`, `param_registry_get_track_value`.
  - Role: memo de valeurs effectives track-aware appliquees.

Etat filtre UI->runtime:
- `g_filter_ui_state[FILTER_TRACK_TARGET_COUNT]` (`filter_ui_state_t`)
  - Porte: type/cutoff/res/.../decimator per cible physique mixer (4).
  - Ecriture: `filter_ui_state_init_defaults`, `param_registry_apply_track_value`, apply_filter_*.
  - Lecture: `param_registry_get_track_value`, apply runtime helpers.

Etat batch/suivi:
- `g_param_registry_batch_depth` (uint8): controle refresh during batch.
- `g_param_cfg_track_type_apply_stage` (volatile uint32_t): trace stage apply type.

Etat modulation:
- `g_mod_lfo_settings[SEQ_TRACK_COUNT][2]` (`mod_lfo_track_settings_t`: dest/rate/depth/shape).
- `g_mod_lfo_runtime[SEQ_TRACK_COUNT][2]` (`mod_lfo_runtime_state_t`: phase, phase_inc, current, rng/sh, last_dest, base_value, calib...).
- `g_mod_lfo_control_counter` (uint32): accumulateur control-rate.

## 6. Flux runtime

Flux nominal prouve:
1. Source ecriture param
- UI/control/storage/seq appellent `param_set` (global) ou `param_registry_apply_track_value` (track-aware).
- Param LFO (`PARAM_LFOx_*`) sont reroutes vers `mod_lfo_v1_set_track_param`.

2. Validation / clamp
- `param_set` et `param_registry_apply_track_value*` clampent selon `param_registry[id].min/max`.

3. Stockage
- Global: `param_store_set_active` dans `param_set`.
- Track-aware: cache `g_param_runtime_track_values/valid` et/ou `g_filter_ui_state` selon domaine.
- Staging optionnel: `param_store_set_staging` via `control_router`.

4. Commit / snapshot / apply differe
- `param_store_commit_if_block_advanced` applique staging -> `param_set` seulement quand `g_audio_block_counter` a avance.
- `pattern_live_ram` restaure snapshots en appelant `param_set` + `param_registry_apply_track_value` + `mod_lfo_v1_set_track_param`.

5. Application runtime
- `param_set`: legacy mix physique (PARAM_MIX_TRACKx_*) puis callback `desc->apply`.
- `param_registry_apply_track_value`: choisit domaine via `track_runtime_get_param_rule`:
  - `TONE`/`MIX` -> `param_runtime_apply_track`
  - `COLORS` -> `param_runtime_apply_colors_track`
  - `BUFFER` -> `param_runtime_apply_buffer_track`
  - `GLOBAL_ALLOWED` -> fallback `param_set`.

6. Insertion modulation
- `brick6_audio_runtime_dsp` appelle `mod_lfo_v1_process_block(frames)`.
- A chaque tick control-rate (3000 Hz), LFO:
  - verifie destination supportee via `track_runtime` + contexte UI track.
  - capture base `param_registry_get_track_value` si necessaire.
  - calcule valeur modulee.
  - applique via `param_registry_apply_track_value_rt_fast`.
  - restaure base quand destination change/depth=0/non supportee.

7. Consommation aval
- Audio/mixer/engines recoivent setters runtime immediats.
- Seq/clock recoivent changements de tempo/sync/length/div/swing/rec.
- UI lit via `param_get`, `param_store_get_active`, `param_registry_get_track_value`, `mod_lfo_v1_dest_*`.

## 7. Contraintes RT/CPU/memoire

Contraintes observees:
- Pas de malloc dans Z3: etats statiques uniquement.
- `mod_lfo_v1_process_block` est cadencee control-rate (stride derive de 48k/3k).
- `param_registry_apply_track_value_rt_fast` limite le cout (path court) pour modulation.
- `param_store_commit_if_block_advanced` borne les commits a 1 par avance bloc et evite writes continues intra-bloc.

Points sensibles cout/ordre:
- `param_set` peut declencher callbacks apply arbitraires (cout variable selon param).
- `param_registry_apply_track_value` fait refresh track hors batch (`track_runtime_refresh_track`), cout dependant des appels.

## 8. Invariants a ne pas casser

Invariants prouves:
- Les bornes min/max de `param_registry` sont appliquees avant ecriture effective.
- Les params LFO ne sont pas appliques comme params runtime directs: ils pilotent `mod_lfo_v1_*`.
- L'application track-aware depend explicitement des rules/statuts `track_runtime`.
- La modulation n'applique que des destinations valides (`mod_lfo_dest_supported_fast`) et restaure la base lors du release de destination.
- Coexistence prouvee legacy vs logique:
  - `PARAM_MIX_TRACKx_*` reste physique (4 tracks, `MAX_TRACKS`).
  - `PARAM_MIX_*` est resolu via track logique/runtime (`track_runtime` + mix target).

Invariant non prouve (donc exclu):
- autorite unique d'ecriture param (faux: chemins multiples coexistants).

## 9. Dependances inter-zones

Entrees vers Z3:
- Z5 UI/Interaction (edits et sync valeurs actives).
- Z4 Seq (plocks apply/restore via `seq_param_iface`).
- Z6 Storage (restore snapshots param/mod).
- Control router (staging/commit).

Sorties de Z3:
- Z1 Audio Hard-RT/Mix (setters audio/mixer/engines).
- Z2 Track Runtime Authority (consomme ses rules/status pour appliquer les params).
- Z4 Seq/Clock (tempo/sync/transport params config).

## 10. Dette technique observee

Points factuels:
- Double chemin d'ecriture maintenu (`param_set` global vs `param_registry_apply_track_value` track-aware), avec semantics differentes.
- `param_registry.c` est monolithique (metadata + dispatch + cache + filtre UI state + cfg seq + bindings engines).
- Dette legacy explicite `PARAM_MIX_TRACKx_*` (physique 4 tracks) coexiste avec mapping logique track-aware.
- Couplage fort a `track_runtime` pour validite/dispatch des domaines.
- Couplage d'ordre d'appel: modulation depend de l'appel bloc audio regulier; staging commit depend de l'avance de `g_audio_block_counter`.
- Couplage UI implicite dans Z3 (`ui_get_active_track`, family/type/midi source) pour apply et modulation destinations.

## 11. Impact eventuel sur la cartographie globale

- Z3 doit explicitement inclure `param_store` en sous-composant, sinon la chaine ecriture/staging/commit est incomplete.
- Frontiere Z3/Z2: Z3 applique, Z2 autorise/interdit (rules/status); cette dependance doit rester marquee bidirectionnelle fonctionnelle mais autorite cote Z2 pour policy.
- Sous-zone "legacy mix physique" (PARAM_MIX_TRACKx_*) doit etre documentee comme dette structurelle persistante, pas comme zone separee.
