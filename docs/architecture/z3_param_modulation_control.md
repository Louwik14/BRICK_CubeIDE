# Z3 - Param / Modulation / Control

## Addendum 2026-07-28 - TONE Synth/Daisy catalogue generique

- `PARAM_DAISY_MODEL` et `PARAM_DAISY_PARAM1..15` ajoutent le stockage TONE canonique du moteur `Synth/Daisy`.
- Les bases vivent dans `track_tone_sound_state.daisy`; `param_registry` les lit/ecrit comme les autres TONE track-aware et `param_backend_apply_tone_daisy()` projette les writes vers `brick6_daisy_runtime`.
- Les 15 params generiques restent normalises `0..1`; leur nom musical depend du `MODEL` courant et appartient a Z5, afin d'eviter un catalogue parallele par algorithme.
- Le cache runtime `g_param_runtime_track_values` passe en `SEQ_STATE_D2` comme son masque de validite pour absorber la croissance de `PARAM_COUNT` sans saturer `RAM_D3`; ce cache reste non persistant et non autoritatif.
- Les p-locks TONE Daisy passent par les slots `TRACK_RUNTIME_TYPE_DAISY` existants. Les destinations Matrix Daisy sont publiees dynamiquement par modele actif: params continus uniquement, `MODEL`, `WAVE`, `SYNC` et `FIRST` exclus.
- Un write `PARAM_DAISY_MODEL` invalide le cache destination Matrix de la track pour que la liste MOD suive le layout TONE courant.

## Addendum 2026-07-28 - TONE Prism dual-osc

- Le bloc canonique Prism dans `track_tone_sound_state.prism` est maintenant indexe par oscillateur pour `MODEL/FINE/TUNE/FM AMT/PARAM1/AMOD/PARAM2/PHASE/LVL`.
- OSC1 reutilise les IDs historiques `PARAM_PRISM_*`; `PARAM_PRISM_LEVEL` et les IDs `PARAM_PRISM_OSC2_*` completent le layout courant. Pas de migration projet/pattern complexe dans cette passe.
- L'apply Prism projette chaque write vers `brick6_braids_runtime_set_osc_*`; l'edition UI de `TUNE` continue d'utiliser le controle unifie `COARSE + FINE`, avec `FINE` remis a `0.5` lors d'un edit direct.
- Les destinations Matrix Prism finales sont explicites et continues: `OSC1/OSC2 PARAM1`, `PARAM2`, `AMOD`, `TUNE`, `FM AMT`, `LVL`. `MODEL`, `FINE` et `PHASE` sont exclus des destinations continues.

## Addendum 2026-07-27 - TONE Synth/Wave

- `PARAM_WAVE_OSC1_*` et `PARAM_WAVE_OSC2_*` portent les 16 parametres TONE principaux du moteur `Synth/Wave`: `TABLE`, `POS`, `START`, `END`, `LEVEL`, `TUNE`, `PHASE`, `FLIP`.
- Les bases track-aware vivent dans `track_tone_sound_state.wave`; `param_registry` les lit/ecrit comme les autres TONE.
- `param_backend_apply_tone_wave()` projette les valeurs vers `brick6_wave_runtime`: `TABLE` cible un slot global `SAMPLE_GLOBAL_KIND_WAVETABLE`, `POS/START/END/LEVEL` sont continus 0..1, `TUNE` est en demi-tons avec pas UI normal 1 st et Shift 0.01 st, `PHASE` et `FLIP` restent discrets.
- `track_runtime` expose ces params uniquement via le layout TONE local `TRACK_RUNTIME_TYPE_WAVE`.
- Le catalogue Matrix expose seulement les destinations Wave continues `OSC1 POS`, `OSC1 LEVEL`, `OSC1 TUNE`, `OSC2 POS`, `OSC2 LEVEL`, `OSC2 TUNE`; l'application RT directe passe par `brick6_wave_runtime_set_osc_pos/level/tune()` sans mutation de la base canonique.
- `TABLE`, `START`, `END`, `PHASE` et `FLIP` restent exclus des destinations Matrix initiales: `TABLE/PHASE/FLIP` sont discrets/structurels, `START/END` restent des bornes statiques de scan; `POS` est l'entree continue modulee et lissee localement par le runtime Wave apres remap dans cette zone.

## Addendum 2026-07-28 - TONE 2/2 qualite Synth/Wave

- `PARAM_WAVE_FRAME_INTERP`, `PARAM_WAVE_SAMPLE_INTERP`, `PARAM_WAVE_POS_UPDATE` et `PARAM_WAVE_POS_SMOOTH` ajoutent les quatre reglages qualite/coût de `Synth/Wave`.
- Les bases track-aware vivent dans `track_tone_sound_state.wave` avec defaults Eco: `FRAME=OFF`, `SAMPLE=OFF`, `POSUPD=16`, `SMOOTH=ON`.
- `param_backend_apply_tone_wave()` projette ces reglages vers l'instance `brick6_wave_runtime` sans les exposer comme destinations Matrix.

## Addendum 2026-07-28 - ENV3 timing

- ENV3 reutilise les conversions musicales UI `param_filter_ui127_to_attack_s/decay_s/release_s` (`1 ms..5 s` exponentiel) mais doit inverser la loi cubique interne de `env_adsr_peaks_t` avant d'ecrire les champs `uint16_t`.
- Les temps ENV3 `ATTACK/DECAY/RELEASE` couvrent donc maintenant la meme plage cible `1 ms..5 s` que ENV FLT au lieu d'etre recompacts par une deuxieme loi cubique vers environ `1 sample..139 ms`.
- Ce changement est local a `mod_env3` et ne modifie ni ENV FLT ni ENV VCA.

## Addendum 2026-07-28 - ENV VCA Peak ADSR

- ENV VCA musicale utilise maintenant `env_adsr_peaks_t` via `env_adsr`, comme ENV FLT et ENV3, au lieu du wrapper `adsr_daisy_c_t`.
- Les params existants `PARAM_VCA_ATTACK/DECAY/SUSTAIN/RELEASE` restent inchanges cote UI et passent par les conversions musicales `param_filter_ui127_to_attack_s/decay_s/release_s` puis par le mapping inverse Peak ADSR du mixer (`1 ms..5 s` reel).
- `mixer_get_track_vca_env_value()` continue d'exposer la derniere sortie VCA normalisee `0..1` pour la Matrix; aucune ADSR parallele n'est ajoutee.

## Addendum 2026-07-26 - MOD operators MULTI/SLEW et LFO rate destinations

- Correction UI-label 2026-07-26: les labels longs de sources Matrix `ENV FLT/ENV VCA/ENV3` sont exposes cote catalogue MOD comme `env flt/env vca/env mod`; les IDs runtime et valeurs enum ne changent pas.

- Correction 2026-07-26: une route Matrix qui consomme `MULT1/MULT2/SLEW1/SLEW2` active aussi les sources amont requises par ces operateurs; `SOURCE=MULT1` avec `M1A=LFO1` et `M1B=ENV VCA` calcule donc les deux sources avant l'accumulation Matrix.
- Correction 2026-07-26: `SLEW1/SLEW2` utilisent maintenant le nombre de frames du tick Matrix pour convertir `AMT` en coefficient de lissage borne et reset leur etat interne quand la source change; le lissage ne depend plus d'un coefficient fixe implicite.

- `mod_matrix` ajoute quatre sources runtime control-rate: `MULT1`, `MULT2`, `SLEW1`, `SLEW2`.
- Les params track-aware MOD persistants `PARAM_MOD_MULTI_*` configurent `MULT1=M1A*M1B` et `MULT2=M2A*M2B`; les sorties sont clampées `-1..1`.
- Les params track-aware MOD persistants `PARAM_MOD_SLEW_*` configurent `SLEW1/SLEW2`; `AMT` est borné `0..1` et converti en coefficient linéaire borné au tick Matrix/LFO, jamais par sample audio.
- Les opérateurs sont évalués dans `mod_lfo_v1_process_block()` juste avant `mod_matrix_process_track()`. Les sources opérateur lues par d'autres opérateurs utilisent l'état opérateur précédent du tick courant/précédent; les cycles directs `SLEW1->SLEW1`, `SLEW2->SLEW2`, `SLEW1<->SLEW2` et les boucles directes `MULT*` sont ignorés.
- `PARAM_LFO1_RATE`, `PARAM_LFO2_RATE` et `PARAM_LFO3_RATE` sont destinations continues Matrix avec labels `L1Rt/lfo1rate`, `L2Rt/lfo2rate` et `L3Rt/lfo3rate`; l'application RT passe par le temp overlay LFO et la valeur modulee est clampee sans rate negatif cote destination. La Matrix conserve donc le comportement sync/index positif: elle peut moduler `OFF` et les divisions sync, mais ne descend pas dans la plage Hz libre negative.

## Addendum 2026-07-26 - ENV retrigger hard/soft

- Trois params track-aware append-only sont ajoutes au layout courant: `PARAM_ENV_RETRIG_FILTER`, `PARAM_ENV_RETRIG_VCA`, `PARAM_ENV_RETRIG_MOD`.
- Defaults: `ON` pour conserver le retrigger hard historique. `ON` = hard reset a zero au note-on; `OFF` = retrigger soft depuis la valeur courante.
- Les bases canoniques vivent dans `track_sound_state` (`env_retrig_filter`, `env_retrig_vca`, `env_retrig_mod`) et sont capturees/restaurees par les flux `PARAM_COUNT` existants.
- Application runtime: `ENV FLT` projette vers la lane filtre mixer resolue, `ENV VCA` vers la lane VCA mixer resolue, `ENV MOD` est lu par `mod_env3_note_on()`. `env_adsr_peaks_t` supporte deja le hard/soft via `env_adsr_retrigger(..., hard_reset)`.

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
- `Src/Param/param_macro.c` / `Inc/Param/param_macro.h`
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
  - Modulation runtime: chemins directs `mod_lfo_v1` quand une destination LFO effective est exposee; `param_registry_apply_track_value_rt_fast` reste fallback de securite/future destination.
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
  - les transitions structurelles distinguent explicitement un scope global et un scope local par track; une transition locale ne bloque plus les traitements MOD/LFO des autres tracks.
