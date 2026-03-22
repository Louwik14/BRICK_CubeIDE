# Architecture audio / FX actuelle

Ce document décrit **l’existant** observé dans le code de `BRICK6_CUBE_fonctionnel`. Il sert de base technique pour intégrer proprement de futurs FX sans extrapoler au-delà de ce qui est réellement branché aujourd’hui.

---

## 1. Vue d’ensemble

### 1.1 Pipeline global

La chaîne audio actuelle est organisée en **blocs de 64 frames** (`AUDIO_BLOCK_SIZE`) et s’exécute depuis les callbacks DMA RX SAI dans `audio.c`.

Flux réel :

1. `audio.c` reçoit un half-buffer TDM8 via DMA.
2. `audio_process_block_int32()` dans `audio_float.c` convertit le TDM int24 vers des `StereoTrack` float.
3. `audio_float.c` appelle `dsp_engine_process_block()`, qui délègue au callback applicatif `my_dsp()` enregistré par `brick6_app_init.c`.
4. `my_dsp()` alimente certaines tracks avec les sources internes (sampler/voices, MicroDexed), puis appelle `mixer_process()`.
5. `mixer_process()` applique les inserts, les sends, le routing vers `MAIN` et `CUE`, puis réécrit le résultat dans `tracks[0]` (MAIN) et `tracks[1]` (CUE).
6. `audio_float.c` repacke `tracks[0]` et `tracks[1]` vers les slots TDM de sortie.

### 1.2 Grands blocs actuels

- **Couche matériel audio** : `Src/Audio/audio.c`.
- **Frontière conversion int24/TDM ↔ float/track** : `Src/Audio/audio_float.c`, `Src/Audio/audio_io.c`.
- **Dispatcher DSP applicatif** : `Src/Audio/dsp_engine.c`.
- **Callback DSP applicatif principal** : `Src/Core/brick6_app_init.c` via `my_dsp()`.
- **Mixer final** : `Src/Audio/mixer.c`.
- **Pool d’instances FX** : `Src/Audio/fx_pool.c`.
- **Chaînage/runtime d’appel des FX** : `Src/Audio/fx_chain.c`.
- **Système de paramètres** : `Src/Param/param_registry.c`, `param_store.c`, `control_router.c`.

### 1.3 Où interviennent mixer, synthés, FX, bus et master

- **Entrées audio physiques** : tracks 0, 1 et 2 depuis les slots TDM RX 0/1, 2/3 et 4/5.
- **Source interne** : track 3, réservée aux sources internes ; aujourd’hui elle est alimentée par `microdexed_synth_process_block()` dans `my_dsp()`.
- **Sampler / voices** : `voice_manager_process()` écrit directement dans `tracks[0]` avant le mixer.
- **FX inserts** : appliqués par `mixer_process()` sur chaque track, via les indices de slot configurés dans `g_tracks[t].insert_slot[]`.
- **FX send** : les sends cumulent des buffers `send_l/send_r`, puis un slot FX optionnel est appliqué par bus de send, et le retour est sommé dans `MAIN` uniquement.
- **Bus** : `mixer_process()` construit deux bus float internes, `bus_main_*` et `bus_cue_*`.
- **Master final** : `audio_io_pack()` applique `output_adjust * master_gain` lors de la conversion float → int24 de sortie.

---

## 2. Cartographie des fichiers

### 2.1 `Src/Audio/audio.c` / `Inc/Audio/audio.h`

**Rôle exact**
- Gère le streaming audio STM32H743 SAI + DMA en double-buffer.
- Sur chaque interruption RX half/full, appelle `audio_process_block_int32()`.

**Structs / types importants**
- `audio_process_fn` : API historique, non utilisée par le chemin actif.

**Fonctions importantes**
- `audio_init()` : mémorise les handles SAI, clear les buffers DMA.
- `audio_start()` : démarre RX puis TX DMA.
- `HAL_SAI_RxHalfCpltCallback()` / `HAL_SAI_RxCpltCallback()` : point d’entrée temps réel.
- `process_half()` : sélectionne la moitié de buffer puis appelle `audio_process_block_int32()`.

**Dépendances utiles**
- `audio_float.c` pour le traitement bloc réel.
- `engine_tasklet_notify_frames()` pour notifier le scheduler applicatif.

### 2.2 `Src/Audio/audio_float.c` / `Inc/Audio/audio_float.h`

**Rôle exact**
- Frontière entre le flux TDM int24 et le modèle DSP float par tracks stéréo.
- Maintient le tableau statique `tracks[MAX_TRACKS]`.
- Expose des setters de paramètres audio globaux et des wrappers vers les FX du pool.

**Structs importantes**
- `StereoTrack` : buffers `L[]`, `R[]`, flag `enabled`.
- `audio_debug_stats_t`.

**Fonctions importantes**
- `audio_tracks_init()` : reset tracks, initialise EQ, saturation et compresseur si présents dans le pool.
- `track_enable()` : active une track ; reset l’EQ si la track 0 passe de off à on.
- `audio_set_float_callback()` : enregistre le callback DSP applicatif via `dsp_engine_set_callback()`.
- `audio_process_block_int32()` : pipeline principal par bloc.
- `audio_float_set_*()` : wrappers de pilotage master / EQ / saturation / compresseur.

**Dépendances utiles**
- `audio_io.c` pour l’unpack/pack.
- `dsp_engine.c` pour déléguer au callback applicatif.
- `fx_pool.c` pour récupérer les états FX par slot.
- `control_events.c` : une file d’événements est dépilée ici, sans traitement associé visible dans ce fichier.

