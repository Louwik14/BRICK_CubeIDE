# Z3 - Param / Modulation / Control

## 1. Perimetre

Zone Z3 (coeur + modules param_registry):
- `Inc/Core/track_sound_state.h` / `Src/Core/track_sound_state.c`
- `Inc/Core/track_tone_sound_state.h` / `Src/Core/track_tone_sound_state.c`
- `Src/Param/param_registry.c` / `Inc/Param/param_registry.h`
- `Src/Param/param_registry_transition.c`
- `Src/Param/param_registry_catalog.c` / `Inc/Param/param_registry_catalog.h`
- `Src/Param/param_filter.c` / `Inc/Param/param_filter.h`
- `Src/Param/param_registry_backends.c` / `Inc/Param/param_registry_backends.h`
- `Src/Param/param_registry_tone_backends.c`
- `Src/Param/param_registry_runtime_state.c` / `Inc/Param/param_registry_runtime_state.h`
- `Src/Param/param_registry_apply_wrappers.c` / `Inc/Param/param_registry_apply_bindings.h`
- `Src/Param/param_store.c` / `Inc/Param/param_store.h`
- `Src/Mod/mod_lfo_v1.c` / `Inc/Mod/mod_lfo_v1.h`

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

## 2.b Repartition des responsabilites (etat courant)

- `param_registry.c`:
  - point d'entree Z3 autoritatif (`param_registry_apply_track_value`, `..._rt_fast`, transition structurelle),
  - orchestration autorisation + consommation des rules/resolution Z2 + sync minimale,
  - lecture directe de `track_state` pour les params CFG par-track.
- `param_registry_transition.c`:
  - pipeline structurel unique pour les mutations `CFG_TRACK` / `CFG_TRACK_TYPE`,
  - capture/rebind/neutralisation/reapply lane-bound hors du coeur d'apply courant.
- `param_registry_catalog.*`:
  - catalogue statique des descripteurs param (`param_registry[]`), labels, bornes, bindings `apply`.
- `param_filter.*`:
  - domaine FILTER complet: resolution cible, conversions, apply runtime, shadow-state UI, orchestration normal/rt_fast.
- `param_registry_backends.*`:
  - details backend par ressource/famille (mix, buffer, colors) consommes par le coeur Z3.
- `param_registry_tone_backends.*`:
  - exécuteur backend central pour le flux d'apply track-aware normal,
  - dispatch tone/mix-aware par engine/family stable.
- `param_registry_runtime_state.*`:
  - cache runtime track-scoped + commit authoritative write + bridge/resync LFO + invalidations associees.
- `param_registry_apply_wrappers.*`:
  - wrappers `apply_*` produit (CFG/SEQ/KBD/ARP/FX/LFO...), hors coeur d'execution track-aware.
  - pour les wrappers CFG track-aware, lecture post-apply sur `track_state` comme source autoritative de famille/type/MIDI.
- `track_sound_state.*`:
  - premiere base canonique par track pour les blocs sonores extraits du runtime,
  - contient actuellement les blocs communs MIX, MOD, FILTER et VCA comme premier noyau du modele parametrique par track,
  - contient aussi un bloc `input` track-aware pour les Input1/2/3 hybrides, avec `hybrid_gate` comme premiere autorite canonique,
  - consommee par param_filter, param_registry_backends et mod_lfo_v1 comme source persistante distincte du runtime.
- `track_tone_sound_state.*`:
  - base canonique par track pour les blocs TONE specifiques moteur,
  - contient le noyau Sampler, MIDI simple, TRX BD, TRX Claves, TRX HiHat, FM Kick, FM Snare, FM Tom, FM Rimshot, FM Clap, FM Cowbell et FM Cymbal par track,
  - consommee par param_registry_backends et param_registry comme source persistante distincte du runtime.

## 2.c Contrat public du seam `param_registry`

Surface `query`:
- `param_registry_get_track_value`
- `param_registry_runtime_get_or_default` quand il est utilise comme lecture de cache/default
- les helpers internes de lecture de domaine `param_registry_get_track_sound_value` et `param_registry_get_track_tone_value`
- `param_get` pour la valeur globale canonique