- `param_registry_catalog.*`:
  - catalogue statique des descripteurs param (`param_registry[]`), labels, bornes, bindings `apply`.
- `param_filter.*`:
  - domaine FILTER complet: resolution cible, conversions, apply runtime, shadow-state UI, orchestration normal/rt_fast.
- `param_registry_backends.*`:
  - details backend par ressource/famille (mix, colors, engines TONE) consommes par le coeur Z3.
- `param_registry_tone_backends.*`:
  - exécuteur backend central pour le flux d'apply track-aware normal,
  - dispatch tone/mix-aware par engine/family stable.
- `param_registry_runtime_state.*`:
  - cache runtime track-scoped + commit authoritative write + bridge/resync LFO + invalidations associees.
- `param_macro.*`:
  - seam Z3 dedie au MACRO runtime,
  - interpolation `base -> scene`, validation track-aware des 32 locks par scene, et handoff explicite vers `param_registry_apply_track_value`,
  - contrat produit: toute cible p-lockable selon `seq_param_iface_param_to_slot` + `seq_param_iface_param_is_supported` est assignable a l'overlay MACRO,
  - runtime source-state borne a 4 macro pots + 16 sources hall momentanees, chaque source parcourant au plus `PROJECT_V1_MACRO_SCENE_LOCK_COUNT` locks de la scene cible,
  - `g_param_macro_sources` est place en `CONTROL_STATE_SDRAM`: etat control low-rate, hors IRQ audio, non DMA-owned, initialise par `param_macro_init`,
  - pas d'autorite canonique propre, pas de stockage projet, pas de second cache runtime.
- `param_registry_apply_wrappers.*`:
  - wrappers `apply_*` produit (CFG/SEQ/KBD/ARP/FX/LFO...), hors coeur d'execution track-aware.
  - pour les wrappers CFG track-aware, lecture post-apply sur `track_state` comme source autoritative de famille/type/MIDI.
  - les commandes CFG avec track explicite passent par `param_registry_apply_track_value(..., track, ...)` et n'utilisent pas l'active track implicite des wrappers.
  - les wrappers ARP ecrivent maintenant l'etat ARP de la track active via `keyboard_runtime`; l'autorite config ARP est par track dans `keyboard_arp`, pas `param_store.active[]`.
  - le runtime de jeu ARP est aussi par track cote `keyboard_arp`; les writes HOLD/ARP ne doivent pas couper les autres tracks.
- `track_sound_state.*`:
  - premiere base canonique par track pour les blocs sonores extraits du runtime,
  - contient actuellement les blocs communs MIX, MOD, FILTER et VCA comme premier noyau du modele parametrique par track,
  - contient aussi un bloc `input` track-aware pour les Input1/2/3 hybrides, avec `hybrid_gate` comme premiere autorite canonique,
  - consommee par param_filter, param_registry_backends et mod_lfo_v1 comme source persistante distincte du runtime.
- `track_tone_sound_state.*`:
  - base canonique par track pour les blocs TONE specifiques moteur,
  - le bloc Prism est borne a 8 params TONE: `EDIT`, `FINE`, `COARSE`, `FM`, `TIMBRE`, `MODULATION`, `COLOR`, `PHASE RESET`,
  - `PARAM_PRISM_EDIT` expose une liste compacte de 39 shapes: variantes filtrees `LP`, `PEAK`, `BP`, `HP` et modes delay-line `COMB_FILTER`, `PLUCKED`, `BOWED`, `BLOWN`, `FLUTED` retires de la surface produit,
  - consommee par param_registry_backends et param_registry comme source persistante distincte du runtime,
  - source unique de re-projection des params Prism apres reset/rebind d'instance runtime.
  - Pour `PARAM_SAMPLER_SAMPLE` hors Multi, la valeur canonique utilisateur est un slot `sample_global_pool` actif `0..255`; l'apply Sampler resout ensuite `STREAM -> backend_index sample_pool` avant de toucher le runtime audio Classic.

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
- `param_macro_init`
- `param_macro_lerp`
- `param_macro_resolve_lock`
- `param_macro_resolve_slot` (compat legacy)
- `param_macro_apply_resolution`
- `param_macro_apply_slot`
- `param_macro_sync_active_bank`
- `param_macro_set_amount`
- `param_macro_adjust_amount`
- `param_macro_get_amount`
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
- RT modulation uses `mod_lfo_v1` direct paths for known effective destinations; `param_registry_apply_track_value_rt_fast` is fallback-only.
- Snapshot restore and structure changes use `param_registry_run_track_transition_pipeline`.
- Playback p-locks use a runtime-temp apply surface: they project the locked value to the engine/runtime only, never to the canonical base stores read by UI (`track_sound_state`, `track_tone_sound_state`, FILTER shadow, LFO config, seq PLAY base). Restore reprojects the base to runtime without changing the displayed/editable value.

## 3. Statut des chemins sensibles

- `control_router_set_param`:
  - Statut: dormant compat.
  - Aucun caller actif trouve dans `Src/`.
  - Les macros `CTRL_PARAM_MIX_TRACK0..3_*` ne sont pas exposees.

- Restore LFO/Matrix (Z6 via `pattern_live_apply_snapshot`):
  - Autorite unique: les valeurs track-aware du layout courant `PARAM_COUNT`.
  - Aucun payload separe LFO `DEST/DEPTH` n'est restaure pour Matrix/ENV3.

- Trig LFO BRICK6:
  - Les evenements trig LFO actuels sont les note-on valides emis par le scheduler sequenceur et le clavier runtime; il n'existe pas encore de parametre separe type `LFO.T`.
  - `FREE` ignore ces trigs: la phase n'est pas relancee par les notes et la modulation continue tant que `RATE != OFF` et qu'une route Matrix active consomme la source.
  - `TRIG` ne gate pas le LFO: avec une destination, une depth et un rate valides, le LFO reste actif; chaque trig LFO rephase selon `PHASE` et relance `DELAY`/`FADE`.
  - `HOLD` tient la derniere valeur capturee; chaque trig LFO relance `DELAY`, puis capture une nouvelle valeur. Entre deux captures, la sortie appliquee reste stable.
  - `ONE` arme un cycle a l'activation effective et a chaque trig LFO; apres un cycle complet, la destination est relachee vers sa base, donc la modulation revient a zero jusqu'au prochain trig.
  - Pour `RND`, aucun offset de phase n'est applique, et `HOLD` capture une nouvelle valeur sample-and-hold.

- Coexistence base write / modulation RT:
  - Contrat: un write track-aware autoritatif resynchronise la base LFO active (`mod_lfo_v1_resync_base_on_authoritative_write`).
  - A la release (dest change / depth=0 / dest non supportee): restauration de cette base.

## 4. Flux runtime (condense)

1. Ecriture base:
- global: `param_set`.
- track-aware: `param_registry_apply_track_value`.

2. Modulation:
- Tick control-rate depuis audio bloc.
- Une transition structurelle globale peut suspendre le tick LFO complet; une transition locale suspend uniquement la track cible, les autres tracks continuent leur modulation.
- Capture base via `param_registry_get_track_value`.
- Apply module via chemin direct `mod_lfo_v1` si la destination est connue/effective.
- Release -> write base via le meme chemin direct; fallback `_rt_fast` uniquement pour une destination future non specialisee.

3. MACRO:
- Resolution lock via `param_macro_resolve_lock` sur la scene cible.
- Interpolation base -> scene via `param_macro_lerp`.
- Handoff d'ecriture via `param_macro_apply_resolution` vers le chemin track-aware standard.
- La source d'autorite d'assignabilite MACRO est volontairement la meme que les p-locks: domaine Z2 -> set `SEQ_PLOCK_SET_*`, puis `seq_param_iface_param_is_supported(track,set,param)`.
- Contrat produit: `p-lockable => macro-assignable`; aucune table MACRO separee ne doit retirer un parametre p-lockable.
- Les assignations MACRO deja existantes hors p-lock (`MIX`) restent conservees par compatibilite produit, sans devenir p-lockables.
- Le preview MACRO applique les cibles non-FILTER via `param_backend_apply_track_value(..., update_base_state=0)` afin de partager le meme dispatcher actif que les writes track-aware sans modifier la base canonique; cela couvre Sampler, Drum, Prism et MIX.
- Les cibles `PLAY`, `MOD` et `MIDI Program` passent par `param_registry_apply_track_value`, puis sont restaurees via la meme release MACRO que les autres locks.
- Les amounts runtime des 4 macro pots sont re-projetés via `param_macro_set_amount` / `param_macro_sync_active_bank` sans passer par `param_store`; chaque pot pointe vers une scene projet.
- Pendant un maintien de scene en overlay `M-Assign`, un mouvement de macro pot bind le pot a cette scene via un set projet sans recomposition runtime immediate; le morph audio du pot ne part pas pendant ce geste.
- Les sources hall en overlay `M-Ctrl` utilisent `param_macro_set_scene_source_amount` / `param_macro_release_scene_source`, restent momentanees et ne changent jamais la base canonique.
- L'arbitrage multi-source est borne et statique: toutes les sources actives sont relâchees vers leur base puis reappliquees dans l'ordre de dernier toucher; si plusieurs sources ciblent le meme parametre, la derniere touchee gagne et une source precedente encore active reprend apres release de la gagnante.
- La capacite MACRO runtime est `16 scenes * 32 locks`; les 4 pots ne possedent pas les locks, ils pointent seulement vers une scene.
- Worst-case control-rate: une recomposition peut relacher puis reappliquer les 20 sources statiques, avec au plus 32 resolutions/applies par source active; ce parcours reste hors IRQ audio, sans malloc et borne par constantes compile-time.