### 2.3 `Src/Audio/audio_io.c` / `Inc/Audio/audio_io.h`

**Rôle exact**
- Convertit le buffer TDM int24 RX vers les `StereoTrack` float.
- Convertit les bus MAIN/CUE float vers le buffer TDM TX int24.

**Fonctions importantes**
- `audio_io_unpack()` :
  - track 0 ← slots RX 0/1
  - track 1 ← slots RX 2/3
  - track 2 ← slots RX 4/5
  - track 3 forcée à zéro côté entrée
- `audio_io_pack()` :
  - slots TX 0/1 ← MAIN
  - slots TX 2/3 ← CUE
  - slots TX 4..7 = 0

**Dépendances utiles**
- `audio_float.h` pour `StereoTrack` et `AUDIO_BLOCK_SIZE`.

### 2.4 `Src/Audio/dsp_engine.c` / `Inc/Audio/dsp_engine.h`

**Rôle exact**
- Pont minimal entre `audio_float.c` et le callback DSP applicatif.

**Fonctions importantes**
- `dsp_engine_set_callback()`.
- `dsp_engine_process_block()`.

**Dépendances utiles**
- Aucune logique DSP ; simple dispatch.

### 2.5 `Src/Core/brick6_app_init.c`

**Rôle exact**
- Initialise toute l’application audio.
- Déclare le callback DSP principal `my_dsp()`.
- Active les slots du FX pool actuellement utilisés.

**Fonctions importantes**
- `my_dsp()` :
  - alimente `tracks[3]` avec `microdexed_synth_process_block()`
  - remplit `tracks[0]` avec `voice_manager_process()`
  - appelle `mixer_process()`
  - écrit dans le live recorder
- `brick6_app_init()` :
  - `mixer_init()`
  - `fx_pool_init()`
  - activation des slots 0=EQ, 1=SAT, 2=DAISY_COMP
  - insertion du slot 2 sur `track0/insert0`
  - `audio_tracks_init()`
  - activation des 4 tracks
  - `audio_set_float_callback(my_dsp)`
  - `param_store_init()` puis une série de `param_reset()`

**Dépendances utiles**
- `mixer.c`, `fx_pool.c`, `audio_float.c`, synthés et sampler.

### 2.6 `Src/Audio/mixer.c` / `Inc/Audio/mixer.h`

**Rôle exact**
- Moteur de mixage final track-based.
- Gère gains, pan, mute, routing, inserts, sends et retours FX.

**Structs importantes**
- `mixer_track_t` interne :
  - `gain`, `pan`, `mute`
  - `route_master`, `route_cue`
  - `insert_slot[2]`
  - `send_level[2]`

**Fonctions importantes**
- `mixer_init()` : initialise l’état des 4 tracks et des 2 sends.
- `mixer_set_track_*()` : configuration runtime.
- `mixer_set_send_fx_slot()` : assigne un slot du FX pool à un bus send.
- `mixer_process()` : cœur du mix.

**Dépendances utiles**
- `fx_chain.c` pour appeler un slot FX.
- `audio_float.c` pour le master gain via `mixer_set_master()`.
- `sd_multitrack_recorder.c` pour les taps d’observation.

### 2.7 `Src/Audio/fx_pool.c` / `Inc/Audio/fx_pool.h`

**Rôle exact**
- Stocke les instances FX disponibles sous forme de slots indexés.
- Gère activation/désactivation et ownership des états DSP.

**Structs / enums importantes**
- `fx_type_t` : `FX_NONE`, `FX_EQ3`, `FX_SAT`, `FX_GRANULAR`, `FX_DAISY_COMP`.
- `fx_slot_t` :
  - `active`
  - `type`
  - `state`

**Fonctions importantes**
- `fx_pool_init()`.
- `fx_pool_activate_slot(index, type)`.
- `fx_pool_deactivate_slot(index)`.
- `fx_pool_get_slot(index)`.

**Dépendances utiles**
- `fx_dj_eq3_cmsis`, `fx_saturation`, `fx_granular`, `fx_daisy_comp`.

### 2.8 `Src/Audio/fx_chain.c` / `Inc/Audio/fx_chain.h`

**Rôle exact**
- Applique un slot FX du pool sur un buffer stéréo.
- Fait le dispatch `type -> process_block()`.

**Fonctions importantes**
- `fx_chain_process_slot()` : applique un slot précis.
- `fx_chain_process_track0()` : helper historique qui chaîne les slots 0..2.

**Dépendances utiles**
- `fx_pool_get_slot()`.
- Implémentations DSP des FX.

### 2.9 `Src/Audio/fx_dj_eq3_cmsis.c` / `Inc/Audio/fx_dj_eq3_cmsis.h`

**Rôle exact**
- EQ 3 bandes stéréo CMSIS via biquads.

**Structs importantes**
- `fx_dj_eq3_t` : instances biquads L/R, coefficients, état, fréquences et gains.

**Fonctions importantes**
- `fx_dj_eq3_init()`, `fx_dj_eq3_reset()`.
- `fx_dj_eq3_set_low_db()`, `set_mid_db()`, `set_high_db()`.
- `fx_dj_eq3_process_block()`.

**Dépendances utiles**
- CMSIS DSP `arm_biquad_casd_df1_inst_f32`.

### 2.10 `Src/Audio/fx_saturation.c` / `Inc/Audio/fx_saturation.h`

**Rôle exact**
- Saturation stéréo avec réglages tone / bias / drive / mix.

