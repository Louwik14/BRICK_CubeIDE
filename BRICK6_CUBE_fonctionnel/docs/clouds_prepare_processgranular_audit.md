# Clouds ciblé: Prepare() + ProcessGranular() (preuves locales)

## A) Prepare() résumé

| action | fichier | ligne preuve |
|---|---|---|
| Détecte changement de mode (`playback_mode_changed`) et si changement "benign" (non spectral<->non spectral) | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 370-373 |
| Si changement benign sans reset: reset des filtres + clear pitch-shifter + mémorise mode précédent | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 375-379 |
| Si reset/mode non-benign: désactive freeze (`parameters_.freeze = false`) | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 381-383 |
| Repartition mémoire si mono: buffer[0]=grand buffer sample, workspace=petit buffer | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 390-399 |
| Repartition mémoire si stéréo: buffer[0] et buffer[1] de même taille (`buffer_size_[1]`), workspace dans le reste du grand buffer | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 400-408 |
| Calcule `sr` via `sample_rate()` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 409 |
| Crée `BufferAllocator allocator(workspace, workspace_size)` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 411 |
| Alloue et init diffusion: `Allocate<float>(2048)` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 412 |
| Alloue et init reverb: `Allocate<uint16_t>(16384)` + `sr` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 413 |
| Taille corrélateur: `correlator_block_size = (kMaxWSOLASize / 32) + 2` | `mutable_instruments/clouds/dsp/granular_processor.cpp` + `wsola_sample_player.h` | 415 + 50 |
| Alloue mémoire corrélateur: `Allocate<uint32_t>(correlator_block_size * 3)` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 416-417 |
| Init corrélateur avec 2 pointeurs sur ce bloc | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 418-420 |
| Init pitch shifter avec ce même bloc casté `uint16_t*` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 421 |
| Si mode spectral: init phase vocoder (`phase_vocoder_.Init(...)`) | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 423-427 |
| Sinon: init audio buffers (`buffer_8_` ou `buffer_16_`) selon résolution | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 429-440 |
| Calcule `num_grains = (mono?40:32) * (low_fidelity?23:16) >> 4` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 442-443 |
| Init modules non-spectral: `player_`, `ws_player_`, `looper_` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 444-446 |
| Fin reset: `reset_buffers_=false`, `previous_playback_mode_=playback_mode_` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 448-449 |
| Post-prepare: spectral -> `phase_vocoder_.Buffer()`, stretch -> `ws_player_.LoadCorrelator(...)` + `correlator_.EvaluateSomeCandidates()` | `mutable_instruments/clouds/dsp/granular_processor.cpp` | 452-461 |

### Détails allocation (units)

- Diffuser: `Allocate<float>(2048)` = **8192 octets** (si `float=4`) (INCONNU strict ABI, vérifier `sizeof(float)` toolchain cible).  
  Preuve allocation: `granular_processor.cpp:412`.
- Reverb: `Allocate<uint16_t>(16384)` = **32768 octets**.  
  Preuve allocation: `granular_processor.cpp:413`.
- Correlator: `((kMaxWSOLASize/32)+2)*3` mots `uint32_t`; `kMaxWSOLASize=4096` -> `(128+2)*3=390` mots -> **1560 octets**.  
  Preuves: `granular_processor.cpp:415-417`, `wsola_sample_player.h:50`.

## B) ProcessGranular() pipeline (focus granular)

| étape DSP | fonction appelée | fichier | preuve ligne |
|---|---|---|---|
| Enregistre l’entrée dans buffer circulaire (hors spectral), avec fade et freeze-aware write | `AudioBuffer::WriteFade` | `granular_processor.cpp` -> `audio_buffer.h` | 80-89 -> 126-166 |
| Mapping macro-paramètres granular: `density` -> `use_deterministic_seed` + `overlap`, `texture` -> `window_shape` | (assignations directes) | `granular_processor.cpp` | 96-106 |
| Exécution moteur granular | `player_.Play(...)` | `granular_processor.cpp` -> `granular_sample_player.h` | 108-112 -> 70-164 |
| Scheduling des grains par sample dans le bloc | boucle `for (t=0; t<size; ++t)` + `ScheduleGrain(...)` | `granular_sample_player.h` | 91-117 |
| Overlap-add de tous les grains actifs du bloc | boucle `for (i=0; i<max_num_grains_; ++i)` + `g->OverlapAdd` | `granular_sample_player.h` | 123-144 |
| Calcul normalisation dépendant nombre de grains actifs | `active_grains`, `fast_rsqrt_carmack`, `Crossfade` | `granular_sample_player.h` | 147-157 |
| Application gain normalisation par sample stéréo | boucle `for (t=0; t<size; ++t)` | `granular_sample_player.h` | 159-163 |

