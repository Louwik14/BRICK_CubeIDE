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
  - Statut: tombstones de compat storage/load-only.
  - Pas de write runtime normal; les flows utilisateur MIX passent par `PARAM_MIX_*` et `param_registry_apply_track_value`.

## 3. Statut des chemins sensibles

- `control_router_set_param`:
  - Statut: dormant compat.
  - Aucun caller actif trouve dans `Src/`.
  - Les macros `CTRL_PARAM_MIX_TRACK0..3_*` ne sont pas exposees.

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
- post-restore global UI: via `ui_active_track_sync_full_after_global_restore()` (pas d'appel storage direct a `param_registry_sync_ui_for_active_track`).

## 5. Invariants a ne pas casser

- Clamp min/max avant write effectif.
- `PARAM_LFO*` = params de config modulation, pas params runtime directs.
- `param_registry_apply_track_value_rt_fast` reserve a la modulation RT.
- Release LFO doit restaurer la base (et non la derniere valeur modulee).
- Pour `PARAM_FILTER_TYPE`, un re-apply de la meme valeur effective ne doit pas provoquer de reset DSP audible: la cible runtime mixer est idempotente sur type identique.
- `param_store.active[]`:
  - global-only: verite runtime.
  - track-scoped UI: miroir UI.
- `PARAM_MIX_TRACK0..3_*` reste un ilot tombstone/load-only borne.

## 6. Dette technique restante (bornee)

- Coexistence maintenue `param_set` (global) vs `param_registry_apply_track_value` (track-aware).
- `param_registry.c` reste monolithique (metadata + dispatch + cache + filtres + bindings).
- Ilot legacy `PARAM_MIX_TRACK0..3_*` conserve pour layout storage et migration load-only; UI mute, restore normal et boot defaults ne l'utilisent plus comme runtime physique.

## 7. Carte courte de la dette reelle (audit code)

- Concentration structurelle (reelle):
  - `param_registry.c` cumule dans un seul TU: contrat public, table metadata complete, dispatch global, dispatch track-aware, cache runtime track, UI filter shadow-state, pont LFO, mappings legacy MIX, et sync UI.
  - La fonction `param_registry_apply_track_value` concentre plusieurs autorites (global/track-aware/filter/LFO) avec branches longues et duplications de patterns.

- Risques reels (encore actifs):
  - Risque de divergence `get/apply` sur les params filtre (`PARAM_FILTER_*`) car logique miroir (resolution cible + shadow-state + conversions) dupliquee entre `param_registry_get_track_value` et `param_registry_apply_track_value`.
  - Risque de regression silencieuse sur la sync base LFO: `mod_lfo_v1_resync_base_on_authoritative_write` est appelee sur de nombreux chemins manuels; un nouveau case oublie casserait la release vers la base.
  - Risque de confusion d'autorite `param_store.active[]` (verite globale vs miroir UI track) toujours present si appelant hors contrat lit sans distinguer domaine.

- Dette lisibilite (non urgente):
  - Densite de helpers et mappings locaux (conversions UI127, apply wrappers, enums labels) elevee mais stable.
  - `param_store.c` reste simple et contractuel; coupling vers `param_registry` pour defaults/apply est connu et borne.

## 8. Plus petite prochaine passe utile

- Passe recommandee: clarification de frontiere documentaire (pas de refonte, pas de deplacement d'autorite).
- Action ciblee ensuite (micro-patch local possible, optionnelle):
  - Introduire un helper local unique pour la post-application track-aware (`cache/resync/return`) et l'utiliser dans `param_registry_apply_track_value` sur les branches repetitives.
  - Objectif: reduire le risque d'oubli de resync LFO sans changer le contrat `param_set` vs `param_registry_apply_track_value`, ni l'ilot legacy.

## 9. Impact sur cartographie globale

- La frontiere Z3/Z2 reste: Z2 autorise/contraint, Z3 applique.
- Frontiere Z3/Z4 (live-rec param):
  - Hors PLAY+REC actif: edition param track-aware -> `param_registry_apply_track_value` (autorite Z3).
  - En PLAY+REC actif: edition param track-aware routee vers Z4 (`seq_runtime_live_rec_param_write`) pour ecriture p-lock sequenceur.
  - Contrat d'autorite: pas de double write concurrent `param_registry_apply_track_value` + ecriture p-lock sur le meme edit live.

## 10. Contrat Sampler Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_SAMPLER`:
  - `Sample`, `Gain`, `Start`, `End`,
  - `Mode`, `Tune`, `Fade In`, `Fade Out`,
  - `Slice Count` visible en UI.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique.
  - le backend Sampler track-aware est mis a jour sans pipeline parallele.
- `PARAM_SAMPLER_SAMPLE` met a jour la selection runtime sans retrigger automatique de preview.
- P-locks:
  - les params Sampler de base restent p-lockables via le flux track-aware.
- Invariants:
  - sample absent -> silence,
  - `Mode` pilote vraiment la direction et le type de lecture du moteur,
  - `Tune` est exprime en semitones avec pas UI de 1 st,
  - aucune allocation dynamique ni recalcul lourd dans l'IRQ audio.

## 10. Contrat MIDI TONE (tranche fonctionnelle)

- Nouveaux params track-aware TONE:
  - `PARAM_MIDI_PROGRAM` (0=OFF, 1..128 => Program 0..127),
  - `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4` (CC16..CC27).
- Autorite d'application:
  - Z3 reste point d'entree `param_registry_apply_track_value`,
  - emission MIDI live delegatee au runtime Z4 (`seq_runtime_on_midi_program_live_change`) pour Program,
  - emission MIDI live directe `midi_cc` pour CC.
- Garde runtime:
  - ces params sont acceptes uniquement si la track effective est `TRACK_RUNTIME_FAMILY_MIDI`,
  - aucun backend audio n'est active par ces params.
- Cache/runtime:
  - base track-aware stockee dans le cache runtime Z3 pour support plock/restore sans seconde autorite.

## 11. Contrat LFO MIDI (borne)

- Autorite de mapping destination LFO: `mod_lfo_v1` (selection des destinations supportees par track).
- Pour une track MIDI:
  - destinations LFO autorisees: `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4` (TONE/CC),
  - destinations LFO interdites: `PARAM_MIDI_PROGRAM`,
  - destinations LFO interdites: tout domaine `COLORS`.
- Application modulation runtime:
  - chemin `param_registry_apply_track_value_rt_fast`,
  - emission CC via `midi_cc` pour les destinations MIDI CC,
  - aucun backend audio ajoute.

## 12. Contrat Hybrid v1 (param/runtime borne)
- `PARAM_HYBRID_GATE` ajoute (bool: `OFF/ON`) pour `Input/Hybrid` uniquement.
- `PARAM_HYBRID_GATE` pilote le gate VCA runtime du mix-track Hybrid:
  - `OFF`: bypass gate (audio input libre),
  - `ON`: gate actif pilote par activite note.
- Les params TONE MIDI (`Program` + `CC`) sont acceptes aussi pour `Input/Hybrid` (en plus de `family MIDI`):
  - `Program`: chemin live existant inchangé (emit conditionnelle via runtime seq),
  - `CC`: emission directe `midi_cc`.
- Hors scope: aucun nouveau backend audio, aucune seconde autorite runtime.

## 13. Contrat LFO COLORS + rebind MIX (runtime)
- `param_registry_apply_track_value_rt_fast` est autorite d'application pour LFO sur `PARAM_FILTER_*` (COLORS mixer), pas uniquement pour COLORS engine-specifiques.
- Le chemin RT fast applique `PARAM_FILTER_*` sur la cible runtime resolue (`filter target`/`mix target`) sans ecraser la base UI/shadow-state track.
- Le shadow-state `PARAM_FILTER_*` porte la base par track logique, jamais par lane mixer physique.
- Lors d'un changement `CFG_TRACK`/`CFG_TRACK_TYPE`, Z3 migre d'abord le runtime per-lane (MIX/FILTER/VCA) selon le rebind des mix lanes, puis reapplique explicitement tous les params lane-bound track-aware (`FILTER_*`, `level/pan/sends/hybrid_gate/vca`) pour recoller le runtime a l'autorite logique.