**Structs importantes**
- `fx_saturation_t` : `k`, `tone`, `asym`, `pre_gain`, `post_gain`, `mix`, `dry`, états IIR simples, `bypass`.

**Fonctions importantes**
- `fx_saturation_init()`.
- `fx_saturation_set_*_ui()`.
- `fx_saturation_process_block()`.

### 2.11 `Src/Audio/fx_granular.cpp` / `Inc/Audio/fx_granular.h`

**Rôle exact**
- Effet granular stéréo avec buffer circulaire dédié.

**Structs importantes**
- `fx_granular_state_t` est opaque côté header.
- L’implémentation contient le buffer state, les grains actifs, RNG, paramètres de densité/pitch/mix/freeze/spread/stereo.

**Fonctions importantes**
- `fx_granular_state_size()`.
- `fx_granular_init()`.
- `fx_granular_process_block()`.
- Setters de paramètres granular.

**Dépendances utiles**
- Le state est alloué statiquement par `fx_pool.c` dans `g_granular_state_storage`.

### 2.12 `Src/Audio/fx_daisy_comp.cpp` / `Inc/Audio/fx_daisy_comp.h`

**Rôle exact**
- Compresseur stéréo basé sur DaisySP.

**Structs importantes**
- `fx_daisy_comp_t` : `daisysp::Compressor core`, `attack_s`, `release_s`, `auto_makeup`, `manual_makeup_db`, `mix`.

**Fonctions importantes**
- `fx_daisy_comp_get_instance()` : singleton statique.
- `fx_daisy_comp_init()`.
- `fx_daisy_comp_set_*()`.
- `fx_daisy_comp_process_block()`.

**Dépendances utiles**
- DaisySP compressor core.

### 2.13 `Src/Audio/fx_onepole.c` / `Inc/Audio/fx_onepole.h`

**Rôle exact**
- Filtre one-pole générique (LP / HP selon `mode`).

**Structs importantes**
- `fx_onepole_t` : `g`, `gi`, `state`, `mode`.

**Fonctions importantes**
- `fx_onepole_init()`, `fx_onepole_reset()`.
- `fx_onepole_set_freq()`, `fx_onepole_set_mode()`.
- `fx_onepole_process()` en inline dans le header.

**Remarque**
- Ce module existe comme brique DSP, mais aucun chaînage explicite vers `mixer_process()` ou `fx_pool` n’est visible dans les fichiers analysés.

### 2.14 `Src/Param/param_registry.c` / `Inc/Param/param_registry.h`

**Rôle exact**
- Décrit tous les paramètres `PARAM_*` du projet.
- Associe métadonnées, bornes, valeur par défaut et callback `apply()`.

**Structs importantes**
- `param_desc_t` :
  - `id`, `name`, `type`
  - `min`, `max`, `step`, `default_value`
  - `display_type`, `unit`, `labels`
  - `apply`

**Fonctions importantes**
- `param_set()` : clamp la valeur, écrit dans `param_store`, appelle `apply` si non NULL.
- `param_get()`.
- `param_reset()`.

**Dépendances utiles**
- `mixer.c`, `audio_float.c`, `fx_daisy_comp`, `fx_granular`, `juno_synth`, `microdexed_synth`.

### 2.15 `Src/Param/param_store.c` / `Inc/Param/param_store.h`

**Rôle exact**
- Stockage double-buffer des paramètres (`staging` / `active`).
- Commit conditionnel synchronisé sur l’avancement des blocs audio.

**Structs importantes**
- `param_store_t` interne :
  - `staging[PARAM_COUNT]`
  - `active[PARAM_COUNT]`
  - `last_commit_block`, `commit_count`, `dirty`

**Fonctions importantes**
- `param_store_init()` : initialise store + registry + défauts.
- `param_store_set_staging()`.
- `param_store_set_active()`.
- `param_store_commit_if_block_advanced()`.
- `param_store_get_active()`.

### 2.16 `Src/Param/control_router.c` / `Inc/Param/control_router.h`

**Rôle exact**
- Façade légère pour pousser un paramètre de contrôle vers le système de paramètres.

**Fonctions importantes**
- `control_router_set_param()` :
  1. `param_set()` immédiat
  2. `param_store_set_staging()`
  3. tentative de `param_store_commit_if_block_advanced()`

**Remarque**
- Les `CTRL_PARAM_*` du header sont essentiellement des alias des `PARAM_*`.

---

## 3. Chemin réel du signal audio

### 3.1 Entrée matérielle

Le chemin physique commence dans `audio.c` :

- DMA RX remplit `rx_buffer` en TDM8.
- `process_half()` sélectionne la moitié courante.
- `audio_process_block_int32(rx, tx, 64)` est appelé.

### 3.2 Conversion TDM → tracks float

Dans `audio_process_block_int32()` :

- `audio_io_unpack()` convertit le bloc RX int24 vers `tracks[]` en float.
- Mapping réel :
  - `tracks[0]` ← slots RX 0/1
  - `tracks[1]` ← slots RX 2/3
  - `tracks[2]` ← slots RX 4/5
  - `tracks[3]` ← zéro
- L’échelle d’entrée appliquée est `postgain_recip * (1 / 8388608.0f)`.

### 3.3 Traitement DSP applicatif avant mix

Toujours dans `audio_process_block_int32()` :

- `audio_dsp_process()` appelle `dsp_engine_process_block()`.
- `dsp_engine_process_block()` appelle le callback enregistré, ici `my_dsp()`.

Dans `my_dsp()` :