4. Restore snapshot:
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
- Les params FILTER ADSR (`EG Amt`, `Atk`, `Dec`, `Sus`, `Rel`) sont des params `COLORS` track-aware: ils sont p-lockables, macro-assignables, et appliques via `param_filter_apply_value` vers les setters mixer filter envelope.
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
- P-lock playback:
  - la valeur UI est toujours la base canonique editable,
  - la valeur p-lock est une projection runtime temporaire,
  - les p-locks ne mettent pas a jour le miroir UI, les bases track-scoped ni le cache runtime autoritatif,
  - les p-locks ne pilotent pas l'affichage live; seul un feedback volontaire de p-lock en edition/step context peut afficher une valeur de lock.
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
  - page 1: `Sample`, `Mode`, `Start`, `End`,
  - page 2: `Gain`, `Tune`, `Loop`, `Slice`,

## 11. Contrat transition structurelle locale

- Les edits locaux `CFG_TRACK` / `CFG_TRACK_TYPE` passent par `param_registry_run_track_transition_pipeline_for_track(cmd, track)`.
- Le pipeline local capture uniquement le mix target de la track cible, refresh uniquement cette track, rebind uniquement sa lane mixer et re-apply uniquement les params runtime de cette track.
- La re-application locale couvre les params communs lane-bound (`COLORS/FILTER`, `MIX`, `VCA`, `HYBRID_GATE`) puis les params `TONE` du type runtime courant, sans toucher les autres tracks et sans repasser par `refresh_all()`.
- Apres re-application locale, le mixer snap les valeurs lissees de la lane cible vers les targets reappliquees pour eviter qu'un rebind neuf joue un bloc avec les defaults internes du mixer alors que l'UI/base track-aware garde les bonnes valeurs.
- Le pipeline global `param_registry_run_track_transition_pipeline(cmd)` reste le chemin restore/load/init et conserve le refresh/rebind global.
- Le rebind mixer local ne reset pas les lanes des autres tracks; les lanes inchangees sont no-op.
  - `Fade In`/`Fade Out` sont retires du contrat Sampler RAM; l'enveloppe d'amplitude reste VCA,
  - `Loop` expose `PARAM_SAMPLER_LOOP_START`, un marqueur/edit position track-aware stocke comme ratio `0..1` et projete vers le runtime RAM sans modifier `Start` ni `End`,
  - `Slice Count` visible en UI sur `Sampler/RAM`; `Off` garde RAM normal, `2..64` active un slicing grille de la fenetre globale `Start/End`.
- Params track-aware exposes pour `UI_TRACK_TYPE_STREAM`/`UI_TRACK_TYPE_CLIP`:
  - `Sample`, `Gain`, `Src BPM`,
  - `Play Mode`, `Loop`, `Stretch`,
  - `Tune` expose le parametre interne legacy `PARAM_SAMPLER_CLIP_PITCH` cote produit/UI sans changer le chemin DSP,
  - `Sync Len`,
  - `Grain` expose pour `Stretch=Shifter`; `Hop` et `Search` restent des params reserves non exposes produit; `Search` n'est plus une destination LFO valide,
  - `Stream` reste borne produit a `BRICK6_MAX_CLIP_TRACKS=4`; au-dela, le catalogue UI ne propose plus ce type aux tracks non deja `Stream`.
  - `Stretch=Off` force une lecture stream a vitesse/pitch d'origine (`ratio=1.0`, pas de tempo-sync),
  - `Stretch=Speed` garde le varispeed courant (`ratio = project_bpm / source_bpm`, pitch non preserve), sans nouvelle correction distribuee dans cette passe,
  - `Stretch=Shifter` garde le cursor varispeed `Speed`, puis applique le pitch-shifter stereo local `brick6_clip_shifter`; `Tune` (`PARAM_SAMPLER_CLIP_PITCH` interne) et le ratio tempo alimentent `brick6_clip_shifter_set_pitch_correction(pitch_ratio / timing_ratio)`, `Grain` pilote la fenetre, `Hop/Search` restent stockes mais sans effet dans ce mode,
  - `Stretch Mode` reste un param track-aware `PLAY` borne a `0..2`: `0=Off`, `1=Speed`, `2=Shifter`; l'edition UI ne doit pas reboucler via `param_set`, et le setter runtime Sampler reste passif (stockage seulement, effet applique au prochain start/restart Stream),
  - aucune retrocompatibilite n'est conservee pour les anciens projets utilisant l'ancien mode `Stretch`/WSOLA; `PROJECT_V1_FILE_VERSION` refuse ces fichiers,
  - `Grain` reste un setter passif track-aware pris en compte au prochain start/restart `Shifter`; `Hop/Search` ne pilotent plus aucun runtime Sampler/Stream,
  - `Sync Len` reste track-aware et stocke la longueur musicale stream exposee au niveau produit.
- Params TONE exposes pour `UI_TRACK_TYPE_MULTI`:
  - `INST`, selecteur track-aware parmi `NONE` et les instruments deja presents dans le `multi_sample_pool`, sans browser, import, scan SD ni reload,
  - `GAIN`, applique via `brick6_sampler_runtime_set_multi_gain`,
  - `LOOP`, nouveau bool `PARAM_SAMPLER_MULTI_LOOP` append-only, default `OFF`, applique via `brick6_sampler_runtime_set_multi_loop`.
  - `INST` reutilise le slot param existant `PARAM_SAMPLER_SAMPLE` avec un chemin specialise `Sampler/Multi`: la valeur UI `0` desassigne la track, les valeurs `1..N` parcourent les instruments charges du pool, et l'apply ecrit `brick6_sampler_runtime_set_multi_instrument`; aucun changement de sample RAM/Stream n'est declenche en mode Multi.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique.
  - le backend Sampler track-aware met a jour `track_tone_sound_state` puis `brick6_sampler_runtime`.
  - `track_tone_sound_state.multi.loop` porte la valeur canonique track-aware et persiste avec le layout global `PARAM_COUNT`.
- `PARAM_SAMPLER_SAMPLE` met a jour la selection runtime sans retrigger automatique de preview.
- P-locks:
  - les params Sampler de base restent p-lockables via le flux track-aware.
  - Pour `Sampler/RAM`, les p-locks `START`, `END`, `MODE` et `LOOP` projettent une valeur runtime temporaire: ils peuvent atteindre les voix RAM actives, mais ne modifient pas `track_tone_sound_state`, `param_store.active[]` ni l'affichage UI.
  - `PARAM_SAMPLER_LOOP_START` reste independant de `START/END`: si le marqueur est hors plage fonctionnelle, le runtime ignore la boucle sans corriger la valeur stockee/visible.
  - Pour `Sampler/RAM` sliced, `START`/`END` restent des params globaux de la track et definissent la fenetre slicee du trig; `Slice Count` reste exclu du p-lock et aucun etat par-slice n'est introduit.
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

## 12. Contrat Drum Tone final

- Types Drum produit:
  - `UI_TRACK_TYPE_DRUM_TRX_BD`: slot reserve/futur, expose en UI, silencieux tant que le moteur n'est pas reinstalle,
  - `UI_TRACK_TYPE_DRUM_BD_ANALOG`: moteur actif Plaits `AnalogBassDrum`.
- Params Drum restants dans `PARAM_COUNT`:
  - `PARAM_DRUM_TRX_BD_PITCH`,
  - `PARAM_DRUM_TRX_BD_DECAY`,
  - `PARAM_DRUM_TRX_BD_PITCH_SWEEP`,
  - `PARAM_DRUM_TRX_BD_SWEEP_DECAY`,
  - `PARAM_DRUM_TRX_BD_ATTACK`,
  - `PARAM_DRUM_TRX_BD_NOISE`,
  - `PARAM_DRUM_TRX_BD_HARMONICS`,
  - `PARAM_DRUM_TRX_BD_DRIVE`.
- `BD_ANALOG` expose seulement quatre controles TONE via la table runtime:
  - `PARAM_DRUM_TRX_BD_PITCH` -> pitch/f0 Plaits,
  - `PARAM_DRUM_TRX_BD_DECAY` -> decay Plaits,
  - `PARAM_DRUM_TRX_BD_HARMONICS` -> tone Plaits,
  - `PARAM_DRUM_TRX_BD_PITCH_SWEEP` -> attack FM Plaits.
- Autorite:
  - `param_registry_apply_track_value` reste point d'entree unique,
  - la base canonique est `track_tone_sound_state.trx_bd`,
  - `drum_synth` est seulement l'executant runtime.
- Invariants:
  - aucun autre type ou param Drum n'est conserve,
  - aucun chemin de compatibilite ancien projet Drum n'est maintenu,
  - les types Drum numeriques inconnus passent par la validation catalogue generique et ne recreent pas de moteur.
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
- Pour les destinations `TONE`, le filtrage passe par `track_runtime_tone_param_to_slot(type,param,&slot)` afin de rester aligne avec le mapping runtime effectif; il ne doit pas utiliser `seq_param_iface_map_param`.
- Pour une track MIDI:
  - destinations LFO autorisees: `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4` (TONE/CC),
  - destinations LFO interdites: `PARAM_MIDI_PROGRAM`,
  - destinations LFO interdites: tout domaine `COLORS`.
