# New DSP Engine Manual

## 1. Scope

Ce manuel documente le corridor réel d’intégration d’un nouveau moteur DSP/synth dans l’architecture actuelle du projet.

Périmètre couvert :
- identité produit `family/type`
- binding runtime track-aware
- modèle canonique des paramètres moteur
- apply runtime des paramètres
- rendu audio hard-RT
- intégration sequencer / notes / p-locks
- exposition UI
- persistence pattern / project
- boot / init

Hors périmètre :
- aucune implémentation de moteur spécifique
- aucun patch code source
- aucune nouvelle architecture parallèle

But :
- fournir une cartographie durable pour les futurs moteurs DSP
- expliciter les autorités existantes
- lister les seams exacts à réutiliser avant tout patch
- éviter de refaire l’audit architectural à chaque nouvelle intégration moteur

## 2. Current DSP integration map

Le chemin observé dans le code est structuré en couches, même s’il reste distribué entre plusieurs zones.

1. Identité produit et configuration track :
- `Inc/UI/ui_core.h`
- `Src/UI/ui_track_catalog.c`
- `Src/Core/track_state.c`

2. Projection runtime track-aware :
- `Inc/Core/track_runtime.h`
- `Src/Core/track_runtime.c`

3. État canonique paramétrique :
- `Src/Core/track_sound_state.c`
- `Src/Core/track_tone_sound_state.c`
- `Src/Param/param_registry_catalog.c`
- `Inc/Param/param_store.h`

4. Apply runtime des paramètres :
- `Src/Param/param_registry.c`
- `Src/Param/param_registry_tone_backends.c`
- `Src/Param/param_registry_backends.c`

5. Exécution audio :
- `Src/Audio/audio.c`
- `Src/Audio/audio_float.c`
- `Src/Core/brick6_audio_runtime.c`
- `Src/Audio/mixer.c`
- moteurs actuels :
  - `Src/Core/brick6_sampler_runtime.c`
  - `Src/Audio/drum_synth.cpp`

6. Sequencer / notes / p-locks :
- `Src/Seq/seq_runtime.c`
- `Src/Seq/seq_play_scheduler.c`
- `Src/Seq/seq_param_iface.c`
- `Src/Keyboard/keyboard_engine.c`

7. UI :
- `Src/UI/ui_navigation.c`
- `Src/UI/ui_template_page.c`
- `Src/UI/pages/ui_page_template_tone.c`
- `Src/UI/ui_param.c`

8. Persistence :
- `Inc/Storage/pattern_live_ram.h`
- `Src/Storage/pattern_live_ram.c`
- `Inc/Storage/project_v1.h`
- `Src/Storage/project_v1.c`
- `Src/Storage/pattern_sd_bank.c`
- `Src/Storage/project_sd_bank.c`

9. Boot / init :
- `Src/Core/brick6_app_init.c`

Lecture systémique actuelle :
- `track_state` porte l’identité structurelle par track.
- `track_runtime` projette cette identité vers `engine / instance_id / mix_track / flags / ui_ensemble_mask`.
- `param_registry` est le seam d’écriture track-aware.
- les backends Z3 projettent la valeur canonique vers le runtime moteur.
- `brick6_audio_runtime_dsp()` rend les moteurs runtime et les injecte dans le mixer.
- `seq_play_scheduler_audio_apply_event()` et `keyboard_engine` déclenchent notes/gates sur les moteurs.
- `pattern_live_capture_current()` et `pattern_live_apply_snapshot()` persistent/restaurent les valeurs track-aware via `PARAM_COUNT`.

## 3. Track identity and runtime binding

### 3.1 Autorités observées

Autorité structurelle par track :
- `Src/Core/track_state.c`

API clefs :
- `track_state_set_track_family()`
- `track_state_set_track_type()`
- `track_state_get_config()`

Catalogue produit des families/types :
- `Src/UI/ui_track_catalog.c`

Exemples de familles/types actuels :
- `Synth -> Wave`
- `Sampler -> RAM / Stream / Looper`
- `Drum -> TRX BD reserve / BD Analog`
- `Master -> FX`
- `MIDI -> MIDI`