1. **Track 3 / source interne**
   - si `tracks[3].enabled`, `microdexed_synth_process_block()` remplit un buffer mono.
   - ce mono est copié dans `tracks[3].L/R`.

2. **Track 0 / sampler voices**
   - si `tracks[0].enabled`, `voice_manager_process(tracks[0].L, tracks[0].R, frames)` écrit directement dans la track 0.
   - puis un `g_master_gain` local à `brick6_app_init.c` est appliqué sur cette track.
   - ce gain n’est pas le même que `audio_float` `master_gain`.

3. **Mix final**
   - `mixer_process(tracks, track_count, frames)` prend ensuite le relais.

### 3.4 Inserts de track

Dans `mixer_process()` pour chaque track active et non mute :

1. Récupération de `L` et `R` directement dans `tracks[t]`.
2. Pour `insert_idx = 0..1` :
   - lit `g_tracks[t].insert_slot[insert_idx]`
   - si `slot >= 0`, appelle `fx_chain_process_slot(slot, L, R, frames)`
3. Les inserts sont donc **in-place sur la track elle-même**.

### 3.5 Post-insert, fader, pan, sends

Toujours dans `mixer_process()` :

1. Tap d’enregistrement `SD_RECORDER_TAP_TRACK_POST_INSERT`.
2. Calcul du pan :
   - `pan_l = 1` ou `1 - pan`
   - `pan_r = 1` ou `1 + pan`
3. Application du gain track :
   - `L[i] *= gain_l`
   - `R[i] *= gain_r`
4. Tap `SD_RECORDER_TAP_TRACK_POST_FADER`.
5. Accumulation vers les buffers `send_l[s]` / `send_r[s]` si `g_send_fx_slot[s] >= 0`.
6. Tap `SD_RECORDER_TAP_TRACK_POST_SEND`.

Important : les sends sont calculés **après insert et après fader/pan**.

### 3.6 Routing vers MAIN / CUE

Après le calcul des sends, `mixer_process()` route chaque track :

- `route_master && route_cue` → somme dans `bus_main_*` et `bus_cue_*`
- `route_master` seul → somme dans `bus_main_*`
- `route_cue` seul → somme dans `bus_cue_*`
- sinon pas de sortie bus

### 3.7 FX de send et retour FX

Une fois toutes les tracks parcourues :

- pour chaque send `s` :
  - lit `g_send_fx_slot[s]`
  - si `slot >= 0`, applique `fx_chain_process_slot(slot, send_l[s], send_r[s], frames)`
  - somme le résultat **uniquement dans `bus_main_*`**

Il n’y a donc pas de retour send vers `bus_cue_*` dans le code actuel.

### 3.8 Sortie du mixer vers les tracks de sortie

Toujours dans `mixer_process()` :

- `tracks[0].L/R` reçoit une copie de `bus_main_l/r`
- `tracks[1].L/R` reçoit une copie de `bus_cue_l/r`

Conséquence : après `mixer_process()`, les tracks 0 et 1 cessent de représenter leurs sources d’origine et deviennent les **buffers de sortie bus**.

### 3.9 Sortie float → TDM

De retour dans `audio_process_block_int32()` :

- `audio_io_pack()` convertit :
  - `tracks[0]` → MAIN → slots TX 0/1
  - `tracks[1]` → CUE → slots TX 2/3
- gain final appliqué : `output_adjust * master_gain`
- slots TX 4..7 forcés à zéro

### 3.10 Résumé très concret du trajet

Chemin principal d’une source track donnée :

`RX TDM -> audio_io_unpack -> tracks[t] -> my_dsp (éventuelle génération/modification) -> inserts de track -> gain/pan -> sends -> routing MAIN/CUE -> FX de send -> retour send dans MAIN -> copie MAIN/CUE vers tracks[0]/tracks[1] -> audio_io_pack -> TX TDM`

Pour la source sampler actuelle :

`voice_manager_process -> tracks[0] -> insert(s) éventuel(s) -> gain/pan/send -> MAIN/CUE -> pack sortie`

Pour MicroDexed actuel :

`microdexed_synth_process_block -> tracks[3] -> insert(s) éventuel(s) -> gain/pan/send -> MAIN/CUE -> pack sortie`

---

## 4. Architecture du système FX

### 4.1 Types de FX existants aujourd’hui

Le `fx_pool` connaît 4 types d’effet réels :

- `FX_EQ3`
- `FX_SAT`
- `FX_GRANULAR`
- `FX_DAISY_COMP`

`FX_NONE` représente l’absence d’effet.

### 4.2 Identification des FX

L’identification se fait à deux niveaux :

1. **Type logique** dans `fx_type_t`.
2. **Slot runtime** dans le pool, indexé par entier (`uint32_t index`).

Le mixer et les paramètres ne manipulent pas directement un `fx_type_t` ; ils manipulent surtout des **indices de slot** (`int8_t slot`, `-1` signifiant “aucun FX”).

### 4.3 Instanciation réelle

L’instanciation n’est pas dynamique. Chaque type est branché sur un stockage statique :

- `FX_EQ3` → `g_eq` (instance statique globale)
- `FX_SAT` → `g_sat`
- `FX_GRANULAR` → state opaque dans `g_granular_state_storage[2048]` + buffers `grain_buffer_l/r[48000]`
- `FX_DAISY_COMP` → instance singleton renvoyée par `fx_daisy_comp_get_instance()`

### 4.4 Stockage runtime

Le stockage runtime côté pool est `g_slots[FX_POOL_SIZE]`, avec `FX_POOL_SIZE = 3`.

Chaque `fx_slot_t` stocke :