- Application modulation runtime:
  - chemin direct `mod_lfo_v1` pour les destinations MIDI CC,
  - emission CC via `midi_cc` seulement quand la valeur 7-bit change,
  - aucun backend audio ajoute.

## 23.b Contrat LFO Prism / Drum direct

- Les destinations Prism directes sont `PARAM_PRISM_EDIT`, `FINE`, `COARSE`, `FM`, `TIMBRE`, `MODULATION` et `COLOR`.
- `PARAM_PRISM_PHASE_RESET` reste exclu des destinations LFO directes et du catalogue LFO effectif: c'est un comportement de reset/trigger, pas un parametre continu.
- Les labels de ces destinations Prism passent par le helper commun `param_prism_labels`: ils refletent l'etat canonique courant `PARAM_PRISM_EDIT` de la track et restent alignes avec les labels TONE, sans suivre dynamiquement les p-locks/LFO temporaires sur le choix de moteur.
- Les destinations Drum directes actives sont les quatre controles `BD_ANALOG` exposes par le mapping TONE runtime: `PARAM_DRUM_TRX_BD_PITCH`, `DECAY`, `HARMONICS` et `PITCH_SWEEP`.
- Les params Drum reserves/TRX (`SWEEP_DECAY`, `ATTACK`, `NOISE`, `DRIVE`) restent generiques/non exposes pour `BD_ANALOG`; ils n'ont pas de setter runtime actif clair dans `drum_synth`.
- Application modulation runtime:
  - `mod_lfo_v1` calcule toujours `base_value + modulation`, clamp avec les bornes catalogue, puis appelle le setter runtime Prism ou Drum,
  - aucune mise a jour de base canonique `track_tone_sound_state`,
  - aucune emission UI/save/p-lock supplementaire,
  - release, changement de destination, depth 0 et double LFO meme destination conservent le contrat existant de restauration de la base.

## 23.c Contrat LFO FILTER EQ / MIDI CC direct

- Les destinations FILTER directes supplementaires sont `PARAM_FILTER_EQ_LOW`, `PARAM_FILTER_EQ_MID` et `PARAM_FILTER_EQ_HIGH`.
- `PARAM_FILTER_KEYTRK` reste un parametre filtre valide hors LFO, mais n'est plus expose dans le catalogue des destinations LFO.
- `PARAM_FILTER_TYPE` est exclu du catalogue LFO: changement enum de structure DSP, meme si le setter mixer est idempotent sur type identique.
- `PARAM_FILTER_ENVRST` est exclu du catalogue LFO: flag/reset d'enveloppe, pas une modulation continue.
- `PARAM_FILTER_ENVDLY` est exclu du catalogue LFO: pas de setter runtime effectif dans le mixer courant.
- Les destinations MIDI directes sont `PARAM_MIDI_CC1_1..PARAM_MIDI_CC3_4` pour tracks `MIDI` et `Input/Hybrid`; `PARAM_MIDI_PROGRAM` reste exclu des destinations LFO.
- Application modulation runtime:
  - FILTER direct appelle les setters mixer sur la cible runtime resolue, avec conversions `param_filter`,
  - MIDI CC direct appelle l'emission CC existante sans ecrire la base, et n'emet que si la valeur CC 7-bit arrondie change,
  - aucun chemin ne modifie `track_sound_state`, `track_tone_sound_state`, `param_store` ou l'etat p-lock,
  - release et changement de destination restent geres par la base capturee dans `mod_lfo_v1`.
- Nettoyage catalogue LFO:
  - `PARAM_SAMPLER_SAMPLE` est exclu: selection/load/import possible selon type Sampler/Stream/Multi.
  - `PARAM_MASTER_FX1_TYPE..PARAM_MASTER_FX4_B` sont exclus tant qu'il n'existe pas de setter runtime/overlay dedie; le fallback `rt_fast` courant ne modifie pas le son car `param_backend_apply_master_fx_track(..., update_base_state=0)` retourne sans changer `track_tone_sound_state`.
  - Les params Drum TRX reserves `PARAM_DRUM_TRX_BD_PITCH..PARAM_DRUM_TRX_BD_DRIVE` sont exclus pour `TRACK_RUNTIME_TYPE_DRUM_TRX_BD`: le type est reserve/silencieux et `drum_synth` n'a pas de modele actif TRX.

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
- `mod_lfo_v1` applique directement les destinations LFO FILTER continues exposees (`CUTOFF`, `RESONANCE`, `EQ_LOW`, `EQ_MID`, `EQ_HIGH`, `EG_AMT`, `ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE`) sur la cible runtime resolue (`filter target`/`mix target`) sans ecraser la base UI/shadow-state track.
- `param_registry_apply_track_value_rt_fast` reste fallback de securite pour destination future non specialisee; il n'est plus le chemin volontaire des destinations LFO FILTER effectives.
- La page produit `COLORS/CRUNCH` est retiree: `PARAM_FILTER_DRIVE`, `PARAM_FILTER_DECIMATOR_BITS`, `PARAM_FILTER_DECIMATOR_RATE` et `PARAM_FILTER_DECIMATOR_RATE2` ne font plus partie du domaine COLORS effectif, ne sont plus p-lockables/macro-assignables et leurs wrappers d'apply ne branchent plus de runtime.
- Le shadow-state `PARAM_FILTER_*` porte la base par track logique, jamais par lane mixer physique.
- Le bloc MIX suit le meme principe: la base track-aware est portee par `track_sound_state`, la lane mixer n'est qu'une projection temporaire.
- Le bloc MOD suit le meme principe: la config LFO canonique par track est portee par `track_sound_state`, `mod_lfo_v1` n'en fait que l'execution/runtime et le cache de destination.
- Le bloc TONE Sampler suit le meme principe: la base canonique par track est portee par `track_tone_sound_state`, `brick6_sampler_runtime` n'en fait que l'execution/runtime.
- Lors d'un changement `CFG_TRACK`/`CFG_TRACK_TYPE`, Z3 migre d'abord le runtime per-lane (MIX/FILTER/VCA) selon le rebind des mix lanes, puis reapplique explicitement tous les params lane-bound track-aware (`FILTER_*`, `level/pan/sends/hybrid_gate/vca`) pour recoller le runtime a l'autorite logique.
- Le corridor local suppose que Z2 ne donne jamais a une track non-Input une lane reservee `Input1..3`; ainsi un scroll de family qui traverse les familles Input ne peut plus reset la lane MIX/FILTER d'une autre track.

## 26. Contrat corridor structurel Off -> On
- Le corridor structurel est maintenant unique et centralise cote Z3 via `param_registry_apply_track_structure_transition(...)`: capture des mix-targets precedents, mutation structurelle delegatee (callback Z5), rebind mixer, neutralisation runtime invalide, puis re-apply lane-bound avant toute resync UI active-track.
- Le re-apply lane-bound ne depend plus d'un cache partiel silencieux: l'autorite est explicite (`filter_ui_state` pour FILTER, cache track-aware sinon valeur par defaut promue dans le cache).
- Pendant ce corridor, les consommateurs de modulation control-rate (`mod_lfo_v1`) sont suspendus pour eviter une capture/restauration sur topologie intermediaire.

## 27. Contrat send2 delay global
- `PARAM_MIX_SEND2` reste un param MIX track-aware stocke par track dans `track_sound_state` et projete vers `mixer_set_track_send_level(..., 1U, ...)`.
- Les params delay sont globaux:
  - `PARAM_MIX_DELAY_TIME`
  - `PARAM_MIX_DELAY_PINGPONG`
  - `PARAM_MIX_DELAY_WIDTH`
  - `PARAM_MIX_DELAY_FEEDBACK`
  - `PARAM_MIX_DELAY_HPF`
  - `PARAM_MIX_DELAY_LPF`
  - `PARAM_MIX_DELAY_REV`
  - `PARAM_MIX_DELAY_VOL`
- `PARAM_MIX_DELAY_TIME` est stocke comme division musicale sync BPM (`1/32`..`1 bar`), pas comme duree ms/secondes.
- `apply_mix_delay_time()` lit l'autorite tempo `seq_runtime` (`seq_runtime_get_tempo_bpm_milli()` ou tempo externe valide selon `seq_runtime_get_clock_source()`), calcule la duree effective, puis conserve le smoothing/interpolation cote DSP.
- `PARAM_MIX_DELAY_WIDTH` est bipolaire: `-1` mono, `0` stereo naturel, `+1` wide borne.
- `PARAM_MIX_DELAY_PINGPONG` remplace l'ancien mode discret; aucun crossfeed n'est expose.
- `PARAM_MIX_DELAY_REV` envoie le wet delay vers la reverb globale via `mixer_process()`, sans boucle reverb -> delay.
- Leur apply passe par les wrappers `apply_mix_delay_*` puis par les setters mixer `mixer_set_delay_*`.
- Le delay n'est pas un `fx_pool` slot et ne cree pas d'autorite par track.
- `PARAM_MIX_REVERB_HPF` et `PARAM_MIX_REVERB_LPF` sont globaux et filtrent l'entree stereo de la reverb globale en pre-reverb.
- Leur apply passe par `apply_mix_reverb_hpf/lpf` puis `mixer_set_reverb_hpf/lpf`; `0.0` reste neutre pour les deux params.
- Les anciens params `PARAM_MIX_REVERB_TYPE` et `PARAM_MIX_REVERB_SURR` sont retires du layout courant: le premier ne choisissait aucun backend, le second n'avait aucun effet DSP.
- Les defaults reverb boot/catalog sont `Wet=0.0`, `Size=0.0`, `Decay=0.5`, `PreD=0.5`, `HPF=0.0`, `LPF=0.0`; `PreD` est converti en secondes par le setter mixer.
- `RevB` consomme les params globaux utiles (`Wet`, `Size`, `Decay`, `PreD`, `LPF`) sans choix de backend.