Projection runtime :
- `Src/Core/track_runtime.c`

Fonctions clefs :
- `track_runtime_family_from_ui()`
- `track_runtime_type_from_ui()`
- `track_runtime_compute_flags()`
- `track_runtime_compute_ui_ensemble_mask()`
- `track_runtime_bind_ctx()`
- `track_runtime_refresh_all()`
- `track_runtime_resolve_track()`

### 3.2 Mapping actuel

Le binding runtime se fait en deux temps :

1. `track_runtime_refresh_all()` lit `track_state_get_config(track)` et convertit `ui_family/ui_type` vers `track_runtime_family/type`.

2. `track_runtime_bind_ctx()` choisit ensuite :
- `engine`
- `instance_id`
- `bind_state`
- `bind_reason`

Cas observés :
- `Input` -> `TRACK_RUNTIME_ENGINE_AUDIO_TRACK`
- `MIDI` -> `TRACK_RUNTIME_ENGINE_NONE` mais track valide et `CAN_PLAY`
- `Drum` -> `TRACK_RUNTIME_ENGINE_DRUM`, `instance_id = track_id`
- `Synth/Sampler` -> `TRACK_RUNTIME_ENGINE_SAMPLER`

Le `mix_track_id` est résolu séparément :
- input physiques réservés explicitement via `track_runtime_input_family_mix_track()`
- autres tracks routables allouées via `track_runtime_mix_try_reserve_exact()` puis `track_runtime_mix_reserve_track()`

### 3.3 Ce qu’un nouveau moteur doit ajouter

Pour un nouveau moteur DSP/synth, les seams probables sont :
- `Inc/UI/ui_core.h` : ajouter un `UI_TRACK_TYPE_*`
- `Src/UI/ui_track_catalog.c` : ajouter le type à la family cible
- `Inc/Core/track_runtime.h` : ajouter `TRACK_RUNTIME_TYPE_*` et probablement `TRACK_RUNTIME_ENGINE_*`
- `Src/Core/track_runtime.c` :
  - `track_runtime_type_from_ui()`
  - `track_runtime_compute_flags()`
  - `track_runtime_compute_ui_ensemble_mask()`
  - `track_runtime_bind_ctx()`
  - potentiellement `track_runtime_get_voice_mode()`
  - potentiellement `track_runtime_get_play_voice_count_from_descriptor()`

### 3.4 Choix family/type

Un nouveau moteur doit d’abord être classé comme type produit.

Questions à trancher avant patch :
- le moteur appartient-il à une family existante ?
- doit-il être un nouveau `UI_TRACK_TYPE_*` ?
- partage-t-il une capacité existante, comme `Synth`, `Drum`, `Input`, `Master`, ou nécessite-t-il une nouvelle famille produit ?
- son identité est-elle une identité de track, ou seulement une variante interne d’un moteur déjà existant ?

Règle générale :
- préférer un nouveau `type` dans une family existante si le modèle produit reste compatible ;
- créer une nouvelle family uniquement si les règles produit, UI, runtime ou persistence sont réellement différentes ;
- ne pas traiter un moteur comme un mode global si son comportement dépend d’une track logique.

## 4. Canonical parameter model

### 4.1 Autorités observées

État canonique commun par track :
- `Src/Core/track_sound_state.c`

Domaines observés :
- MIX
- FILTER
- VCA
- MOD
- HYBRID gate

État canonique TONE spécifique moteur :
- `Inc/Core/track_tone_sound_state.h`
- `Src/Core/track_tone_sound_state.c`

Catalogue des paramètres :
- `Inc/Param/param_store.h` pour l’enum `param_id_t`
- `Src/Param/param_registry_catalog.c` pour :
  - nom/label
  - type d’affichage
  - min/max
  - step
  - default
  - callback wrapper

Lecture canonique track-aware :
- `param_registry_get_track_value()`

### 4.2 Représentation actuelle des moteurs

