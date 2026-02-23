# Clouds fork structure audit (repo-local evidence only)

## 1) Inventaire réel `mutable_instruments/clouds/dsp`

Commande utilisée:

```bash
find mutable_instruments/clouds/dsp -type f
```

Liste exhaustive (fichier | type):

- mutable_instruments/clouds/dsp/audio_buffer.h | .h
- mutable_instruments/clouds/dsp/correlator.cpp | .cpp
- mutable_instruments/clouds/dsp/correlator.h | .h
- mutable_instruments/clouds/dsp/frame.h | .h
- mutable_instruments/clouds/dsp/fx/diffuser.h | .h
- mutable_instruments/clouds/dsp/fx/fx_engine.h | .h
- mutable_instruments/clouds/dsp/fx/pitch_shifter.h | .h
- mutable_instruments/clouds/dsp/fx/reverb.h | .h
- mutable_instruments/clouds/dsp/grain.h | .h
- mutable_instruments/clouds/dsp/granular_processor.cpp | .cpp
- mutable_instruments/clouds/dsp/granular_processor.h | .h
- mutable_instruments/clouds/dsp/granular_sample_player.h | .h
- mutable_instruments/clouds/dsp/looping_sample_player.h | .h
- mutable_instruments/clouds/dsp/mu_law.cpp | .cpp
- mutable_instruments/clouds/dsp/mu_law.h | .h
- mutable_instruments/clouds/dsp/parameters.h | .h
- mutable_instruments/clouds/dsp/pvoc/frame_transformation.cpp | .cpp
- mutable_instruments/clouds/dsp/pvoc/frame_transformation.h | .h
- mutable_instruments/clouds/dsp/pvoc/phase_vocoder.cpp | .cpp
- mutable_instruments/clouds/dsp/pvoc/phase_vocoder.h | .h
- mutable_instruments/clouds/dsp/pvoc/stft.cpp | .cpp
- mutable_instruments/clouds/dsp/pvoc/stft.h | .h
- mutable_instruments/clouds/dsp/sample_rate_converter.h | .h
- mutable_instruments/clouds/dsp/window.h | .h
- mutable_instruments/clouds/dsp/wsola_sample_player.h | .h

## 2) Point d'entrée réel et appels de `GranularProcessor::Process`

Point d’entrée réel:

- `void GranularProcessor::Process(ShortFrame* input, ShortFrame* output, size_t size)`
  - déclaré dans `granular_processor.h`
  - défini dans `granular_processor.cpp`

Appels présents DANS `Process` + provenance:

- `copy(...)` -> C++ STL (`std::copy`), utilisé directement dans `granular_processor.cpp`; le header exact est transitif depuis les includes (INCONNU localement sans préprocesseur).
- `fill(...)` -> C++ STL (`std::fill`), utilisé directement dans `granular_processor.cpp`; header exact transitif INCONNU sans dump préprocesseur.
- `sample_rate()` -> méthode inline privée `GranularProcessor::sample_rate()`, `granular_processor.h`.
- `fb_filter_[i].set_f_q<...>()`, `.set(...)`, `.Process<...>()` -> `stmlib::Svf` dans `stmlib/dsp/filter.h` (inclus via `granular_processor.h`).
- `SoftLimit(...)` -> `stmlib/dsp/dsp.h`.
- `src_down_.Process(...)` / `src_up_.Process(...)` -> `SampleRateConverter::Process` dans `sample_rate_converter.h`.
- `ProcessGranular(...)` -> méthode privée définie dans `granular_processor.cpp`.
- `diffuser_.set_amount(...)` / `diffuser_.Process(...)` -> `clouds/dsp/fx/diffuser.h`.
- `looper_.synchronized()` -> `clouds/dsp/looping_sample_player.h`.
- `SemitonesToRatio(...)` -> `stmlib/dsp/units.h`.
- `pitch_shifter_.set_ratio(...)`, `.set_size(...)`, `.Process(...)` -> `clouds/dsp/fx/pitch_shifter.h`.
- `lp_filter_[]` / `hp_filter_[]` methods -> `stmlib::Svf`.
- `reverb_.set_amount(...)`, `.set_diffusion(...)`, `.set_time(...)`, `.set_input_gain(...)`, `.set_lp(...)`, `.Process(...)` -> `clouds/dsp/fx/reverb.h`.
- `ParameterInterpolator dry_wet_mod(...)` + `dry_wet_mod.Next()` -> `stmlib/dsp/parameter_interpolator.h`.
- `Interpolate(...)` -> `stmlib/dsp/dsp.h`.
- `SoftConvert(...)` -> `stmlib/dsp/dsp.h`.
- macros `ONE_POLE(...)` / `CONSTRAIN(...)` -> `stmlib/dsp/dsp.h` et `stmlib/stmlib.h`.

## 3) Dépendances réelles (chaîne locale)

### Niveau 0: `granular_processor.cpp`

Includes directs:

- `clouds/dsp/granular_processor.h`
- `<cstring>`
- `stmlib/dsp/parameter_interpolator.h`
- `stmlib/utils/buffer_allocator.h`
- `clouds/resources.h`