## 28. Contrat send2 delay DUAL

- Les params delay restent globaux et ne creent pas d'autorite par track:
  - `PARAM_MIX_DELAY_TYPE`,
  - `PARAM_MIX_DELAY_TIME`,
  - `PARAM_MIX_DELAY_PINGPONG`,
  - `PARAM_MIX_DELAY_MODE`,
  - `PARAM_MIX_DELAY_TIME_R`,
  - `PARAM_MIX_DELAY_WIDTH`,
  - `PARAM_MIX_DELAY_FEEDBACK`,
  - `PARAM_MIX_DELAY_HPF`,
  - `PARAM_MIX_DELAY_LPF`,
  - `PARAM_MIX_DELAY_FBW`,
  - `PARAM_MIX_DELAY_MOD`,
  - `PARAM_MIX_DELAY_MOD_RATE`,
  - `PARAM_MIX_DELAY_REV`,
  - `PARAM_MIX_DELAY_VOL`.
- Les anciens IDs `PARAM_MIX_DELAY_SWING` et `PARAM_MIX_DELAY_ACCENT` restent reserves/tombstones pour conserver la numerotation `PARAM_COUNT`, mais ne sont plus des params produit et n'ont plus d'apply DSP.
- `TYPE=CLASSIC` reste le default et continue de router vers le moteur `fx_delay_stereo.*`.
- `TYPE=DUAL` route vers `fx_delay_dual.*`; `MODE` est interprete uniquement par ce backend.
- En CLASSIC, `PARAM_MIX_DELAY_PINGPONG` garde le controle visible `X`.
- En DUAL, la surface UI substitue `MODE` au slot de `X`; `PINGPONG` reste conserve pour compat CLASSIC.
- `TIME` et `TIME_R` persistent des divisions musicales sync BPM, pas des durees calculees.
- `apply_mix_delay_time()` et `apply_mix_delay_time_r()` recalculent les secondes depuis l'autorite tempo Z4.
- `FDBK` accepte la plage DUAL jusqu'a `1.20`; le backend CLASSIC conserve son clamp interne historique a `0.95`.
- `FBW`, `MOD` et `MOD_RATE` sont des globals DUAL; en CLASSIC ils restent masques/sans effet audio direct.

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

## 29. Contrat Master/FX MacroFX
- `track_tone_sound_state` porte un bloc `master_fx` par track: 4 slots, chacun avec `type`, `level`, `macro_a`, `macro_b`.
- Params ajoutes en fin d'enum pour limiter le risque de renumerotation: `PARAM_MASTER_FX1_TYPE/LVL/A/B` a `PARAM_MASTER_FX4_TYPE/LVL/A/B`.
- Ces params sont `TONE` track-aware, stockes et restaurables via les flux `PARAM_COUNT` existants.
- Z3 conserve uniquement l'autorite de stockage/apply param; l'execution DSP lit la base `track_tone_sound_state` en Z1 sans ajouter de seconde autorite param.
- `LVL` est interprete par le DSP comme profondeur/intensite du slot. Exception locale: `STUTTER LVL` reste on/off (`0` OFF, `>0` full wet), tandis que `FREEZE LVL` utilise le seuil `>0` pour engager le freeze et conserve la valeur `1..127` comme niveau progressif de retour wet/freeze et de duck dry; a `127`, le dry est coupe pour un comportement repeater dominant.
- Cote edition UI, `STUTTER LVL` reste stocke en brut `0..127` mais quantifie en deux etats: `0` pour OFF, `127` pour ON. `FREEZE LVL` reste edite en continu `0..127`: affichage `OFF` a zero, puis pourcentage au-dessus de zero. Les autres types Master/FX conservent l'edition continue de `LVL`.
- Pour `FREEZE`, `A=TIME` reste la division temporelle et `B=HOLD` reste un choix discret `SHORT/MID/LONG/INF`; le DSP consomme la valeur raw `B` quantifiee en 4 modes de feedback distincts, dont `INF` quasi maintenu mais borne.
- Types DSP actifs: `DRIVE`, `CRUSH`, `RING`, `CHOP`, `PUMP`, `COMB`, `WOBBLE`, `FREEZE`, `STUTTER`, `COLOR`.
- Liste FX exposee: `OFF`, `DRIVE`, `CRUSH`, `PUMP`, `CHOP`, `WOBBLE`, `COMB`, `RING`, `STUTTER`, `FREEZE`, `COLOR`.
- `TALK`, `PITCH` et `ECHO` sont retires sans tombstone produit; la plage `PARAM_MASTER_FXn_TYPE` est compacte `0..10`.
- `STUTTER` et `FREEZE` sont des types a ressource unique dans les 4 slots Master/FX: l'apply Z3 normalise une tentative de doublon vers `OFF` pour eviter un etat canonique mensonger apres restore/load. Les autres types restent slot-local.
- Pour `DRIVE`, le mapping visible est `A=DRIVE` et `B=TONE`; `LVL` reste la profondeur/wet du slot et ne pilote plus le pre-gain interne.
- `COLOR` reutilise les macros de slot existantes: `A=AMT` bipolaire sombre/brillant centre a 64, `B=FOCUS` continu large/aigu. Aucun nouveau parametre ni changement `PARAM_COUNT` n'est introduit.
- Risque documente: `PARAM_COUNT` augmente; les snapshots/projets binaires produits par cette passe changent de layout parametre.

## 30. Contrat Drum params finaux

- Le bloc Drum ne conserve que les huit `PARAM_DRUM_TRX_BD_*` utiles au slot futur `TRX BD` et au moteur actif `BD_ANALOG`.
- Les anciens params Drum retires ne sont plus presents dans `PARAM_COUNT`.
- Les anciens params `PARAM_TB3_*` sont retires de `PARAM_COUNT`; aucun tombstone ni compatibilite projet/config `TB3` n'est conserve.
- `PARAM_COUNT` est reduit par cette suppression; aucun maintien de compatibilite ancien projet Drum n'est requis.
- `BD_ANALOG` utilise la projection TONE runtime limitee a `Pitch`, `Decay`, `Tone` et `FM`.

## 31. Contrat BD_ANALOG TONE

- `BD_ANALOG` utilise quatre IDs du bloc `PARAM_DRUM_TRX_BD_*`:
  - `PARAM_DRUM_TRX_BD_PITCH` -> pitch/f0 Plaits,
  - `PARAM_DRUM_TRX_BD_DECAY` -> decay Plaits,
  - `PARAM_DRUM_TRX_BD_HARMONICS` -> tone Plaits,
  - `PARAM_DRUM_TRX_BD_PITCH_SWEEP` -> attack FM Plaits.
- Autorite:
  - `param_registry_apply_track_value` reste le point d'entree unique TONE,
  - la base canonique reste `track_tone_sound_state`,
  - `drum_synth` n'est que l'executant runtime et instancie `plaits::AnalogBassDrum` directement, sans `plaits::Voice`.
- Frontieres:
  - PLAY fournit `note_on`, note, velocity/accent et comportement de trigger,
  - COLORS reste le chemin commun filtre/EQ de track,
  - MIX reste niveau/pan/sends/mute,
  - VCA reste amplitude/enveloppe dynamique mixer pour les types qui l'exposent encore; `Sampler/Stream` et `Sampler/Looper` bloquent `PARAM_VCA_*` et neutralisent tout state VCA stale,
  - MOD atteint TONE et COLORS via `track_runtime_tone_param_to_slot()` et les chemins directs `mod_lfo_v1`; `drum_synth` est appele uniquement via ses setters runtime audites.

## 32. Contrat MIX page 1 p-lock / LFO