Sampler :
- bloc dédié dans `track_tone_sound_state_t`
- params observés :
  - `PARAM_SAMPLER_SAMPLE`
  - `GAIN`
  - `START`
  - `END`
  - `MODE`
  - `TUNE`
  - `FADE_IN`
  - `FADE_OUT`
  - `SLICE_COUNT`

Drum :
- blocs dédiés par sous-modèle dans `track_tone_sound_state_t`
- chaque moteur Drum a son sous-ensemble TONE propre

MIDI / Hybrid :
- params TONE `PROGRAM` et `CC`
- stockés eux aussi dans `track_tone_sound_state_t`

### 4.3 Defaults, bornes, labels, UI, p-locks, modulation

Defaults :
- `track_tone_sound_state_set_defaults()` lit `param_registry[param].default_value`

Bornes / labels / types :
- `param_registry_catalog.c`

Exposition UI :
- `ui_page_template_tone.c` choisit les banques/subpages et les `param_id_t`

P-locks :
- le modèle Seq stocke `set_id + param_slot + value16` ; `param_slot` est un slot local, jamais un `param_id_t` casté/tronqué.
- `seq_param_iface_param_to_slot(track,set,param,&slot)` encode un `param_id_t` vers le slot à stocker.
- `seq_param_iface_slot_to_param(track,set,slot,&param)` décode un slot stocké vers le `param_id_t` canonique à appliquer.
- `seq_param_iface_apply_lock()` et `seq_param_iface_restore_base()` consomment ces slots et repassent par `param_registry_apply_track_value()` pour les domaines non-PLAY.

Contrat des mappings :
- sets génériques (`PLAY`, `MOD`, `ENV`) : mapping stable par set.
- set `TONE` : mapping dépendant du `track_runtime_type` effectif de la track.
- changement moteur : les p-locks `TONE` sont conservés par slot, sans migration implicite de sémantique.
- slot non résoluble ou contexte runtime non bound : skip propre.

`seq_param_iface_map_param()` est un helper legacy/interne/non track-aware. Ne pas l’utiliser pour encoder des p-locks depuis UI, live-rec, scheduler, feedback, base commit, LFO, ni pour des chemins `TONE` ou runtime-specific.

Un paramètre track-aware devient p-lockable si `seq_param_iface_param_to_slot()` peut le résoudre pour la track/le set, et si le statut runtime effectif l’autorise.

Exception observée :
- `PARAM_SAMPLER_SLICE_COUNT` est explicitement exclu du mapping p-lockable.

Modulation :
- `mod_lfo_v1` utilise `param_registry_apply_track_value_rt_fast()`
- un paramètre n’est modulable que s’il passe les règles Z2/Z3 et s’il est accepté par le chemin RT fast

### 4.4 Ce qu’un nouveau moteur doit ajouter

Seams probables pour un nouveau moteur :
- `Inc/Param/param_store.h` : nouveaux `PARAM_ENGINE_*`
- `Src/Param/param_registry_catalog.c` : descriptors complets
- `Inc/Core/track_tone_sound_state.h` : nouveau bloc canonique moteur
- `Src/Core/track_tone_sound_state.c` : defaults associés
- `Src/Core/track_runtime.c` : table/règle `TONE slot -> param` et inverse pour le type runtime effectif
- `Src/UI/pages/ui_page_template_tone.c` : pages/subpages TONE dans le même ordre que l’exposition runtime TONE
- `Src/Seq/seq_param_iface.c` : seulement pour exclusions/règles du seam, pas pour recréer un mapping global TONE

Point de vigilance persistence :
- `persistent_control_model` décrit le modèle CONTROL canonique
- `persistent_control_codec` encode chaque champ explicitement, sans dépendre du layout RAM
- l'ajout d'une clé persistante exige la mise à jour du catalogue et du schéma; la version du codec ne change que si le contrat disque change

## 5. Runtime parameter apply path

### 5.1 Chemin observé

Depuis l’UI :
- `ui_param_handle_encoder()`
- `ui_param_set_active_track_value()`
- `param_registry_apply_track_edit()`
- `param_registry_apply_track_value()`

Depuis les p-locks :
- `seq_param_iface_apply_lock()`
- `param_registry_apply_track_value()`