### Niveau 1: `granular_processor.h`

Includes directs:

- `stmlib/stmlib.h`
- `stmlib/dsp/filter.h`
- `clouds/dsp/correlator.h`
- `clouds/dsp/frame.h`
- `clouds/dsp/fx/diffuser.h`
- `clouds/dsp/fx/pitch_shifter.h`
- `clouds/dsp/fx/reverb.h`
- `clouds/dsp/granular_sample_player.h`
- `clouds/dsp/looping_sample_player.h`
- `clouds/dsp/pvoc/phase_vocoder.h`
- `clouds/dsp/sample_rate_converter.h`
- `clouds/dsp/wsola_sample_player.h`

### Niveau 2+ (includes transitifs internes)

- `granular_sample_player.h` -> `grain.h`, `audio_buffer.h`, `parameters.h`, `stmlib/dsp/atan.h`, `stmlib/dsp/units.h`, `stmlib/utils/random.h`, `clouds/resources.h`.
- `looping_sample_player.h` -> `audio_buffer.h`, `parameters.h`, `stmlib/dsp/units.h`, `clouds/resources.h`.
- `wsola_sample_player.h` -> `window.h`, `correlator.h`, `audio_buffer.h`, `parameters.h`, `stmlib/dsp/units.h`, `clouds/resources.h`.
- `phase_vocoder.h` -> `stmlib/fft/shy_fft.h`, `pvoc/stft.h`, `pvoc/frame_transformation.h`, `frame.h`.
- `pvoc/frame_transformation.cpp` -> `stmlib/dsp/atan.h`, `stmlib/dsp/units.h`, `stmlib/utils/random.h`, `frame.h`, `parameters.h`.
- `pvoc/stft.cpp` -> `stmlib/dsp/dsp.h` et option `USE_ARM_FFT`.
- `fx/*.h` -> tous basés sur `fx_engine.h`.

## 4) Architecture: où sont implémentés les blocs DSP

- Grains:
  - moteur d’ordonnancement: `GranularSamplePlayer::Play` dans `granular_sample_player.h`.
  - rendu overlap/add: `Grain` dans `grain.h`.
- Pitch:
  - pitch-shifter post-traitement: `PitchShifter` dans `fx/pitch_shifter.h`.
  - conversions pitch->ratio: `SemitonesToRatio` dans `stmlib/dsp/units.h`.
- Reverb:
  - implémentation entière inline dans `fx/reverb.h` (`Reverb::Process`).
- Diffusion:
  - implémentation inline dans `fx/diffuser.h` (`Diffuser::Process`).
- SRC:
  - template `SampleRateConverter` dans `sample_rate_converter.h`.
  - utilisation dans `GranularProcessor::Process` avec `src_down_`/`src_up_`.

## 5) Minimum build (compiler + produire du son)

### KEEP (nécessaires)

- `mutable_instruments/clouds/clouds_resources.cpp`
- `mutable_instruments/clouds/dsp/granular_processor.cpp`
- `mutable_instruments/clouds/dsp/correlator.cpp`
- `mutable_instruments/clouds/dsp/pvoc/frame_transformation.cpp`
- `mutable_instruments/clouds/dsp/pvoc/phase_vocoder.cpp`
- `mutable_instruments/clouds/dsp/pvoc/stft.cpp`
- `mutable_instruments/stmlib/dsp/atan.cpp`
- `mutable_instruments/stmlib/dsp/units.cpp`
- `mutable_instruments/stmlib/utils/random.cpp`

Raison vérifiable:

- `granular_processor.cpp` appelle `phase_vocoder_.Process`, `reverb_`, `diffuser_`, `pitch_shifter_`, `src_*`, etc.
- ces classes sont principalement implémentées inline en headers, mais certaines dépendances externes (LUT/data statiques) viennent de `.cpp`:
  - tables Clouds -> `clouds_resources.cpp`
  - `atan_lut` -> `stmlib/dsp/atan.cpp`
  - `lut_pitch_ratio_*` -> `stmlib/dsp/units.cpp`
  - `Random::rng_state_` -> `stmlib/utils/random.cpp`

### OPTIONAL

- `mutable_instruments/clouds/dsp/mu_law.cpp` (table commentée; `lut_ulaw` est définie dans `clouds_resources.cpp`).

### REMOVE (du minimum Clouds DSP)

- aucun autre `.cpp` dans `mutable_instruments/clouds/dsp/` n’existe à retirer.

## 6) Zones floues (INCONNU + vérification)

- INCONNU: preuve d’exécution bit-perfect "son identique" sans mode spectral.
  - Vérifier avec test A/B audio (même paramètres, comparaison buffers sortie).
- INCONNU: taille RAM exacte nécessaire en mode spectral pour une config cible donnée.
  - Vérifier en instrumentant `BufferAllocator::free()` après `GranularProcessor::Prepare()` sur cible.
- INCONNU: existence d’instructions de compilation conditionnelle dans d’autres branches du projet qui coupent certains modules Clouds.
  - Vérifier par `rg -n "PLAYBACK_MODE_|USE_ARM_FFT|clouds/dsp"` sur tout le repo et scripts de build cibles.