- `active` : slot utilisable ou non
- `type` : type courant
- `state` : pointeur vers l’instance DSP réelle

### 4.5 Activation / désactivation

- `fx_pool_activate_slot(index, type)` :
  - désactive d’abord le slot existant
  - branche le `state` correspondant
  - initialise certains états si nécessaire (granular)
  - marque le slot actif
- `fx_pool_deactivate_slot(index)` :
  - `active = 0`
  - remet `state = NULL`, `type = FX_NONE`
  - libère le drapeau `g_granular_in_use` pour le granular

### 4.6 Appel des FX dans le traitement audio

Aucun FX n’est appelé directement depuis `fx_pool`.

Le chemin réel est :

- `mixer_process()` ou autre code runtime choisit un index de slot
- `fx_chain_process_slot(slot, L, R, frames)` récupère `fx_pool_get_slot(slot)`
- `fx_chain_process_fx_slot()` fait le `switch(s->type)`
- le `process_block()` DSP correspondant est appelé

### 4.7 Configuration initiale actuelle

Dans `brick6_app_init()` :

- slot 0 → `FX_EQ3`
- slot 1 → `FX_SAT`
- slot 2 → `FX_DAISY_COMP`
- puis `mixer_set_track_insert_slot(0U, 0U, 2)` place **le compresseur Daisy en insert 0 de la track 0**

L’EQ et la saturation sont donc **instanciés dans le pool**, mais pas insérés par défaut dans le mixer au boot visible ici.

---

## 5. Fonctionnement du FX pool

### 5.1 Structure des slots

Un slot est défini par :

```c
typedef struct {
    uint8_t active;
    uint8_t type;
    void* state;
} fx_slot_t;
```

### 5.2 Type d’un slot

Le type est un `uint8_t` contenant une valeur de `fx_type_t`.

### 5.3 État actif / inactif

- `active = 1` : le slot peut être utilisé par `fx_chain`
- `active = 0` : `fx_chain_process_fx_slot()` sort immédiatement

### 5.4 Stockage d’instance / state

Le slot ne possède pas lui-même la mémoire DSP ; il ne fait que pointer sur :

- une instance statique globale (`g_eq`, `g_sat`)
- un singleton (`fx_daisy_comp_get_instance()`)
- une zone de storage statique dédiée (`g_granular_state_storage`)

### 5.5 Initialisation

`fx_pool_init()` :

- parcourt les 3 slots
- met `active = 0`, `type = FX_NONE`, `state = NULL`
- remet `g_granular_in_use = 0`

L’initialisation fine des DSP ne se fait pas complètement dans `fx_pool_init()` :

- `audio_tracks_init()` initialise l’EQ, la saturation et le compresseur Daisy si les slots correspondants existent déjà
- `fx_pool_activate_slot(FX_GRANULAR)` appelle directement `fx_granular_init()`

### 5.6 Activation / désactivation

#### Activation

`fx_pool_activate_slot()` :

- vérifie `index < FX_POOL_SIZE`
- désactive le slot courant
- selon `type` :
  - branche l’instance ou initialise le state
- écrit `slot->type`
- barrière mémoire `__DMB()` / `__DSB()`
- `slot->active = 1`

#### Désactivation

`fx_pool_deactivate_slot()` :

- met `active = 0`
- remet `state = NULL` et `type = FX_NONE`
- cas particulier granular : libère `g_granular_in_use`

### 5.7 Accès à un slot

- `fx_pool_get_slot(index)` retourne `NULL` si hors bornes.
- sinon retourne `&g_slots[index]`.

Beaucoup de code repose sur cet accès direct, par exemple :

- `audio_float.c` suppose implicitement :
  - slot 0 = EQ
  - slot 1 = saturation
  - slot 2 = compresseur Daisy
- `param_registry.c` parcourt les slots pour trouver le premier `FX_GRANULAR` actif
- `mixer.c` référence des slots par simple entier stocké dans ses inserts/sends

### 5.8 Limites actuelles du système

1. **Pool très petit** : `FX_POOL_SIZE = 3`.
2. **Convention implicite forte** : `audio_float.c` hard-code les slots 0/1/2 pour EQ/SAT/COMP.
3. **Un seul granular à la fois** : `g_granular_in_use` interdit plusieurs instances.
4. **Pas d’allocation par track** : plusieurs tracks peuvent référencer le même slot, donc partager exactement le même état DSP.
5. **Pas de metadata de rôle** (insert/send/master) dans le pool ; seul le code appelant donne le sens du slot.
6. **Initialisation dispersée** entre `fx_pool_activate_slot()` et `audio_tracks_init()`.

---

## 6. Lien entre paramètres et FX

### 6.1 Principe général

Le chaînage paramètre → moteur est :

`PARAM_* -> param_registry[id] -> param_set() -> apply_* -> module audio/FX/mixer`

### 6.2 Rôle de `param_registry`

`param_registry` est la table centrale décrivant chaque paramètre :

- bornes
- défaut
- type d’affichage
- callback `apply`

Quand `param_set(id, value)` est appelé :

1. la valeur est clampée selon `param_registry[id].min/max`
2. `param_store_set_active(id, clamped)` est appelé
3. si `apply != NULL`, le callback est exécuté immédiatement

C’est donc `param_registry` qui matérialise le lien entre un `PARAM_*` et son effet concret sur l’audio.

### 6.3 Rôle des callbacks `apply_*`

Les callbacks `apply_*` encapsulent le branchement réel vers le runtime.

Exemples :