### Ce qui se passe par sample dans un grain (interpolation/window/pitch/overlap)

| opération par sample | fonction/zone | fichier | preuve |
|---|---|---|---|
| Pitch: incrément de phase fixé à `pitch_ratio * 65536` lors du `Start` du grain | `grain->Start(... phase_increment ...)` | `granular_sample_player.h` | 189-190, 225-231 |
| Fenêtre (windowing): pré-rendu enveloppe triangular/smoothed/LUT `lut_window` | `RenderEnvelope<...>` | `grain.h` | 88-117 (LUT 101-103) |
| Interpolation d’échantillons depuis buffer circulaire | `AudioBuffer::Read<InterpolationMethod(quality)>` | `grain.h` + `audio_buffer.h` | 159-166 + 186-194 |
| Overlap/Add (somme dans destination stéréo) | additions `*destination++ += ...` | `grain.h` | 162-169 |

### Nombre max de grains (preuve)

- Capacité hard max: `kMaxNumGrains = 64`.  
  Preuve: `granular_sample_player.h:49`.
- `Prepare()` fixe `num_grains` runtime: 40 (mono HF), 32 (stéréo HF), modulé en low-fidelity par facteur `23/16` via `>>4`.  
  Preuve: `granular_processor.cpp:442-444`.
- `Play()` itère jusqu’à `max_num_grains_`.  
  Preuve: `granular_sample_player.h:123`.

## C) Hotspots CPU (preuves code)

| hotspot | pourquoi | preuve |
|---|---|---|
| `GranularSamplePlayer::Play` boucle scheduling `for t<size` | O(N) par bloc + logique random/seed/schedule | `granular_sample_player.h:91-117` |
| `GranularSamplePlayer::Play` boucle grains `for i<max_num_grains_` | O(G) + appels `OverlapAdd` | `granular_sample_player.h:123-144` |
| `Grain::OverlapAdd` boucle sample interne `while(size--)` | O(N) par grain actif; combiné => O(N * grains_actifs) | `grain.h:150-171` |
| Lecture interpolée Hermite/Linear/ZOH dans `AudioBuffer::Read*` | coût DSP à chaque sample/grain selon qualité | `audio_buffer.h:186-194`, `241-278` |
| Pré-rendu enveloppe dans `RenderEnvelope` | boucle sample par grain/bloc | `grain.h:95-117` |
| Écriture record buffer `WriteFade` | boucle sample d’entrée bloc | `audio_buffer.h:126-166` |

## D) Minimal granular-only files (mode granular uniquement)

Contrainte: **sans modifier le code actuel**.

### KEEP

- `mutable_instruments/clouds/clouds_resources.cpp` (LUT: `lut_grain_size`, `lut_sin`, `lut_window`, `lut_xfade_*`, `src_filter_1x_2_45`, `lut_ulaw`).
- `mutable_instruments/clouds/dsp/granular_processor.cpp`
- `mutable_instruments/clouds/dsp/correlator.cpp`
- `mutable_instruments/clouds/dsp/pvoc/frame_transformation.cpp`
- `mutable_instruments/clouds/dsp/pvoc/phase_vocoder.cpp`
- `mutable_instruments/clouds/dsp/pvoc/stft.cpp`
- `mutable_instruments/stmlib/dsp/atan.cpp`
- `mutable_instruments/stmlib/dsp/units.cpp`
- `mutable_instruments/stmlib/utils/random.cpp`

Raison: même en mode granular runtime, `granular_processor.cpp` contient des appels compilés vers spectral (`phase_vocoder_.Init/Process/Buffer`), donc ces symboles doivent être résolus à l’édition de liens si code inchangé.  
Preuves: `granular_processor.cpp:131-144`, `423-427`, `452-453`.

### OPTIONAL

- `mutable_instruments/clouds/dsp/mu_law.cpp` (table commentée; `lut_ulaw` défini dans `clouds_resources.cpp`).

### REMOVE

- **INCONNU** sans patch de code: retirer les `.cpp` pvoc n’est pas prouvable safe avec le code actuel (références directes présentes dans `granular_processor.cpp`).
- Vérification: tenter un link sans `pvoc/*.cpp` et observer symboles non résolus.