Surface `command / apply / transition / post-commit`:
- `param_registry_apply_track_value`
- `param_registry_apply_track_value_rt_fast`
- `param_registry_apply_track_edit`
- `param_registry_batch_begin`
- `param_registry_batch_end`
- `param_registry_sync_filter_ui_for_active_track`
- `param_registry_apply_track_structure_transition`
- `param_registry_run_track_transition_pipeline`
- `param_registry_runtime_commit_authoritative_write`
- `param_registry_runtime_resync_lfo`
- `param_set`
- `param_reset`

Ambiguite bornee restante:
- `param_registry_get_track_value` reste un multiplexeur de lecture large par domaine, mais il est contractuellement pure query.
- `param_registry_run_track_transition_pipeline` reste un orchestrateur interne de transition; il est classe cote mutation/post-commit, pas cote query.

Call-sites critiques:
- UI read paths use `param_registry_get_track_value` and `param_get` only.
- UI edit paths use `param_registry_apply_track_value` / `param_registry_apply_track_edit`.
- RT modulation uses `param_registry_apply_track_value_rt_fast`.
- Snapshot restore and structure changes use `param_registry_run_track_transition_pipeline`.

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
- post-restore global UI: via `ui_active_track_sync_full_after_global_restore()` (miroir UI actif fait cote Z5 via `ui_param_sync_active_track_mirror_from_runtime`, pas d'appel storage direct).

## 5. Invariants a ne pas casser

- Clamp min/max avant write effectif.
- `PARAM_LFO*` = params de config modulation, pas params runtime directs.
- `param_registry_apply_track_value_rt_fast` reserve a la modulation RT.
- Release LFO doit restaurer la base (et non la derniere valeur modulee).
- Pour `PARAM_FILTER_TYPE`, un re-apply de la meme valeur effective ne doit pas provoquer de reset DSP audible: la cible runtime mixer est idempotente sur type identique.
- `param_store.active[]`:
  - global-only: verite runtime.
  - track-scoped UI: miroir UI.
- `track_state`:
  - source autoritative par track pour family/type/MIDI,
  - lu directement par les wrappers CFG quand la valeur effective doit etre reflchee apres mutation.
- `track_sound_state`:
  - source autoritative par track pour les sous-ensembles communs MIX, MOD, FILTER et VCA actuellement extraits du runtime,
  - porte aussi l'autorite canonique `input.hybrid_gate` pour Input1/2/3 hybrides,
  - sert de premiere base du modele parametrique commun par track, distincte de `track_state`.
- `track_tone_sound_state`:
  - source autoritative par track pour les blocs TONE specifiques moteur deja extraits,
  - sert de base canonique Sampler, MIDI simple, TRX BD, TRX Claves, TRX HiHat, FM Kick, FM Snare, FM Tom, FM Rimshot, FM Clap, FM Cowbell et FM Cymbal par track, distincte de `track_sound_state`.
- `PARAM_MIX_TRACK0..3_*` reste un ilot tombstone/load-only borne.
- Pour les emissions MIDI CC/Program depuis Z3, la resolution du channel track passe par Z2 (`track_runtime_get_midi_channel_*`) et non par une lecture directe d'etat UI.

## 6. Dette technique restante (bornee)

- Coexistence maintenue `param_set` (global) vs `param_registry_apply_track_value` (track-aware).
- `param_registry.c` reste dense mais plus cible orchestration (le catalogue, FILTER, backends, runtime-state et wrappers sont externalises).
- Le chemin d'apply track-aware normal est maintenant plus lisible: autorisation -> backend -> resync LFO, avec un exécuteur backend centralisé cote `param_registry_tone_backends.c`.
- Les familles tonales stables ont ete extraites dans `param_registry_tone_backends.c`; `param_registry_backends.c` porte surtout les backends communs.
- Les commits de write runtime passent maintenant par un helper unique dans `param_registry_runtime_state.c`.
- Ilot legacy `PARAM_MIX_TRACK0..3_*` conserve pour layout storage et migration load-only; UI mute, restore normal et boot defaults ne l'utilisent plus comme runtime physique.

## 7. Carte courte de la dette reelle (audit code)

- Concentration structurelle (reelle):
  - le coeur est maintenant distribue en modules specialises; la densite residuelle porte surtout sur l'orchestration dans `param_registry.c`.
  - `param_registry_apply_track_value` reste le point unique multi-domaines (global/track-aware/filter/LFO) et conserve une logique de routage non triviale.

- Risques reels (encore actifs):
  - Risque de divergence `get/apply` sur les params filtre (`PARAM_FILTER_*`) car logique miroir (resolution cible + shadow-state + conversions) dupliquee entre `param_registry_get_track_value` et `param_registry_apply_track_value`.
  - Risque de regression silencieuse sur la sync base LFO: `mod_lfo_v1_resync_base_on_authoritative_write` est appelee sur de nombreux chemins manuels; un nouveau case oublie casserait la release vers la base.
  - Risque de confusion d'autorite `param_store.active[]` (verite globale vs miroir UI track) toujours present si appelant hors contrat lit sans distinguer domaine.

- Dette lisibilite (non urgente):
  - densite locale surtout dans les decisions d'orchestration Z3 (autorisation + routing des chemins).
  - `param_store.c` reste simple et contractuel; coupling vers le catalogue/bindings est connu et borne.

## 8. Plus petite prochaine passe utile

- Passe recommandee: clarification de frontiere documentaire (pas de refonte, pas de deplacement d'autorite).
- Action ciblee ensuite (micro-patch local possible, optionnelle):
  - Introduire un helper local unique pour la post-application track-aware (`cache/resync/return`) et l'utiliser dans `param_registry_apply_track_value` sur les branches repetitives.
  - Objectif: reduire le risque d'oubli de resync LFO sans changer le contrat `param_set` vs `param_registry_apply_track_value`, ni l'ilot legacy.

## 9. Impact sur cartographie globale

- La frontiere Z3/Z2 reste: Z2 autorise/contraint, Z3 applique.
- Les edits structurels UI `CFG_TRACK` / `CFG_TRACK_TYPE` empruntent maintenant le corridor complet Z3 (`param_registry_run_track_transition_pipeline` + finalisation lane-bound) au lieu d'une sync allÃ©gÃ©e parallele.
- Le pipeline structurel est dÃ©sormais isolÃ© dans `param_registry_transition.c`; `param_registry.c` conserve l'orchestration d'apply normal.
- Frontiere Z3/Z4 (live-rec param):
  - Hors PLAY+REC actif: edition param track-aware -> `param_registry_apply_track_value` (autorite Z3).
  - Sur ce chemin hors PLAY+REC, la sync base Seq post-apply passe par une commande explicite UI->Seq (`seq_param_iface_commit_base_after_authoritative_apply(cmd)`), avec cible/preconditions explicites; `seq_param_iface` ne lit plus `ui_get_active_track()` comme garde implicite.
  - En PLAY+REC actif: edition param track-aware routee vers Z4 (`seq_runtime_live_rec_param_write`) pour ecriture p-lock sequenceur.
  - Contrat d'autorite: pas de double write concurrent `param_registry_apply_track_value` + ecriture p-lock sur le meme edit live.

## 10. Contrat Sampler Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_SAMPLER`:
  - `Sample`, `Gain`, `Start`, `End`,
  - `Mode`, `Tune`, `Fade In`, `Fade Out`,
  - `Slice Count` visible en UI.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique.
  - le backend Sampler track-aware met a jour `track_tone_sound_state` puis `brick6_sampler_runtime`.
- `PARAM_SAMPLER_SAMPLE` met a jour la selection runtime sans retrigger automatique de preview.
- P-locks:
  - les params Sampler de base restent p-lockables via le flux track-aware.
- Invariants:
  - sample absent -> silence,
  - `Mode` pilote vraiment la direction et le type de lecture du moteur,
  - `Tune` est exprime en semitones avec pas UI de 1 st,
  - aucune allocation dynamique ni recalcul lourd dans l'IRQ audio.

## 11. Contrat MIDI simple Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_MIDI` et `UI_TRACK_TYPE_INPUT/HYBRID`:
  - `PARAM_MIDI_PROGRAM`,
  - `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - l'emission runtime reste dans les chemins existants (`seq_runtime_on_midi_program_live_change`, `midi_cc`).
- Invariants:
  - Program et CC restent des valeurs track-aware stables,
  - aucune logique Drum/Hybrid/routage audio n'entre dans ce bloc,
  - aucune seconde autorite runtime n'est introduite.

## 12. Contrat TRX BD Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_TRX_BD`:
  - `PARAM_DRUM_TRX_BD_PITCH`,
  - `PARAM_DRUM_TRX_BD_DECAY`,
  - `PARAM_DRUM_TRX_BD_PITCH_SWEEP`,
  - `PARAM_DRUM_TRX_BD_SWEEP_DECAY`,
  - `PARAM_DRUM_TRX_BD_ATTACK`,
  - `PARAM_DRUM_TRX_BD_NOISE`,
  - `PARAM_DRUM_TRX_BD_HARMONICS`,
  - `PARAM_DRUM_TRX_BD_DRIVE`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - TRX BD garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun Ã©tat runtime n'est pousse dans la base canonique.

## 13. Contrat TRX Claves Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_TRX_CLAVES`:
  - `PARAM_DRUM_TRX_CLAVES_PITCH`,
  - `PARAM_DRUM_TRX_CLAVES_INTERVAL`,
  - `PARAM_DRUM_TRX_CLAVES_DECAY`,
  - `PARAM_DRUM_TRX_CLAVES_BALANCE`,
  - `PARAM_DRUM_TRX_CLAVES_DRIVE`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - TRX Claves garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun Ã©tat runtime n'est pousse dans la base canonique.

## 14. Contrat TRX HiHat Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_TRX_HIHAT`:
  - `PARAM_DRUM_TRX_HIHAT_DECAY`,
  - `PARAM_DRUM_TRX_HIHAT_METAL`,
  - `PARAM_DRUM_TRX_HIHAT_HP_TONE`,
  - `PARAM_DRUM_TRX_HIHAT_LP_TONE`,
  - `PARAM_DRUM_TRX_HIHAT_GAP`,
  - `PARAM_DRUM_TRX_HIHAT_PEAK`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - TRX HiHat garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 15. Contrat FM Kick Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_KICK`:
  - `PARAM_DRUM_FM_KICK_PITCH`,
  - `PARAM_DRUM_FM_KICK_DECAY`,
  - `PARAM_DRUM_FM_KICK_FM_AMOUNT`,
  - `PARAM_DRUM_FM_KICK_PITCH_SWEEP`,
  - `PARAM_DRUM_FM_KICK_FEEDBACK`,
  - `PARAM_DRUM_FM_KICK_MOD_FREQ`,
  - `PARAM_DRUM_FM_KICK_MOD_DECAY`,
  - `PARAM_DRUM_FM_KICK_SWEEP_DECAY`,
  - `PARAM_DRUM_FM_KICK_RATIO_MODE`,
  - `PARAM_DRUM_FM_KICK_RATIO_INDEX`,
  - `PARAM_DRUM_FM_KICK_MOD_ENV_SYNC`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Kick garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 16. Contrat FM Snare Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_SNARE`:
  - `PARAM_DRUM_FM_SNARE_PITCH`,
  - `PARAM_DRUM_FM_SNARE_DECAY`,
  - `PARAM_DRUM_FM_SNARE_FM_AMOUNT`,
  - `PARAM_DRUM_FM_SNARE_NOISE`,
  - `PARAM_DRUM_FM_SNARE_HP_TONE`,
  - `PARAM_DRUM_FM_SNARE_MOD_FREQ`,
  - `PARAM_DRUM_FM_SNARE_MOD_DECAY`,
  - `PARAM_DRUM_FM_SNARE_NOISE_DECAY`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Snare garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 17. Contrat FM Tom Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_TOM`:
  - `PARAM_DRUM_FM_TOM_PITCH`,
  - `PARAM_DRUM_FM_TOM_DECAY`,
  - `PARAM_DRUM_FM_TOM_PITCH_SWEEP`,
  - `PARAM_DRUM_FM_TOM_FM_AMOUNT`,
  - `PARAM_DRUM_FM_TOM_MOD_FREQ`,
  - `PARAM_DRUM_FM_TOM_MOD_DECAY`,
  - `PARAM_DRUM_FM_TOM_SWEEP_DECAY`,
  - `PARAM_DRUM_FM_TOM_START_PHASE`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Tom garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 18. Contrat FM Rimshot Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_RIMSHOT`:
  - `PARAM_DRUM_FM_RIMSHOT_RIM_PITCH`,
  - `PARAM_DRUM_FM_RIMSHOT_RIM_DECAY`,
  - `PARAM_DRUM_FM_RIMSHOT_BODY_MIX`,
  - `PARAM_DRUM_FM_RIMSHOT_HP_TONE`,
  - `PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT`,
  - `PARAM_DRUM_FM_RIMSHOT_BODY_PITCH`,
  - `PARAM_DRUM_FM_RIMSHOT_BODY_DECAY`,
  - `PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT`,
  - `PARAM_DRUM_FM_RIMSHOT_MOD_DECAY`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Rimshot garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 19. Contrat FM Clap Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_CLAP`:
  - `PARAM_DRUM_FM_CLAP_CLAP_COUNT`,
  - `PARAM_DRUM_FM_CLAP_CLAP_SPACING`,
  - `PARAM_DRUM_FM_CLAP_TAIL_DECAY`,
  - `PARAM_DRUM_FM_CLAP_HP_TONE`,
  - `PARAM_DRUM_FM_CLAP_FEEDBACK`,
  - `PARAM_DRUM_FM_CLAP_FM_AMOUNT`,
  - `PARAM_DRUM_FM_CLAP_BASE_FREQ`,
  - `PARAM_DRUM_FM_CLAP_MOD_FREQ`,
  - `PARAM_DRUM_FM_CLAP_MOD_DECAY`,
  - `PARAM_DRUM_FM_CLAP_CLAP_DECAY`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Clap garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 20. Contrat FM Cowbell Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_COWBELL`:
  - `PARAM_DRUM_FM_COWBELL_PITCH`,
  - `PARAM_DRUM_FM_COWBELL_DECAY_SHORT`,
  - `PARAM_DRUM_FM_COWBELL_DECAY_LONG`,
  - `PARAM_DRUM_FM_COWBELL_FM_AMOUNT`,
  - `PARAM_DRUM_FM_COWBELL_FEEDBACK`,
  - `PARAM_DRUM_FM_COWBELL_ENV_MIX`,
  - `PARAM_DRUM_FM_COWBELL_MOD_DECAY`,
  - `PARAM_DRUM_FM_COWBELL_MOD_FREQ`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Cowbell garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 21. Contrat FM Cymbal Tone

- Params track-aware exposes pour `UI_TRACK_TYPE_DRUM_FM_CYMBAL`:
  - `PARAM_DRUM_FM_CYMBAL_DECAY`,
  - `PARAM_DRUM_FM_CYMBAL_SUSTAIN`,
  - `PARAM_DRUM_FM_CYMBAL_FM_AMOUNT`,
  - `PARAM_DRUM_FM_CYMBAL_HP_TONE`,
  - `PARAM_DRUM_FM_CYMBAL_FEEDBACK`,
  - `PARAM_DRUM_FM_CYMBAL_BASE_CARRIER`,
  - `PARAM_DRUM_FM_CYMBAL_BASE_MOD`,
  - `PARAM_DRUM_FM_CYMBAL_MOD_DECAY`.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est stockee dans `track_tone_sound_state`,
  - le runtime Drum reste l'executant via `drum_synth_set_param_for_instance`.
- Invariants:
  - FM Cymbal garde sa propre base track-aware,
  - aucun autre sous-moteur Drum n'entre dans ce bloc,
  - aucun ÃƒÂ©tat runtime n'est pousse dans la base canonique.

## 22. Contrat MIDI TONE (tranche fonctionnelle)

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

## 23. Contrat LFO MIDI (borne)

- Autorite de mapping destination LFO: `mod_lfo_v1` (selection des destinations supportees par track).
- Pour une track MIDI:
  - destinations LFO autorisees: `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4` (TONE/CC),
  - destinations LFO interdites: `PARAM_MIDI_PROGRAM`,
  - destinations LFO interdites: tout domaine `COLORS`.
- Application modulation runtime:
  - chemin `param_registry_apply_track_value_rt_fast`,
  - emission CC via `midi_cc` pour les destinations MIDI CC,
  - aucun backend audio ajoute.

## 24. Contrat Hybrid v1 (param/runtime borne)
- `PARAM_HYBRID_GATE` ajoute (bool: `OFF/ON`) pour `Input1/2/3` en mode `Hybrid` uniquement.
- `PARAM_HYBRID_GATE` pilote le gate VCA runtime du mix-track Hybrid:
  - `OFF`: bypass gate (audio input libre),
  - `ON`: gate actif pilote par activite note.
- Les params MIX track-aware (`LEVEL`, `PAN`, `SEND1`, `SEND2`, `MUTE`) vivent eux aussi dans `track_sound_state` comme base canonique par track, puis sont projetes vers le mixer runtime.
- La valeur canonique `hybrid_gate` vit dans `track_sound_state.input` comme autorite par track.
- Les params Sampler track-aware vivent dans `track_tone_sound_state` comme base canonique par track, puis sont projetes vers `brick6_sampler_runtime`.
- Les params TONE MIDI (`Program` + `CC`) sont acceptes aussi pour `Input1/2/3 Hybrid` (en plus de `family MIDI`):
  - `Program`: chemin live existant inchangÃ© (emit conditionnelle via runtime seq),
  - `CC`: emission directe `midi_cc`.
- Hors scope: aucun nouveau backend audio, aucune seconde autorite runtime.

## 25. Contrat LFO COLORS + rebind MIX (runtime)
- `param_registry_apply_track_value_rt_fast` est autorite d'application pour LFO sur `PARAM_FILTER_*` (COLORS mixer), pas uniquement pour COLORS engine-specifiques.
- Le chemin RT fast applique `PARAM_FILTER_*` sur la cible runtime resolue (`filter target`/`mix target`) sans ecraser la base UI/shadow-state track.
- Le shadow-state `PARAM_FILTER_*` porte la base par track logique, jamais par lane mixer physique.
- Le bloc MIX suit le meme principe: la base track-aware est portee par `track_sound_state`, la lane mixer n'est qu'une projection temporaire.
- Le bloc MOD suit le meme principe: la config LFO canonique par track est portee par `track_sound_state`, `mod_lfo_v1` n'en fait que l'execution/runtime et le cache de destination.
- Le bloc TONE Sampler suit le meme principe: la base canonique par track est portee par `track_tone_sound_state`, `brick6_sampler_runtime` n'en fait que l'execution/runtime.
- Lors d'un changement `CFG_TRACK`/`CFG_TRACK_TYPE`, Z3 migre d'abord le runtime per-lane (MIX/FILTER/VCA) selon le rebind des mix lanes, puis reapplique explicitement tous les params lane-bound track-aware (`FILTER_*`, `level/pan/sends/hybrid_gate/vca`) pour recoller le runtime a l'autorite logique.

## 26. Contrat corridor structurel Off -> On
- Le corridor structurel est maintenant unique et centralise cote Z3 via `param_registry_apply_track_structure_transition(...)`: capture des mix-targets precedents, mutation structurelle delegatee (callback Z5), rebind mixer, neutralisation runtime invalide, puis re-apply lane-bound avant toute resync UI active-track.
- Le re-apply lane-bound ne depend plus d'un cache partiel silencieux: l'autorite est explicite (`filter_ui_state` pour FILTER, cache track-aware sinon valeur par defaut promue dans le cache).
- Pendant ce corridor, les consommateurs de modulation control-rate (`mod_lfo_v1`) sont suspendus pour eviter une capture/restauration sur topologie intermediaire.

## 22. Contrat Passe 6 - Frontiere Z3 execution vs miroir UI Z5

- Contrat d'edit track-aware explicite:
  - Z5 emet `param_registry_track_edit_cmd_t` vers Z3 (`param_registry_apply_track_edit`).
  - Z3 applique sans relire le focus UI implicite.
- Contrat de transition structurelle explicite:
  - Z5 delegue la mutation runtime a Z3 via `param_registry_track_structure_transition_cmd_t`.
  - Z3 orchestre l'ordre capture -> mutation -> rebind/re-apply -> fin de transition.
- Contrat miroir UI:
  - le miroir `param_store.active[]` track-scoped actif est synchronise cote Z5 (`ui_param_sync_active_track_mirror_from_runtime`).
  - Z3 conserve l'autorite runtime d'execution; Z5 conserve l'autorite presentation/contexte d'edition.
- Pour `PLAY`, ce miroir UI reflÃ¨te la base seq canonique exposee par Z3.

## 15. Contrat Passe 1 - Autorite execution MIDI
- Les chemins d'application MIDI dans `param_registry` lisent le canal via Z2 (`track_runtime_get_midi_channel_zero_based`).
- Objectif: reduire le couplage implicite UI -> execution courante pour les writes runtime simples.

## 16. Contrat Passe 2 - Consommation du resolver Z2
- Les helpers de resolution de cible FILTER (`resolve_filter_target_track*`) consomment desormais `track_runtime_resolve_track`.
- Z3 n'interprete plus localement l'etat bind/mix-target pour ces chemins: la cible resolue vient de Z2.

## 17. Contrat Passe 3 - Apply engine interne clarifie

- `param_registry_apply_track_value` est desormais un routeur court:
  - params LFO config -> `mod_lfo_v1_set_track_param`,
  - params FILTER -> `param_apply_filter_track_value`,
  - autres params track-aware/global -> `param_apply_non_filter_track_value`.
- Le chemin non-FILTER est separe en 4 sous-roles explicites:
  - resolution contextuelle Z2 (`param_track_apply_ctx_build`),
  - autorisation (`param_track_apply_authorize`),
  - application backend (`param_track_apply_backend`),
  - sync post-apply (`param_track_apply_sync_after_apply`).
- La resolution structurelle consomme le resolver Z2 (`track_runtime_resolve_track`) plutot que `track_runtime_get_ctx` local dans le coeur d'apply.
- Les decisions MIDI TONE (Program/CC) sont autorisees depuis le descriptor resolu (`family/type`) puis appliquees localement (emit/cache).
- Le bloc FILTER garde son shadow-state UI, mais l'apply est isole dans un helper dedie au lieu d'etre melange au reste du dispatch.
- Le domaine `PLAY` a maintenant une base seq canonique: l'apply normal route vers `seq_param_iface_set_base_value`, le read normal remonte via `seq_param_iface_get_base_value`, et les locks seq mettent a jour cette meme base sans passer par le backend generique Z3.

Impact debug immediat:
- point d'entree d'apply plus lisible,
- etapes autorisation/resolution/apply/sync tracables en isolation,
- reduction du risque d'oubli de resync LFO sur les chemins non-FILTER.

Dette explicitement laissee pour Passe 4:
- extraire davantage la logique FILTER (shadow-state + conversions) pour reduire la duplication get/apply,
- rapprocher `param_registry_apply_track_value_rt_fast` de la meme frontiere interne,
- isoler les backends engine-specifiques hors du fichier monolithique `param_registry.c`.

## 18. Contrat Passe 4 - FILTER/RT fast/domaines explicites

- FILTER n'est plus applique via un bloc unique melange:
  - resolution cible: `param_filter_resolve_target`,
  - apply runtime mixer/audio: `param_filter_apply_runtime`,
  - mutation shadow-state UI track: `param_filter_update_shadow_state`,
  - orchestration complete: `param_filter_apply_value`.
- `param_apply_filter_track_value` est reduit a un routeur de politique (`update_shadow=1`, `resync=1`).
- `param_registry_apply_track_value_rt_fast` reutilise le meme coeur FILTER (`param_filter_apply_value`) avec une politique RT explicite (`update_shadow=0`, `resync=0`).
- Le non-FILTER RT fast suit maintenant une frontiere explicite (ctx/authorize/apply):
  - `param_track_rt_fast_ctx_build`,
  - `param_track_rt_fast_authorize`,
  - `param_track_rt_fast_apply_backend`.
- Domaines residuels clarifies explicitement:
  - en apply normal (`param_track_apply_backend`): `PLAY` et `MOD` => refuses explicitement,
  - en RT fast (`param_track_rt_fast_authorize` + backend): `PLAY`, `MOD` et `MIDI_PROGRAM` => refuses explicitement.
- Le chemin normal conserve la sync base LFO apres apply autoritatif; le RT fast reste sans sync base pour la modulation control-rate.

Dette explicite post-passe 4:
- `param_runtime_apply_track` reste encore mixe (dispatch tone/mix + engine-specific) dans le meme TU,
- le shadow FILTER reste local a `param_registry.c` (pas encore isole dans un sous-module dedie),
- la separation en fichiers Z3 (rules/resolution/apply/sync) reste a faire seulement si necessaire en passe suivante.





## 13. Contrat queries strictes - runtime state
- `param_registry_runtime_get_or_default` ne resynchronise plus LFO ni cache au passage; la query de valeur ne doit plus produire d'effet caché.
- Les commandes d'ecriture restent les seules autorites de commit runtime et de resync associe.

## 14. Contrat commandes explicites - apply track-aware
- `param_registry_apply_track_value` porte maintenant le refresh runtime explicite avant resolution et execution track-aware.
- `param_track_exec_ctx_build` redevient un helper de contexte pur; il ne fait plus de maintenance cache/runtime au passage.