- `apply_mix_track0_insert0()` → `mixer_set_track_insert_slot(0, 0, slot)`
- `apply_mix_send0_fx()` → `mixer_set_send_fx_slot(0, slot)`
- `apply_eq_low_db()` → `audio_float_set_dj_eq_low_db()`
- `apply_sat_drive()` → `audio_float_set_saturation_drive_ui()`
- `apply_gran_density()` → cherche un `FX_GRANULAR` actif puis appelle `fx_granular_set_density()`
- `apply_daisy_threshold()` → récupère le singleton Daisy puis appelle `fx_daisy_comp_set_threshold_db()`

### 6.4 Rôle éventuel de `param_store`

`param_store` conserve deux copies :

- `staging[]`
- `active[]`

Son but est de permettre un commit synchronisé avec l’avancement des blocs audio via `param_store_commit_if_block_advanced()`.

Cependant, dans le chemin `control_router_set_param()` actuel :

1. `param_set()` applique déjà immédiatement le paramètre et appelle `apply`
2. la valeur active est recopiée dans le staging
3. un commit est éventuellement tenté

En pratique, le comportement visible aujourd’hui est donc **immédiat**, et `param_store` joue surtout un rôle de stockage cohérent / historique de commit.

### 6.5 Rôle éventuel de `control_router`

`control_router_set_param()` est une façade API pour la couche de contrôle/UI.

Son rôle actuel est léger :

- convertir `control_param_id_t` en `param_id_t`
- déléguer à `param_set()`
- synchroniser `param_store`

### 6.6 Paramètres FX déjà branchés

#### Granular

Branchés :

- `PARAM_GRAN_DENSITY`
- `PARAM_GRAN_PITCH`
- `PARAM_GRAN_MIX`
- `PARAM_GRAN_FREEZE`
- `PARAM_GRAN_SPREAD`
- `PARAM_GRAN_STEREO`

Mais ces paramètres n’ont un effet que si **un slot `FX_GRANULAR` actif** existe dans le pool.

#### Daisy compressor

Branchés :

- `PARAM_DAISY_COMP_THRESHOLD_DB`
- `PARAM_DAISY_COMP_RATIO`
- `PARAM_DAISY_COMP_ATTACK_S`
- `PARAM_DAISY_COMP_RELEASE_S`
- `PARAM_DAISY_COMP_MAKEUP_DB`
- `PARAM_DAISY_COMP_AUTO_MAKEUP`
- `PARAM_DAISY_COMP_MIX`

Ils pilotent toujours le **singleton Daisy compressor**, qu’il soit ou non inséré dans le mixer.

#### Bus compressor (nommage actuel)

Les paramètres `PARAM_BUS_COMP_*` sont branchés vers les wrappers `audio_float_set_bus_comp_*()`, mais ces wrappers pointent eux aussi vers `fx_pool_daisy_comp_state()`, donc vers **le slot 2** supposé contenir `FX_DAISY_COMP`.

Autrement dit, dans l’état actuel, le “bus comp” n’est pas une implémentation distincte du Daisy comp : c’est un **autre nom de contrôle** du même compresseur si le slot 2 est bien un `FX_DAISY_COMP`.

#### EQ / saturation

- `PARAM_EQ_LOW_DB`, `MID_DB`, `HIGH_DB` pilotent les setters `audio_float_set_dj_eq_*()`.
- `PARAM_SAT_TONE`, `BIAS`, `DRIVE`, `MIX` pilotent les setters saturation côté `audio_float.c`.

Ces setters supposent implicitement :

- EQ = slot 0
- SAT = slot 1

### 6.7 Paramètres mixer liés aux FX

Les paramètres suivants relient directement le système de paramètres au routing FX :

- `PARAM_MIX_TRACKx_INSERTy` → choix du slot FX d’insert
- `PARAM_MIX_TRACKx_SENDz` → niveau de send
- `PARAM_MIX_SENDz_FX` → slot FX appliqué au send z

C’est le vrai pont entre paramètres et placement des FX dans la chaîne audio.

### 6.8 Paramètres déclarés mais pas complètement exploités

Points visibles dans le code :

- `PARAM_BUS_COMP_DRYWET` et `PARAM_BUS_COMP_HPF_HZ` existent dans `param_registry`, mais leur `apply` est `NULL`.
- donc ils sont **déclarés**, clampés et stockés, mais **n’ont aucun effet runtime observable** dans les fichiers analysés.

---

## 7. Points d’insertion pour un futur FX

### 7.1 Futur FX de track

**Où dans le code**
- `mixer_process()` dans la boucle track, via `g_tracks[t].insert_slot[]` puis `fx_chain_process_slot()`.
- Configuration via `mixer_set_track_insert_slot()` et paramètres `PARAM_MIX_TRACKx_INSERTy`.

**Déjà prévu implicitement ?**
- Oui.
- C’est aujourd’hui le point d’insertion le plus explicite et le plus propre pour un FX track-level.

**Avantages**
- Même mécanique pour toutes les tracks.
- Contrôlable via le système de paramètres existant.
- Position claire : avant fader/pan/sends.

**Limites**
- Seulement 2 inserts par track.
- Les inserts référencent des slots partagés du `fx_pool` ; si plusieurs tracks pointent vers le même slot, elles partageront le même état DSP.
- Le pool actuel ne contient que 3 slots.

### 7.2 Futur FX de send

**Où dans le code**
- Accumulation send dans `mixer_process()` via `send_l/send_r`.
- Application d’un FX send via `g_send_fx_slot[s]` puis `fx_chain_process_slot()`.