Depuis restauration pattern/project :
- `pattern_live_transition_reapply()`
- `param_registry_apply_track_value()`

Depuis modulation :
- `param_registry_apply_track_value_rt_fast()`

### 5.2 Rôle de `param_registry_apply_track_value`

`Src/Param/param_registry.c` est le seam d’écriture track-aware principal.

La fonction :
1. fait `track_runtime_refresh_track(track)`
2. clamp la valeur selon `param_registry[id]`
3. route :
   - `LFO config` -> `mod_lfo_v1_set_track_param()`
   - `FILTER` -> `param_apply_filter_track_value()`
   - reste -> `param_apply_non_filter_track_value()`

Le backend moteur passe ensuite par :
- `param_backend_apply_track_value()` dans `param_registry_tone_backends.c`

### 5.3 Rôle du backend moteur

`param_backend_apply_track_value()` :
- lit `track_runtime_get_param_rule(id)`
- vérifie le `ctx` runtime
- route selon domaine/ressource/engine :
  - mix
  - buffer
  - sampler
  - drum
  - midi tone

Sampler :
- `param_backend_apply_tone_sampler()` dans `param_registry_backends.c`
- met à jour `track_tone_sound_state`
- pousse la projection runtime vers `brick6_sampler_runtime_set_*()`

Drum :
- `param_backend_apply_tone_drum()`
- met à jour `track_tone_sound_state`
- pousse la projection runtime via `drum_synth_set_param_for_instance()`

### 5.4 Ce qu’un moteur futur doit faire

Pour un nouveau moteur, le chemin attendu est :
- nouveaux `PARAM_ENGINE_*`
- nouvelles règles dans `track_runtime_get_param_rule()`
- extension du backend Z3 pour :
  - stocker dans `track_tone_sound_state`
  - pousser la projection runtime vers `engine_runtime_set_*()` ou équivalent

Règles à respecter :
- aucun write UI direct vers le moteur
- aucune seconde autorité cachée dans la page UI
- aucun bypass du `param_registry`
- les valeurs canoniques restent distinctes de la projection runtime

Seams probables :
- `Src/Core/track_runtime.c`
- `Src/Param/param_registry_backends.c`
- `Src/Param/param_registry_tone_backends.c`
- éventuellement `Src/Param/param_registry_apply_wrappers.c` si des wrappers catalogue sont nécessaires

## 6. Audio render path

### 6.1 Pipeline réel

Le pipeline observé est :

1. IRQ DMA :
- `Src/Audio/audio.c`
- `process_half()`

2. Collecte des events seq du bloc :
- `seq_runtime_audio_collect_block_events()`

3. Rendu des sous-segments :
- `audio_process_block_int32()` dans `Src/Audio/audio_float.c`

4. Frontière float :
- `audio_io_unpack()`
- `dsp_engine_process_block()`
- callback installée par `audio_set_float_callback(brick6_audio_runtime_dsp)`

5. Runtime DSP applicatif :
- `brick6_audio_runtime_dsp()` dans `Src/Core/brick6_audio_runtime.c`

6. Mixage :
- `mixer_process()` dans `Src/Audio/mixer.c`

### 6.2 Rendu actuel des moteurs

Drum :
- `brick6_render_synth_tracks()`
- `drum_synth_process_block_for_instance()`
- `mixer_submit_external_mono_native()`

Sampler :
- `brick6_render_sampler_tracks()`
- `brick6_sampler_runtime_render_track()`
- `mixer_begin_external_mono_native()` / `mixer_commit_external_mono_native()`

### 6.3 Point d’insertion probable d’un nouveau moteur

Le seam principal est :
- `brick6_audio_runtime_dsp()`

Deux options cohérentes avec l’existant :

1. Moteur externe injecté dans le mixer, comme Drum/Sampler :
- ajouter un helper `brick6_render_engine_tracks()`
- boucler sur les tracks runtime bindées au nouveau `TRACK_RUNTIME_ENGINE_*`
- rendre dans un buffer mono/stéréo temporaire
- injecter via `mixer_submit_external_mono_native()` ou l'API stéréo si nécessaire