- Les IDs existants `PARAM_MIX_LEVEL`, `PARAM_MIX_PAN`, `PARAM_MIX_SEND1` et `PARAM_MIX_SEND2` restent les seules cibles MIX page 1 exposees au p-lock et au LFO.
- Autorite de base: `track_sound_state` par track; projection runtime autoritative via `param_registry_apply_track_value`, modulation temporaire via chemins directs `mod_lfo_v1` vers la lane mixer resolue par Z2.
- Stockage p-lock MIX: `seq_param_iface` garde un etat compact dedie a 4 slots reels, sans reserver la table 256 slots pour ce set.
- Execution LFO MIX simple: `mod_lfo_v1` applique directement `LEVEL`, `PAN`, `SEND1` et `SEND2` sur la target mixer resolue par Z2, sans passer par `param_registry_apply_track_value_rt_fast`.
- Ce chemin direct ne modifie pas `track_sound_state`, `param_store`, le cache runtime param ou l'etat UI: il reste une projection runtime modulee temporaire.
- La release LFO de ces quatre destinations reapplique la base capturee par le meme chemin direct, sauf si un autre LFO actif de la meme track cible deja la meme destination.
- Execution LFO directe etendue: `PARAM_FILTER_CUTOFF`, `PARAM_FILTER_RESONANCE`, `PARAM_FILTER_EG_AMT`, `PARAM_FILTER_ATTACK`, `PARAM_FILTER_DECAY`, `PARAM_FILTER_SUSTAIN`, `PARAM_FILTER_RELEASE` et `PARAM_VCA_ATTACK`, `PARAM_VCA_DECAY`, `PARAM_VCA_SUSTAIN`, `PARAM_VCA_RELEASE` sont appliques par `mod_lfo_v1` directement vers la target mixer runtime, avec les memes conversions UI->runtime que `param_filter`.
- Ces chemins directs ne mutent pas la base `track_sound_state`, le shadow FILTER, `param_store` ni le cache runtime param; la base capturee reste restauree sur release, sauf si l'autre LFO actif de la meme track cible encore la meme destination.
- Execution LFO Sampler/Stream/Multi/Looper directe: `PARAM_SAMPLER_GAIN`, `PARAM_SAMPLER_START`, `PARAM_SAMPLER_END`, `PARAM_SAMPLER_MODE`, `PARAM_SAMPLER_TUNE`, `PARAM_SAMPLER_SLICE_COUNT`, `PARAM_SAMPLER_CLIP_SOURCE_BPM`, `PARAM_SAMPLER_CLIP_SYNC_LENGTH`, `PARAM_SAMPLER_CLIP_PITCH` expose `Tune`, `PARAM_SAMPLER_CLIP_PLAY_MODE`, `PARAM_SAMPLER_CLIP_LOOP`, `PARAM_SAMPLER_CLIP_STRETCH_MODE`, `PARAM_SAMPLER_CLIP_GRAIN`, `PARAM_SAMPLER_CLIP_HOP`, `PARAM_SAMPLER_MULTI_LOOP` et `PARAM_LOOPER_XFADE` sont routes directement vers les setters runtime existants quand le type runtime courant les supporte. `PARAM_SAMPLER_LOOP_START` reste hors catalogue LFO: il est live par edit/p-lock, mais pas une destination LFO produit.
- `PARAM_SAMPLER_CLIP_SEARCH` reste stockable/reserve hors surface produit, mais n'est plus expose comme destination LFO valide.
- Les cibles de selection/chargement (`PARAM_SAMPLER_SAMPLE` / instrument Multi), triggers, commandes, Master FX no-op, Drum TRX reserve et MIDI program sont exclues du catalogue LFO; elles ne doivent pas introduire d'acces SD/FatFs/import/load dans `mod_lfo_v1`.
- Le chemin RT fast generique reste present uniquement comme fallback de securite pour future destination explicitement ajoutee.
- `PARAM_SAMPLER_TUNE` partage le meme setter runtime `brick6_sampler_runtime_set_tune()` pour les edits UI, p-locks, LFO et restore de base: la base canonique `track_tone_sound_state` n'est mutee que par les writes autoritatifs, tandis que les projections p-lock/LFO/restore recalculent le pas RAM actif sans animer le miroir UI.
- Pour `Sampler/RAM`, ce setter reprojette le pitch live sur les voix actives du track: chromatique = note + Tune, slice actif = note->slice uniquement et Tune->pitch global.

## 33. Contrat Sampler/Looper TONE skeleton

- `track_tone_sound_state` porte un bloc `looper` par track: `arm`, `len`, `play`.
- Params ajoutes en fin d'enum: `PARAM_LOOPER_ARM`, `PARAM_LOOPER_LEN`, `PARAM_LOOPER_PLAY`.
- Surface TONE visible pour `Sampler/Looper`:
  - `ARM`: `Off` / `Rec` / `Overd`,
  - `LEN`: `Free` / `1` / `2` / `4` / `8` / `16`,
  - `PLAY`: `Off` / `Auto`,
- Ces params sont stockes/restaurables via les flux `PARAM_COUNT`; `ARM=Rec` pilote le record simple existant cote Z5, `ARM=Overd` reste borne/no-op pour l'audio overdub non implemente, et `PLAY` est stocke sans lancer de playback Looper.
- `seq_param_iface` et `mod_lfo_v1` excluent ces params du p-lock/LFO: ce sont des commandes de workflow, pas des modulations audio continues.

## 34. Contrat Prism Phase Reset

- `PARAM_PRISM_PHASE_RESET` est un param TONE track-aware `Off/On`, default `Off`, stocke dans `track_tone_sound_state.prism.phase_reset`.
- L'apply Prism met a jour la base canonique puis projette l'option vers `brick6_braids_runtime_set_phase_reset(instance_id, enabled)`.
- `Off` conserve le comportement historique: aucun reset de phase force au note-on.
- `On` arme un reset phase one-shot au prochain `note_on`; l'execution audio passe par `sync_block[0]=1` sur le premier sous-bloc rendu.
- Aucun reset random ni reinit locale complexe du moteur Mutable n'est associe a ce param.
- `mod_lfo_v1` exclut `PARAM_PRISM_PHASE_RESET` des destinations LFO; ce param est une option de comportement de trigger, pas une modulation continue.
- `PARAM_COUNT` augmente; les snapshots/patterns/projets binaires produits par cette passe changent de layout parametre.

## 35. Contrat XFade Looper apres retrait buffer master

- Les anciens params buffer master sont retires de `PARAM_COUNT` sans tombstones.
- `PARAM_LOOPER_XFADE` remplace l'ancien alias de stockage et pilote uniquement l'etat neutre `audio_xfade` pour `Sampler/Looper`.
- `track_tone_sound_state.looper` porte `arm`, `len`, `play` et `xfade`; aucun bloc TONE buffer dedie ne reste.
- `PARAM_LOOPER_XFADE` est un param TONE track-aware Looper: visible sur `TONE/LOOP`, p-lockable via `SEQ_PLOCK_SET_TONE`, et assignable MACRO par le contrat `p-lockable => macro-assignable`.
- Les params Looper de workflow restent explicitement separes de ce contrat; `XFade` est le morph audio continu, pas une commande `ARM`/`PLAY`/`SAVE`.

## 36. Contrat Looper STRETCH UI/state

- `Sampler/Looper` ajoute trois params TONE stockes/projetes runtime: `PARAM_LOOPER_STRETCH`, `PARAM_LOOPER_PITCH`, `PARAM_LOOPER_GRAIN`.
- `STRETCH` expose `Off` / `Speed` / `Shifter`, defaut `Off`; `PITCH` expose `-12..+12 st`, defaut `0`; `GRAIN` reutilise les tailles Stream `384` / `512` / `768` / `1024` / `1536` / `2048`, defaut index `4`.
- `track_tone_sound_state.looper` porte maintenant `arm`, `len`, `play`, `xfade`, `stretch`, `pitch` et `grain`.
- L'apply Looper projette `stretch`, `pitch` et `grain` vers `brick6_looper_runtime_set_stretch()` depuis le backend param, hors IRQ audio; l'IRQ lit seulement l'etat runtime Looper deja projete.
- L'execution audio est en Z1: `Off` garde la lecture brute si `Pitch=0`, `Speed` applique le ratio tempo + pitch au read increment, et `Shifter` reutilise `brick6_clip_shifter` via un pool Looper dedie separe du pool Stream.
- L'apply de `PARAM_LOOPER_STRETCH` / `PARAM_LOOPER_PITCH` projette seulement l'etat runtime et peut armer un resync one-shot quand `Pitch` arrive sur un point stable `-12`, `0` ou `+12`; il ne repositionne jamais directement le playhead hors IRQ audio.
- Si la metadata de prise Looper est invalide, le runtime retombe sur `Off`; si le pool Shifter Looper est plein, il retombe sur `Speed`.
- `seq_param_iface` et `mod_lfo_v1` excluent `PARAM_LOOPER_STRETCH`, `PARAM_LOOPER_PITCH` et `PARAM_LOOPER_GRAIN` du p-lock/LFO: ces controles restent projetes par write param autoritatif, pas par modulation continue.
- `SRC BPM` et `SYNC LEN` restent des params Stream uniquement; le stretch Looper utilise la metadata de prise REC.

## 37. Contrat experimental LFO window-rate

- `mod_lfo_v1` possede maintenant un mode compile-time experimental `MOD_LFO_WINDOW_RATE_EXPERIMENT`.
- Quand ce mode vaut `1`, la cadence de modulation suit la fenetre audio recue par `mod_lfo_v1_process_block(frames)`, typiquement alignee sur `BRICK6_AUDIO_EVENT_GRID_FRAMES`.
- Le LFO calcule une seule valeur tenue par fenetre audio courante et avance la phase par `phase_inc_per_sample * frames`, afin que le cout d'un LFO rapide ne depende pas de sa frequence.
- Le mode experimental applique le tick immediatement sur chaque appel bloc; il ne passe pas par l'accumulateur legacy, ce qui evite de garder une valeur ancienne jusqu'a la prochaine fenetre complete et preserve les sous-fenetres eventuelles.
- Le tick est place avant le rendu des engines dans `brick6_audio_runtime_dsp`; les destinations moteur et mixer consomment donc la valeur de la fenetre courante, sans decalage volontaire d'un bloc.
- `MOD_LFO_WINDOW_RATE_EXPERIMENT=0` conserve le chemin legacy 3000 Hz / stride 16 frames.
- Cette passe ne change ni les destinations LFO, ni les params utilisateur, ni les formes d'onde, ni le routing runtime direct existant.

## 38. Contrat LFO RANDOM/S&H

- `MOD_LFO_SHAPE_RANDOM_SH` est un sample-and-hold bipolaire, pas un bruit aleatoire par tick.
- La valeur aleatoire est tenue entre deux wraps de phase LFO et regeneree au premier tick actif apres init/reset/changement de shape, puis a chaque wrap de phase.

## 39. Contrat LFO final