**Déjà prévu implicitement ?**
- Oui.
- L’architecture send existe déjà avec 2 sends (`MIXER_NUM_SENDS = 2`).

**Avantages**
- Point standard pour des FX de type reverb/delay/granular send.
- Contrôle déjà prévu par :
  - `PARAM_MIX_TRACKx_SENDz`
  - `PARAM_MIX_SENDz_FX`

**Limites**
- Le retour send est routé uniquement vers `MAIN`, pas vers `CUE`.
- Là encore, le FX est référencé par slot partagé.
- Aucun send n’est configuré par défaut au boot dans `brick6_app_init.c`.

### 7.3 Futur FX master

**Où dans le code**
- Il n’existe pas de “master insert” explicite dans `mixer_process()`.
- Les endroits cohérents seraient :
  1. juste après la construction des bus `bus_main_*` / `bus_cue_*` dans `mixer_process()`
  2. ou dans `audio_float.c` juste avant `audio_io_pack()`

**Déjà prévu implicitement ?**
- Non, pas de slot dédié master aujourd’hui.

**Option la plus cohérente avec l’architecture actuelle**
- Ajouter un traitement master dans `mixer_process()` après la somme des tracks et après les retours send, car c’est là que le signal bus existe encore en float stéréo structuré.

**Avantages**
- Vision claire du bus final.
- Reste dans la logique `mixer` = lieu du routing/mix des bus.

**Limites**
- Nécessite d’étendre explicitement l’architecture, car rien n’est prévu aujourd’hui côté API mixer pour un insert master.

### 7.4 Futur filtre “de voice”

**Où dans le code**
- Pour la voie sampler actuelle : dans `voice_manager_process()` ou immédiatement après son appel dans `my_dsp()` avant `mixer_process()`.
- Pour Juno/MicroDexed : à l’intérieur des modules synthés eux-mêmes (`juno_synth_*`, `microdexed_synth_*`) si le filtre doit être “par voix” au sens instrument.

**Déjà prévu implicitement ?**
- Non pour un système générique de “voice FX” commun.
- Oui partiellement côté synthé Juno, qui possède déjà ses propres paramètres VCF (`JUNO_PARAM_VCF_*`).

**Avantages**
- Permet un vrai traitement avant mix, potentiellement par source ou par voix instrument.

**Limites**
- Ce n’est pas le même niveau architectural qu’un FX de track du `mixer`.
- Un filtre “de voice” ne devrait pas être branché dans le `fx_pool` si son état doit être indépendant pour plusieurs voix simultanées.
- Le `fx_pool` actuel est orienté instances partagées de blocs stéréo, pas voix polyphoniques multiples.

### 7.5 Option la plus cohérente selon le type de futur FX

- **FX de track stéréo partagé** → inserts du `mixer`
- **FX de type reverb/delay partagé** → sends du `mixer`
- **FX de bus final** → extension explicite du `mixer` côté master
- **Filtre réellement “par voix”** → dans le moteur source concerné (`voice_manager`, `juno_synth`, `microdexed_synth`), pas dans le `fx_pool` tel qu’il existe aujourd’hui

---

## 8. Procédure type pour ajouter un nouveau FX

Checklist basée sur l’existant.

### 8.1 Ajouter les fichiers DSP

- Créer `Src/Audio/fx_<nom>.(c|cpp)` et `Inc/Audio/fx_<nom>.h`.
- Exposer :
  - structure/state
  - `init`
  - setters de paramètres
  - `process_block()` stéréo si le FX vise le `fx_pool` / `mixer`

### 8.2 Ajouter le type FX si nécessaire

- Étendre `fx_type_t` dans `Inc/Audio/fx_pool.h`.
- Ajouter le case correspondant dans `fx_chain_process_fx_slot()` dans `Src/Audio/fx_chain.c`.

### 8.3 Ajouter la structure / state runtime

Selon le modèle choisi :

- instance statique globale comme `g_eq` / `g_sat`
- singleton comme Daisy comp
- storage opaque dédié comme granular

### 8.4 Ajouter l’entrée dans le pool si nécessaire

Dans `Src/Audio/fx_pool.c` :

- ajouter le case `FX_<NOM>` dans `fx_pool_activate_slot()`
- brancher `slot->state`
- initialiser le state si requis
- gérer `fx_pool_deactivate_slot()` si le FX a des ressources à libérer logiquement

Si le nombre de slots doit augmenter :

- modifier `FX_POOL_SIZE`
- vérifier les conventions implicites de slots déjà supposées ailleurs (`audio_float.c` notamment)

### 8.5 Brancher le traitement audio

#### Si FX de track ou de send

- utiliser le chemin existant `mixer -> fx_chain -> fx_pool`
- affecter le slot à une insert track ou à un send FX via :
  - `mixer_set_track_insert_slot()`
  - `mixer_set_send_fx_slot()`

#### Si FX master

- ajouter explicitement un point d’appel dans `mixer_process()` après la construction des bus
- éventuellement prévoir une API dédiée de type `mixer_set_master_fx_slot()` si l’architecture doit rester cohérente

#### Si FX “de voice”

- le brancher dans le module source concerné, pas nécessairement dans `fx_pool`

### 8.6 Ajouter les paramètres

Dans `Inc/Param/param_store.h` :

- ajouter les nouveaux `PARAM_<...>`

Dans `Src/Param/param_registry.c` :

- ajouter les `apply_<...>()`
- ajouter les entrées dans `param_registry[]`
- définir bornes, pas, défauts, type d’affichage

### 8.7 Ajouter les callbacks `apply_*`