En l’état du repo, l’analogie la plus directe pour un nouveau moteur dédié est le modèle Drum/Sampler :
- runtime moteur dédié
- rendu dans `brick6_audio_runtime_dsp()`
- injection dans le mixer

### 6.4 Contraintes hard-RT à respecter

Dans le chemin IRQ :
- pas de malloc
- pas d’I/O bloquante
- pas de scan dynamique lourd
- coût borné par bloc
- buffers statiques ou stack bornée uniquement

Points de vigilance :
- `process_half()` peut être segmenté par densité d’événements seq
- le rendu moteur doit rester stable même avec plusieurs appels segmentés dans un même half-buffer
- si le moteur a un coût variable par mode, il faut borner explicitement le worst-case avant merge

## 7. Sequencer, notes and p-locks

### 7.1 Comment les événements seq atteignent les moteurs

Génération :
- `seq_play_scheduler_schedule_step()`

Projection bloc :
- `seq_play_scheduler_audio_collect_block_events()`

Application bloc :
- `seq_play_scheduler_audio_apply_event()`

Dispatch moteur :
- `seq_play_scheduler_emit_engine_note()`

Cas observés :
- Drum -> `drum_synth_note_on/off_for_instance()`
- Sampler -> `brick6_sampler_runtime_trigger_note()` / `brick6_sampler_runtime_stop()`
- gates mixer/filter via `mixer_track_filter_note_*()` et `mixer_track_vca_note_*()`

### 7.2 Clavier live

Chemin parallèle mais aligné :
- `Src/Keyboard/keyboard_engine.c`
- `keyboard_engine_note_on_internal()`
- `keyboard_engine_dispatch_note_to_matching_tracks()`

Le clavier réutilise aussi `track_runtime` pour :
- routing engine
- target filter
- mix target
- support VCA gate

### 7.3 P-locks

Format modèle :
- `seq_model` / persistence stockent `set_id + param_slot + value16`.
- le format Seq/persistence ne stocke pas de `param_id_t` dans le champ slot.
- la refonte slot/param ne change pas le format disque ; elle clarifie seulement la sémantique du champ `param_slot`.

Encodage depuis un paramètre :
- l’appelant détermine le `set_id` depuis le domaine runtime du paramètre.
- il appelle `seq_param_iface_param_to_slot(track,set,param,&slot)`.
- il écrit ensuite `set_id + slot + value16`.
- aucun chemin externe ne doit faire `param_slot = (uint8_t)param_id`.

Décodage / apply runtime :
- `seq_boundary_engine` lit les slots stockés.
- `seq_param_iface_slot_to_param(track,set,slot,&param)` résout le `param_id_t` canonique.
- si `set=PLAY`, la base Seq est utilisée.
- sinon, `seq_param_iface_apply_lock()` appelle `param_registry_apply_track_value(param, track, decoded)`.
- `seq_param_iface_restore_base()` restaure par le même seam d’apply.

Spécificité `TONE` :
- le mapping slot/param dépend du `track_runtime_type` effectif.
- un changement moteur conserve les locks par slot, sans migration implicite vers les anciens `param_id_t`.
- un slot non résoluble pour le type courant est ignoré proprement.

Conclusion :
- un nouveau moteur n’a pas besoin d’un système p-lock spécial si ses paramètres sont de vrais `param_id_t` track-aware et si son type runtime expose un mapping `TONE slot -> param` symétrique.

### 7.4 Ce qu’un moteur mélodique doit vérifier

Seams probables :
- `Src/Seq/seq_play_scheduler.c` : ajout du dispatch note vers le runtime moteur, sans utiliser `seq_param_iface_map_param()` pour résoudre des locks
- `Src/Keyboard/keyboard_engine.c` : même dispatch pour jeu clavier direct et MIDI externe
- `Src/Seq/seq_param_iface.c` : filtrer les params p-lockables/non p-lockables et utiliser `slot_to_param` / `param_to_slot` comme seam public
- `Src/Core/track_runtime.c` : mapping TONE local au type runtime, voice mode / play voice count si la polyphonie diffère du sampler/drum