- Chaque track possede 3 LFO symetriques; la config canonique reste dans `track_sound_state.mod_lfo[MOD_LFO_COUNT_PER_TRACK]` et l'execution dans `mod_lfo_v1`.
- Surface par LFO: `RATE`, `SHAPE`, `TRIG`, `PHASE`; destination et profondeur appartiennent uniquement a `mod_matrix`.
- `RATE` est bipolaire: valeur negative = Hz libre continu `0..80.00Hz`, `0` = `OFF`, valeur positive `1..16` = table sync stable `8BAR, 4BAR, 2BAR, 1BAR, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, 1/128`.
- Le mode Hz convertit directement la frequence en `phase_inc`, sans dependance BPM; le mode sync convertit la division via le BPM courant. `OFF` coupe la source LFO.
- `SHAPE` expose `SIN`, `TRI`, `SAW`, `SQR`, `RND`, `SIN+`, `TRI+`, `SQR+`, `RSAW`. Les formes `+` sont unipolaires `0..1`; les autres restent bipolaires `-1..1`. La matrice applique ensuite `base + source * depth` puis clamp destination.
- `TRIG`: `FREE` tourne sans reset; `TRIG` reset la phase au note/trig sans servir de gate ON/OFF; `HOLD` capture la valeur LFO au trig et la tient; `ONE` joue un cycle a l'activation effective et a chaque trig puis stoppe/restaure la base.
- `PHASE` est en degres pour les shapes periodiques. Pour `RND`, le meme parametre reste la quantite de slew de la sortie sample-and-hold afin de conserver le controle utile sans ajouter un cinquieme parametre LFO.
- Les triggers LFO arrivent par `mod_lfo_v1_note_trigger(track)`, appele depuis les chemins note sequenceur et clavier apres resolution track, sans dependance UI fragile.
- En mode window-rate, si une fenetre traverse le wrap, la nouvelle valeur est appliquee a toute la fenetre courante, comme les autres formes tenues par fenetre; aucun ramp ni interpolation random n'est introduit.

## 40. Contrat REC CFG START/TEMPO

- `PARAM_CFG_START` remplace l'ancien ID REC dans le catalogue global REC CFG.
- `apply_cfg_start()` ecrit l'autorite Z4 via `seq_runtime_set_rec_start_mode()` puis relit `seq_runtime_get_rec_start_mode()` pour miroir UI/store.
- `PARAM_CFG_TEMPO` garde l'autorite tempo Z4 (`seq_runtime_set_tempo_bpm_milli`); l'edition REC CFG force `1.00 BPM` sans SHIFT et `0.01 BPM` avec SHIFT, sans nouvelle autorite tempo.

- `PARAM_CFG_METRO` est un parametre global REC CFG `0..127`: `0=OFF`, `1..127=ON+volume`.
- `apply_cfg_metro()` clamp explicitement la valeur, appelle `metronome_runtime_set_level_u7()` puis miroir `param_store`; le gain audio reel est calcule cote Z1 avec une courbe carree bornee.

## Addendum 2026-07-17 - lot 4B catalogue param low-cost

- Le catalogue CFG/MIX consomme les macros de variante issues de `ui_core.h`: en low-cost, seuls les choix de family disponibles peuvent atteindre `Input1`; `Input2..4` ne sont plus des choix valides via le catalogue UI.
- Les tombstones legacy `PARAM_MIX_TRACK0..3_ROUTE` restent dans `PARAM_COUNT` pour ne pas renumeroter le layout, mais leurs labels/bornes low-cost sont limites a `None/Master`. Les routes `Cue` et `Both` ne sont plus exposables dans cette variante.
- Premium conserve les labels et bornes `None/Master/Cue/Both` inchanges.

## 41. Contrat restore Kit V1

- L'apply Kit restaure les bases canoniques `track_sound_state` et `track_tone_sound_state`, puis reprojette uniquement les domaines track-aware `COLORS`, `TONE` et `MIX` par `param_registry_apply_track_value`.
- La config LFO/Matrix/ENV3 est portee par `track_sound_state_t` et reprojetee par les params track-aware courants; aucun payload LFO separe n'est restaure.
- Le domaine `PLAY` reste exclu de l'apply Kit pour ne pas restaurer seq/pattern/p-locks/transport.
## 42. Contrat dirty Kit

- Les writes sonores autoritatifs qui passent par `param_registry_apply_track_value` marquent le Kit actif dirty si un slot Kit actif existe: CFG family/type, FILTER/COLORS, TONE, MIX et LFO config.
- Le dirty Kit appartient a `kit_v1`, pas a Z3: Z3 emet seulement la notification post-apply apres succes. Les restores Kit suspendent ce marquage et nettoient le dirty apres apply/save.
- Les projections temporaires runtime, p-lock playback, transport, playhead, sequence et navigation UI ne doivent pas marquer le Kit dirty.

## 43. Contrat destination modulation commune

- `mod_destination_catalog` porte maintenant le catalogue commun des destinations de modulation continues: validation track-aware, cache index/param par track, labels longs/courts et application RT directe.
- `mod_lfo_v1` conserve la compatibilite de surface UI (`mod_lfo_v1_dest_*`) mais delegue au catalogue commun; il ne possede plus le cache destination ni le cache MIDI CC.
- Les chemins RT directs existants (MIX, FILTER, VCA, Sampler, Prism, Drum, MIDI CC, fallback `param_registry_apply_track_value_rt_fast`) sont conserves sans changement fonctionnel, mais leur autorite est preparee pour etre consommee par la matrice de modulation.

## 44. Contrat runtime Matrix et accumulation

- `mod_matrix` porte l'autorite runtime d'accumulation des modulations continues par track: 8 slots statiques, source explicite, destination catalogue commune, depth bipolaire et etat `enabled` distinct.
- `mod_matrix` maintient un cache runtime borne des routes configurees: flag global `any route`, flag par track et masque des sources par track. Ces caches sont reconstruits a l'init/reset et maintenus par les edits Matrix source/destination/depth afin que l'IRQ audio puisse court-circuiter le chemin quand aucune route n'existe.
- Les slots actifs sont regroupes par destination a chaque fenetre control-rate; la valeur appliquee est `base + somme(depth * source)`, puis clamp unique aux bornes du parametre.
- `mod_lfo_v1` ne route plus directement vers une destination: il produit les sources `LFO1`, `LFO2` et `LFO3` et orchestre la lecture des sources runtime `ENV3`, `ENV VCA` et `ENV FLT`; les anciens champs `DEST/DEPTH` ne sont plus une surface de compatibilite Matrix/ENV3.
- `ENV VCA` et `ENV FLT` exposent uniquement la derniere sortie normalisee `0..1` des enveloppes mixer existantes: VCA et filtre conservent leur role audio d'origine, aucune ADSR parallele n'est creee, et la Matrix utilise seulement la profondeur bipolaire pour inverser.
- Les sources liees aux enveloppes mixer sont invalidees par `mod_matrix` si la track courante ne supporte pas reellement le VCA gate ou le filtre; une route invalide apres changement de type relache la destination vers sa base au tick suivant.
- Le chemin audio Matrix derive famille/type depuis `track_runtime_ctx_t`; il ne relit pas l'etat UI pour exposer ou valider ces sources.
- Les bases modulees sont detenues par `mod_matrix` et resynchronisees via `mod_matrix_resync_base_on_authoritative_write`; les projections runtime ne mutent pas `track_sound_state`, `track_tone_sound_state`, `param_store` ni les caches autoritatifs.

## 45. Contrat ENV3 source libre

- `mod_env3` porte une enveloppe ADSR runtime par track, basee sur `env_adsr`, declenchee par les memes note-on valides que les LFO et relachee sur note-off/all-notes-off.
- ENV3 n'a aucune destination codee en dur: sa sortie normalisee `0..1` est exposee uniquement comme source `MOD_MATRIX_SOURCE_ENV3` pour `mod_matrix`.
- La config canonique ENV3 vit dans `track_sound_state.mod_env3`; elle est appliquee au runtime sans allocation, lock, acces fichier ou appel UI dans le chemin audio.
- La cadence reste celle de la fenetre de modulation existante; le cout depend du nombre de tracks et sources actives, pas du nombre total de parametres.

## 46. Contrat surface MOD/ENV Matrix

- La surface MOD expose `MATRIX`, `LFO 1`, `LFO 2`, `LFO 3`; les champs `DEST/DEPTH` ne sont plus edites dans les pages LFO et passent uniquement par `mod_matrix`.
- Les params `PARAM_MOD_MATRIX_SLOT/SOURCE/DEST/DEPTH` sont des params track-aware MOD qui adressent le slot selectionne par track. `SLOT` est un selecteur d'edition, pas un slot de modulation actif.
- Pour LINK de voice group, `PARAM_MOD_MATRIX_DEPTH` est la seule valeur Matrix compatible; la propagation conserve explicitement l'index de slot source. `SLOT`, `SOURCE` et `DEST` restent des selecteurs/structure et ne sont pas propages.
- Les anciens ids `PARAM_LFO1_DEST/DEPTH` et `PARAM_LFO2_DEST/DEPTH` sont retires du layout courant; aucun tombstone n'est conserve pour Matrix/ENV3.
- Les params `PARAM_ENV3_ATTACK/DECAY/SUSTAIN/RELEASE` ecrivent la config canonique `track_sound_state.mod_env3`; en p-lock playback, `mod_env3` applique une copie runtime temporaire puis revient a la base courante sans modifier la valeur sauvegardee/affichee.
- Les params Matrix a adressage par slot selectionne restent exclus des p-locks MOD tant qu'il n'existe pas d'IDs slot-addressed stables; cela evite de rendre une automation dependante du slot actuellement affiche.

## Addendum 2026-07-25 - parametres TONE Stack