Brancher chaque paramètre vers le bon niveau architectural :

- `mixer_*` si le paramètre configure le placement/routing FX
- `audio_float_*` si le projet suit déjà cette convention de wrappers hard-codés vers des slots connus
- `fx_<nom>_set_*()` si le callback peut accéder directement au state/instance

### 8.8 Initialisation boot

Dans `brick6_app_init()` :

- activer le slot voulu via `fx_pool_activate_slot()`
- l’affecter à un insert/send si nécessaire
- initialiser les paramètres par défaut via `param_reset()` ou par setters directs

### 8.9 Exposition future à l’UI

Sans l’implémenter ici, le point d’entrée prévu aujourd’hui est :

- `control_router_set_param()` côté contrôle/UI
- `param_registry` pour les métadonnées d’édition/affichage

### 8.10 Vérifications minimales à faire à chaque ajout

- le `process_block()` est bien appelé depuis `fx_chain`
- le slot est bien actif dans `fx_pool`
- le mixer pointe sur le bon slot
- les paramètres ont un `apply != NULL` si un effet runtime est attendu
- aucune hypothèse implicite de numéro de slot n’est cassée

---

## 9. Zones ambiguës ou à confirmer

### 9.1 Convention de slots hard-codée dans `audio_float.c`

`audio_float.c` suppose :

- slot 0 = EQ
- slot 1 = saturation
- slot 2 = Daisy comp

C’est cohérent avec `brick6_app_init()` aujourd’hui, mais ce n’est pas abstrait. Toute évolution du pool devra confirmer si cette convention doit rester vraie.

### 9.2 “Bus comp” vs Daisy comp

Les paramètres `PARAM_BUS_COMP_*` pilotent en pratique le `fx_pool_daisy_comp_state()` du slot 2. Il faut donc considérer que, dans l’état actuel, le “bus comp” n’est pas un moteur séparé visible ici, mais une autre façade de contrôle du même compresseur Daisy. **À confirmer** si un autre module historique devait exister.

### 9.3 EQ et saturation présents mais pas insérés par défaut dans le mixer

Le boot active bien `FX_EQ3` et `FX_SAT` dans le pool, mais seul le slot 2 est explicitement inséré dans `mixer_set_track_insert_slot(0U, 0U, 2)`. Aucun insert visible n’assigne les slots 0 ou 1 au mixer dans les fichiers analysés.

Les paramètres EQ et SAT sont donc bien reliés à des instances réelles, mais leur effet audio dépend du fait que ces slots soient effectivement appelés quelque part. Dans le code analysé, ce n’est pas visible par défaut. **À confirmer** selon d’autres chemins éventuels non demandés.

### 9.4 `fx_chain_process_track0()` existe encore

`fx_chain_process_track0()` chaîne directement les slots 0..2, mais aucun appel actif à ce helper n’a été relevé dans le chemin principal. Le chemin réellement utilisé passe par `mixer_process()` et ses inserts/sends.

### 9.5 `track_set_gain()` est vide dans `audio_float.c`

`mixer_set_track_gain()` appelle `track_set_gain()` pour les tracks < `MAX_TRACKS`, mais `track_set_gain()` ne fait rien. Le vrai gain track appliqué dans le chemin audio est celui de `mixer_process()` via `g_tracks[t].gain`.

### 9.6 Événements contrôle dépilés mais non traités dans `audio_float.c`

`audio_process_block_int32()` dépile jusqu’à 8 `control_event_t` par bloc, mais le contenu de `evt` n’est pas utilisé ensuite dans ce fichier. Il existe donc une mécanique de queue, mais pas de traitement visible ici. **À confirmer** si c’est un stub volontaire ou un branchement incomplet.

### 9.7 Paramètres déclarés sans effet runtime

Confirmé dans `param_registry[]` :

- `PARAM_BUS_COMP_DRYWET`
- `PARAM_BUS_COMP_HPF_HZ`

ont `apply = NULL`.

### 9.8 Taille et partage du pool

Le pool ne contient que 3 slots et plusieurs parties du code peuvent référencer les mêmes indices. Cela limite fortement l’ajout de nouveaux FX sans clarifier :

- quels slots sont réservés
- quels slots sont libres
- si un FX peut être partagé entre plusieurs tracks
- si une instance indépendante par track est nécessaire

---

## 10. Résumé utile

- L’audio arrive en TDM8 dans `audio.c`, est converti en `StereoTrack` float dans `audio_float.c`, puis traité par le callback applicatif `my_dsp()`.
- `my_dsp()` alimente les sources internes et appelle `mixer_process()`, qui est aujourd’hui **le vrai centre du routing audio/FX** : inserts, sends, routing MAIN/CUE et retour FX.
- Le système FX repose sur un `fx_pool` de 3 slots, appelés via `fx_chain`.
- Le système de paramètres repose sur `param_registry`, dont les callbacks `apply_*` font le lien concret vers le mixer, les FX et les synthés.
- Le chemin le plus propre pour ajouter un futur FX partagé est :
  - **FX de track** → inserts du mixer
  - **FX de send** → sends du mixer
  - **FX master** → extension explicite du mixer
  - **filtre par voix** → moteur source concerné, pas forcément le `fx_pool`
- Les fichiers les plus centraux à connaître pour la suite sont :
  - `Src/Audio/audio_float.c`
  - `Src/Audio/mixer.c`
  - `Src/Audio/fx_pool.c`
  - `Src/Audio/fx_chain.c`
  - `Src/Param/param_registry.c`
  - `Src/Core/brick6_app_init.c`