Question ouverte :
- le code actuel expose `TRACK_RUNTIME_VOICE_MODE_POLY` mais aucun moteur in-tree ne l’utilise vraiment.
- si un nouveau moteur requiert une vraie polyphonie runtime, `track_runtime_get_voice_mode()` et les consommateurs associés doivent être revérifiés.

## 8. UI exposure

### 8.1 Gating des ensembles UI

Autorité runtime :
- `track_runtime_compute_ui_ensemble_mask()`

Exposition :
- `track_runtime_is_ui_ensemble_available()`

Consommation UI :
- `ui_navigation_is_page_available()` dans `Src/UI/ui_navigation.c`

Le gating se fait donc via Z2, pas via une page qui décide seule.

### 8.2 Résolution TONE

Infrastructure générique :
- `Src/UI/ui_template_page.c`

Page TONE :
- `Src/UI/pages/ui_page_template_tone.c`

La page TONE choisit une `ui_template_family_t` selon `family/type` de la track active.

Cas observés :
- Buffer -> pages `REC/FADE`
- Sampler -> `PLAY/FX/SLICE`
- MIDI -> `PROG/CC1/CC2/CC3`
- Hybrid -> `PROG/CC1/CC2/CC3`
- Drum -> pages dynamiques selon le type

### 8.3 Ce qu’un futur moteur doit faire

Pour un nouveau moteur, l’exposition attendue est :
- rester dans l’ensemble UI pertinent, généralement `TONE` pour un moteur sonore
- ajouter une famille template spécifique au type moteur si nécessaire
- ou étendre le resolver TONE existant si la family produit reste inchangée

Seams probables :
- `Src/UI/pages/ui_page_template_tone.c`
- éventuellement `Src/UI/ui_renderer_template.c` si un widget spécial est nécessaire
- `Src/UI/ui_param.c` si certains paramètres ont un traitement spécial

Règles structurantes :
- ne pas créer de mode global séparé
- ne pas contourner `track_runtime_is_ui_ensemble_available()`
- ne pas dupliquer une logique de contexte en dehors du système template existant
- l’UI édite le modèle canonique via les commandes existantes, elle ne devient pas propriétaire du runtime DSP
- l’ordre UI des paramètres TONE doit rester aligné avec l’ordre `TONE slot -> param` exposé par le type runtime effectif
- les chemins UI p-lock/feedback/base commit doivent utiliser `seq_param_iface_param_to_slot(track,set,param,&slot)` et jamais `seq_param_iface_map_param()`

## 9. Persistence

### 9.1 Pattern

Capture :
- `pattern_live_capture_current()`
- boucle sur tous les `param_id_t` via `PARAM_COUNT`
- lit `param_registry_get_track_value(id, track, &value)`

Restore :
- `pattern_live_apply_snapshot()`
- `pattern_live_transition_reapply()`
- réapplique `param_registry_apply_track_value()` sur les valeurs track-aware capturées

Conclusion :
- si un nouveau paramètre moteur est correctement visible par `param_registry_get_track_value()` et `param_registry_apply_track_value()`, la capture/restore live suit déjà le corridor existant

### 9.2 Project

Projet :
- `project_product` orchestre le codec Project progressif
- `persistent_project_control` capture/applique les metadata et le Pattern de travail
- assets, macros et Patterns de bank sont fournis/consommés séparément

Fichiers SD :
- `pattern_control_bank.c` maintient un namespace transactionnel validé par `COMMIT.BIN`
- `persistent_control_codec.c` valide versions, sections, longueurs, clés et CRC indépendamment des `sizeof` RAM

### 9.3 Impact d’un nouveau moteur

Si le moteur ajoute seulement :
- un nouveau `UI_TRACK_TYPE`
- un nouveau `TRACK_RUNTIME_TYPE/ENGINE`
- des nouveaux `PARAM_*`

alors il faudra vérifier :
- exposition dans `persistent_control_model`
- clés stables dans `persistent_key_catalog`
- capture/application dans les adapters CONTROL
- version de `PERSIST_CODEC_VERSION` uniquement si le contrat disque évolue