- Stack possede ses propres IDs TONE: niveaux OSC1..3, bruit, puis MODEL/TUNE/TIMBRE/COLOR par slot. Ils sont stockes dans `track_tone_sound_state.stack` et ne reutilisent aucun champ Prism.
- Les writes hors IRQ passent par `param_backend_apply_tone_stack()` puis par la file `brick6_stack_runtime_submit_*`; le controle hors IRQ n'ecrit pas directement dans l'instance runtime Stack. Les projections runtime temporaires de p-lock utilisent les setters audio directs sans mutation de la base canonique.
- Les p-locks TONE utilisent la table `track_runtime_tone_slots_stack[]`; `MODEL` est donc p-lockable comme parametre stepped Stack.
- Le catalogue commun de modulation expose uniquement les destinations Stack continues: niveaux, bruit, TUNE, TIMBRE et COLOR. Les params `PARAM_STACK_OSC*_MODEL` sont exclus des destinations LFO/Matrix continues.
- Prism conserve son catalogue de params, son apply backend et ses destinations historiques sans rerouting vers Stack.
- Le cache runtime param conserve les valeurs en `CTRL_STATE`; son masque de validite vit en `SEQ_STATE_D2` pour garder `RAM_D3` bornee quand le catalogue de params evolue.

## Addendum 2026-07-25 - simplification analogique Stack

- Le catalogue utilisateur Stack remplace les huit modeles analogiques simples par `SINFD`, `TRIFD` et `SHAPE`; l'ancien modele prototype `SOFT` n'est plus expose.
- `PARAM_STACK_OSC*_MODEL` est maintenant borne a `0..10`; aucune compatibilite de conversion des anciennes valeurs prototype n'est conservee.
- Les params continus `TIMBRE`, `COLOR` et `PARAM3` restent les surfaces modulees/p-lockables des modeles fold: `SINFD/TRIFD FOLD/SYM/SHAPE`; `SHAPE` garde `SHAPE/MORPH`.

## Addendum 2026-07-25 - params page VOICE Stack

- Stack conserve deux params TONE globaux pour la page `VOICE`: `PARAM_STACK_OSC_DETUNE` et `PARAM_STACK_PHASE_RESET`.
- `PARAM_STACK_OSC_DETUNE` est stocke dans `track_tone_sound_state.stack.osc_detune`, p-lockable via le set TONE, mais exclu des destinations continues Matrix/LFO car ses offsets sont regeneres uniquement au note-on et restent fixes pendant la note.
- `PARAM_STACK_PHASE_RESET` stocke `FREE`/`RESET`, reste exclu des destinations continues et conserve `FREE` par defaut.
- En `FREE`, les phases Stack restent free-running pendant le silence avec les increments propres de chaque slot; en `RESET`, le note-on garde le reset deterministe local au runtime Stack.

## Addendum 2026-07-25 - TRACK CFG group SPREAD/LINK

- `PARAM_CFG_GROUP_SPREAD`, `PARAM_CFG_GROUP_LINK` et `PARAM_CFG_GROUP_SEQ_LINK` sont des commandes CFG speciales resolues par `param_registry` vers la master effective du voice group; elles ne passent pas par les domaines TONE/COLORS/MIX/PLAY.
- SPREAD reutilise `PARAM_MIX_PAN` comme point d'application: pour `n=2..8`, la position du membre `i` est `spread * (((i / (n - 1)) * 2) - 1)`, donc centree autour de zero, avec `i=0` master puis slaves dans l'ordre logique.
- LINK est intercepte en un seul point UI, apres l'edition manuelle de base et avant toute propagation secondaire: le delta propage est `valeur_apres_source - valeur_avant_source`, donc le delta reellement accepte apres clamp.
- LINK inclut les edits manuels de base sur `PARAM_CFG_TRACK`, `PARAM_CFG_TRACK_TYPE`, et sur domaines compatibles `TONE`, `COLORS`, `MIX` et `MOD` pour params continus/int non enum/bool. Il exclut strictement `PLAY`, p-locks, live-rec p-locks, scheduler, modulation, automation, commandes, navigation, `SPREAD`, `LINK` et `SEQ LINK`.
- La recursion est bloquee par un garde local de propagation; une ecriture propagee ne redevient jamais source LINK.

## Addendum 2026-07-26 - resolution fine TONE Stack

- Les `PARAM_STACK_OSC*_TUNE` stockent maintenant la valeur en centiemes de demi-ton via un `step=0.01`; le pas coarse `1.0` est une policy UI Z5, pas la resolution catalogue.
- Le runtime Stack consomme ce pitch comme cents par slot oscillator et ne quantize plus la base Stack au demi-ton; les p-locks TONE encodent donc les valeurs fines exactes selon le step catalogue.
- Prism/Braids garde le stockage interne `PARAM_PRISM_FINE` et `PARAM_PRISM_COARSE`; Z5 expose cependant un controle Prism `TUNE` unifie sur `PARAM_PRISM_COARSE`.
- Pour ce controle UI, la valeur visible combine `COARSE + FINE`, l'edition directe remet `FINE` au neutre `0.5`, et l'encodage p-lock de `PARAM_PRISM_COARSE` conserve la resolution fine du controle unifie.

## Addendum 2026-07-26 - PARAM3 Stack et modeles Fold

- Les slots Stack gardent `MODEL` et `TUNE`, mais les pages OSC exposent maintenant les trois params modele `TIMBRE`, `COLOR` et `PARAM3`; `TUNE` est deplace en page dediee Z5.
- `track_tone_sound_state.stack` porte `param3[3]`; `PARAM_STACK_OSC*_PARAM3` est ajoute au layout `PARAM_COUNT`, reapply via `brick6_stack_runtime_submit_slot_param3()` et destination continue Matrix/LFO comme `TIMBRE/COLOR`.
- `PARAM_STACK_OSC*_MODEL` est borne a `0..10`: `SOFT` n'est plus actif, l'ancien index 0 devient `SINFD`, et `TRIFD` est ajoute comme second moteur fold dedie.
- Pour `SINFD` et `TRIFD`, les labels modele sont `FOLD` / `SYM` / `SHAPE`; les autres modeles ne consomment pas `PARAM3` dans le runtime audio.

## Addendum 2026-07-26 - LINK choix structurels et filtre

- LINK voice group propage maintenant les choix discrets `PARAM_FILTER_TYPE`, `PARAM_CFG_TRACK` et `PARAM_CFG_TRACK_TYPE` par valeur absolue source apres clamp, pas par delta relatif.
- Les autres params LINK existants restent sur leur propagation relative bornee; les exclusions `PLAY`, p-locks, scheduler, automation, `SPREAD`/`LINK`/`SEQ LINK` et selecteurs Matrix restent inchangees.

## Addendum 2026-07-27 - identite Synth/Wave avant TONE

- `Synth/Wave` possede une identite runtime separee. Les `PARAM_WAVE_*` ont ete ajoutes ensuite par l'addendum TONE Wave ci-dessus.
- Avant cette passe TONE, aucun slot TONE, apply backend, p-lock TONE ou destination Matrix Wave n'etait branche.
- Les destinations Prism existantes restent attachees a `TRACK_RUNTIME_ENGINE_PRISM` et ne s'appliquent pas a `TRACK_RUNTIME_ENGINE_WAVE`.

## Addendum 2026-07-28 - PARAM CFG GROUP SPREAD KEYTRK

- `PARAM_CFG_GROUP_SPREAD_KEYTRK` ajoute le toggle `KEYTRK` de `CFG/GROUP`, resolu par `param_registry` vers la master effective du voice group comme `SPREAD` et `LINK`.
- `KEYTRK=OFF` garde le comportement `SPREAD` existant: pan MIX des membres `spread * (((i / (n - 1)) * 2) - 1)`.
- `KEYTRK=ON` neutralise le pan MIX seulement pour les membres `Sampler/Multi` et laisse `brick6_sampler_runtime` appliquer un pan par voix Multi.
- Courbe keytrack: facteur lineaire borne `0.5 + note/127 * 0.75`, soit `0.5..1.25`, multiplie par le pan de spread puis clamp `-1..1`.
- `LINK` reste exclu de `SPREAD` et `KEYTRK`; aucune propagation LINK secondaire, p-lock ou modulation n'est ajoutee.

## Addendum 2026-07-28 - PARAM CFG GROUP SEQ LINK

- `PARAM_CFG_GROUP_SEQ_LINK` ajoute le toggle utilisateur `SEQ LINK` de `CFG/GROUP`.
- L'apply est centralise dans `param_registry`: la track active est resolue vers la master effective du voice group, puis le flag est commite via `param_registry_commit_voice_group_seq_link()`.
- La lecture valeur passe par la projection master-effective `track_runtime_get_voice_group_seq_link()` afin qu'une track hors groupe operationnel reste affichee `OFF`.

## Addendum 2026-07-28 - contrat commit SEQ LINK

- `SEQ LINK` se modifie maintenant par `param_registry_commit_voice_group_seq_link()` ou `param_registry_commit_voice_group_seq_link_bulk()`, quelle que soit l'origine: UI, Pattern/Project, Kit, Patch Poly ou snapshot track.
- Ces APIs sont le point unique qui ecrit le stockage brut Z2 puis emet la notification post-commit Z4 `seq_runtime_on_seq_link_changed()` quand la valeur brute change.
- Les mutations brutes `track_state_*_seq_link_raw()` sont reservees a ce contrat interne et ne constituent pas une surface d'appel produit.
- `SEQ LINK` reste une commande de structure CFG: pas de p-lock, pas de modulation, pas de live-rec, pas de propagation par `CFG GROUP LINK`.

## Addendum 2026-07-28 - reset runtime cache par Track snapshot

- `param_registry_clear_track_runtime_state(track)` neutralise les overlays temporaires par parametre puis vide le cache runtime track-scoped.
- Ce chemin est consomme par `track_snapshot` avant restore/clear d'une track afin d'eviter qu'un parametre ancien non reapplique reste lisible par fallback cache.
- Les bases canoniques restent `track_sound_state`, `track_tone_sound_state`, FILTER/LFO/Matrix/PLAY; le cache runtime n'est pas une source de persistence.