Question pratique :
- l’ajout de `PARAM_*` change `PARAM_COUNT`, donc change les arrays `track_values[][PARAM_COUNT]` et `global_values[PARAM_COUNT]`
- en conséquence, l’impact format disque est très probable

## 10. Boot and init

Initialisation observée dans `Src/Core/brick6_app_init.c` :
- `drum_synth_init(48000.0f)`
- `brick6_sampler_runtime_init()`
- `brick6_audio_runtime_init()`
- `audio_init()`
- `audio_set_float_callback(brick6_audio_runtime_dsp)`
- `seq_runtime_init()`
- `ui_core_init()`
- `pattern_live_init()`
- `project_v1_init()`
- `audio_start()`

Ordre important :
- le runtime moteur doit être prêt avant `audio_start()`
- le callback DSP doit être installé avant `audio_start()`
- les defaults param/state doivent être disponibles avant qu’un restore projet/pattern reconfigure les tracks

Seams probables pour un futur moteur :
- `Src/Core/brick6_app_init.c` : appel `engine_runtime_init()` ou équivalent
- éventuellement un nouveau module bootstrap si le moteur requiert tables/ROM/init hors IRQ

Question à vérifier au moment du patch :
- si le moteur requiert tables volumineuses ou précomputations, confirmer qu’elles sont faites hors IRQ et avant `audio_start()`

## 11. Future engine checklist

1. Ajouter l’identité produit si nécessaire.
- Fichiers probables :
  - `Inc/UI/ui_core.h`
  - `Src/UI/ui_track_catalog.c`
  - `Src/Core/track_state.c` si validation supplémentaire requise

2. Ajouter le type/runtime binding.
- Fichiers probables :
  - `Inc/Core/track_runtime.h`
  - `Src/Core/track_runtime.c`
- Fonctions probables :
  - `track_runtime_type_from_ui()`
  - `track_runtime_compute_flags()`
  - `track_runtime_compute_ui_ensemble_mask()`
  - `track_runtime_bind_ctx()`

3. Ajouter l’état canonique TONE du moteur.
- Fichiers probables :
  - `Inc/Core/track_tone_sound_state.h`
  - `Src/Core/track_tone_sound_state.c`

4. Ajouter les paramètres moteur au catalogue.
- Fichiers probables :
  - `Inc/Param/param_store.h`
  - `Src/Param/param_registry_catalog.c`
  - `Src/Param/param_registry_apply_wrappers.c`

5. Ajouter le backend d’apply.
- Fichiers probables :
  - `Src/Param/param_registry_backends.c`
  - `Src/Param/param_registry_tone_backends.c`
  - `Src/Core/track_runtime.c` pour les `param_rule`

6. Ajouter le runtime moteur.
- Fichiers probables :
  - nouveau module type `Src/Core/brick6_engine_runtime.c`
  - nouveau header associé
- Fonctions attendues :
  - init
  - reset
  - set_param(s)
  - note on/off ou trigger
  - render_track / process_block

7. Brancher le rendu audio.
- Fichiers probables :
  - `Src/Core/brick6_audio_runtime.c`
- Point d’insertion probable :
  - helper dédié type `brick6_render_engine_tracks()`

8. Brancher les événements notes/gates.
- Fichiers probables :
  - `Src/Seq/seq_play_scheduler.c`
  - `Src/Keyboard/keyboard_engine.c`
- Fonctions probables :
  - `seq_play_scheduler_emit_engine_note()`
  - `keyboard_engine_dispatch_note_to_matching_tracks()`
  - `keyboard_engine_note_on_internal()`
  - `keyboard_engine_note_off_internal()`

9. Exposer les pages UI.
- Fichiers probables :
  - `Src/UI/pages/ui_page_template_tone.c`
  - éventuellement `Src/UI/ui_renderer_template.c`

10. Vérifier p-locks et exclusions.
- Fichiers probables :
  - `Src/Core/track_runtime.c`
  - `Src/Seq/seq_param_iface.c`
  - `Src/UI/pages/ui_page_template_tone.c`
  - `Src/UI/ui_param.c`
- Points à trancher :
  - quels paramètres moteur sont p-lockables
  - quel ordre `TONE slot -> param` est exposé pour le type runtime
  - quels paramètres doivent être exclus
  - quels paramètres sont compatibles avec l’apply RT fast
  - vérifier que UI/live-rec/scheduler utilisent `param_to_slot` / `slot_to_param`, pas `seq_param_iface_map_param()`
- Invariant : ne pas modifier le format Seq/p-lock pour ajouter un moteur ; ajouter seulement le mapping de slots et les règles d’exposition.

11. Vérifier persistence pattern/project.
- Fichiers probables :
  - `Inc/Storage/pattern_live_ram.h`
  - `Src/Storage/pattern_sd_bank.c`
  - `Inc/Storage/project_v1.h`
  - `Src/Storage/project_sd_bank.c`
- Vérifications probables :
  - impact `PARAM_COUNT`
  - bump de version fichier si layout change

12. Ajouter l’init boot.
- Fichiers probables :
  - `Src/Core/brick6_app_init.c`

13. Vérifier build et tests manuels.
- Vérifications minimales :
  - boot sans audio glitch
  - changement family/type
  - édition TONE
  - jeu clavier
  - playback sequencer
  - p-locks
  - save/load pattern
  - save/load project

## 12. Risks and open questions

### Risques techniques

Risque de seconde autorité :
- créer un cache moteur local qui diverge de `track_tone_sound_state` ou du backend Z3

Risque de bypass UI :
- écrire directement dans le runtime moteur depuis une page au lieu de passer par `param_registry_apply_track_value()`

Risque hard-RT :
- intégrer un moteur trop coûteux ou à coût variable dans `brick6_audio_runtime_dsp()`

Risque de routing :
- supposer une lane physique implicite au lieu de respecter `track_runtime_get_mix_target_track()`

Risque de note path incomplet :
- patcher `seq_play_scheduler` sans patcher `keyboard_engine`, ou inversement

Risque persistence :
- ajouter des `PARAM_*` sans revisiter le format disque pattern/project

Risque UI :
- créer une page TONE non gatee par `track_runtime_is_ui_ensemble_available()`

### Ambiguïtés observées

`TRACK_RUNTIME_VOICE_MODE_POLY` existe mais aucun moteur in-tree ne l’active.
Si un futur moteur requiert une vraie polyphonie runtime, il faudra revérifier `track_runtime_get_voice_mode()` et tous les consumers reliés.

`TRACK_RUNTIME_ENGINE_NONE` est utilisé pour la family MIDI, mais la track reste `CAN_PLAY`.
C’est voulu pour les chemins note/MIDI sans moteur audio ; ne pas confondre avec une track OFF.

`track_runtime_compute_flags()` marque `Synth/Sampler` comme `CAN_PLAY` mais pas `CAN_SYNTH`.
Cela confirme que les flags ne sont pas un simple alias “moteur audio présent”.

Certains paramètres peuvent être explicitement non p-lockables.
Il faudra décider moteur par moteur quels paramètres doivent être p-lockables, modulables, ou exclus du chemin RT fast.
Pour `TONE`, cette décision doit être portée par le mapping runtime effectif et consommée via `param_to_slot` / `slot_to_param`; `seq_param_iface_map_param()` ne doit pas redevenir une API d’encodage p-lock.

### Décisions à prendre avant l’intégration d’un nouveau moteur

- Le moteur est-il un nouveau `UI_TRACK_TYPE_*` dans une family existante ?
- Nécessite-t-il une nouvelle family produit ?
- Le moteur sort-il en mono ou en stéréo ?
- Si stéréo, le chemin d’injection mixer actuel suffit-il ?
- Le moteur doit-il être mono, multi-instance, ou polyphonique par track ?
- Quels paramètres sont :
  - TONE canoniques
  - p-lockables
  - modulables LFO
  - exclus du RT fast
- L’ajout des paramètres modifie-t-il le contrat disque et impose-t-il un bump explicite de `PERSIST_CODEC_VERSION` ?
- Le moteur nécessite-t-il des tables, buffers ou précomputations à initialiser hors IRQ ?
