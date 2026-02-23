# Clouds ciblé: rôle de `low_fidelity_`, `src_down_`, `src_up_`

## A: définition

### `low_fidelity_` — définition / modification / usages

| élément | preuve |
|---|---|
| Déclaration membre `bool low_fidelity_` | `mutable_instruments/clouds/dsp/granular_processor.h:174` |
| Valeur init par défaut `false` dans `Init()` | `mutable_instruments/clouds/dsp/granular_processor.cpp:53` |
| Modifiée par setter public `set_low_fidelity(bool)` | `mutable_instruments/clouds/dsp/granular_processor.h:137-140` |
| Modifiée indirectement par `set_quality(int32_t)` (`quality >> 1`) | `mutable_instruments/clouds/dsp/granular_processor.h:127-130` |
| Utilisée pour encoder `quality()` | `mutable_instruments/clouds/dsp/granular_processor.h:142-146` |
| Utilisée pour choisir résolution 8/16 bits (`resolution()`) | `mutable_instruments/clouds/dsp/granular_processor.h:154-156` |
| Utilisée pour ajuster sample-rate effectif (`sample_rate()/kDownsamplingFactor`) | `mutable_instruments/clouds/dsp/granular_processor.h:163-166` |
| Utilisée dans branche DSP runtime `if (low_fidelity_)` de `Process()` | `mutable_instruments/clouds/dsp/granular_processor.cpp:204-211` |
| Utilisée pour persistent state (head 8-bit vs 16-bit) | `mutable_instruments/clouds/dsp/granular_processor.cpp:289-292`, `357-363` |
| Utilisée dans `Prepare()` pour calcul `num_grains` | `mutable_instruments/clouds/dsp/granular_processor.cpp:442-443` |

### `src_down_` / `src_up_`

| élément | preuve |
|---|---|
| Déclaration `src_down_` | `mutable_instruments/clouds/dsp/granular_processor.h:213` |
| Déclaration `src_up_` | `mutable_instruments/clouds/dsp/granular_processor.h:214` |
| Type: `SampleRateConverter<-kDownsamplingFactor, 45, src_filter_1x_2_45>` (down) | `mutable_instruments/clouds/dsp/granular_processor.h:213` |
| Type: `SampleRateConverter<+kDownsamplingFactor, 45, src_filter_1x_2_45>` (up) | `mutable_instruments/clouds/dsp/granular_processor.h:214` |
| Initialisation des deux convertisseurs dans `Init()` | `mutable_instruments/clouds/dsp/granular_processor.cpp:56-57` |
| Usage runtime dans branche low fidelity | `mutable_instruments/clouds/dsp/granular_processor.cpp:206`, `208` |

---

## B: pipeline

Code exact dans `GranularProcessor::Process`:

```cpp
if (low_fidelity_) {
  size_t downsampled_size = size / kDownsamplingFactor;
  src_down_.Process(in_, in_downsampled_,size);
  ProcessGranular(in_downsampled_, out_downsampled_, downsampled_size);
  src_up_.Process(out_downsampled_, out_, downsampled_size);
} else {
  ProcessGranular(in_, out_, size);
}
```

Preuve: `mutable_instruments/clouds/dsp/granular_processor.cpp:204-211`.

Ce qui change quand `low_fidelity_ == true` (preuves strictes):

1. Taille de traitement granular divisée par `kDownsamplingFactor` (`downsampled_size = size / ...`).
2. Ajout d’un pré-traitement SRC: `src_down_.Process(...)`.
3. `ProcessGranular` est exécutée sur buffers downsampled (`in_downsampled_`, `out_downsampled_`).
4. Ajout d’un post-traitement SRC: `src_up_.Process(...)`.

Preuves: `granular_processor.cpp:205-208`.

Fonctions appelées en plus vs mode normal:

- `SampleRateConverter::Process` pour `src_down_` et `src_up_`.
  - preuve appel: `granular_processor.cpp:206`, `208`
  - preuve implémentation: `sample_rate_converter.h:52-84`

---

## C: CPU impact

Boucles supplémentaires introduites (uniquement branche low fidelity):

- Boucle principale `while (input_size)` dans `SampleRateConverter::Process`.
- Boucle de consommation `for (i=0; i<consumed; ++i)`.
- Boucle de production `for (i=0; i<produced; ++i)`.
- Boucle FIR interne `for (j=i; j<filter_size; j+=produced)` avec `filter_size=45` (depuis type membre).

Preuves:

- structure des boucles: `sample_rate_converter.h:56-82`.
- `filter_size=45` pour `src_down_` et `src_up_`: `granular_processor.h:213-214`.

Complexité ajoutée (basée sur structure de boucles, sans benchmark):

- Branche low fidelity exécute **2 passes SRC** supplémentaires (`down` + `up`) autour de `ProcessGranular`.
- Chaque pass SRC est O(N) en nombre de frames traitées, avec coût FIR interne borné par `filter_size`.
- Donc charge additionnelle = coût SRC_down + coût SRC_up, absente en branche `else`.

Preuves: `granular_processor.cpp:204-211` + `sample_rate_converter.h:56-82`.

---

## D: audio impact

Ce que fait le SRC d’après le code:

1. **Filtrage FIR**
   - convolution via coefficients `coefficients_[j]` dans boucle interne, somme `y_l/y_r`.
   - preuve: `sample_rate_converter.h:72-76`.

2. **Downsample path (`ratio < 0`)**
   - `consumed = -ratio`, `produced = 1`, `scale = 1.0f`.
   - pour `src_down_`, ratio = `-kDownsamplingFactor`.
   - preuve formule: `sample_rate_converter.h:55`, `57`, `67`; type membre: `granular_processor.h:213`.

3. **Upsample path (`ratio > 0`)**
   - `consumed = 1`, `produced = ratio`, `scale = float(ratio)`.
   - pour `src_up_`, ratio = `+kDownsamplingFactor`.
   - preuve formule: `sample_rate_converter.h:55`, `57`, `67`; type membre: `granular_processor.h:214`.

4. `kDownsamplingFactor` vaut 2.
   - preuve: `granular_processor.h:49`.

Donc (strictement d’après code):

- `src_down_` réalise un changement de taux avec FIR et consommation par paquets de 2 (ratio -2).
- `src_up_` réalise l’opération inverse avec FIR et production x2 (ratio +2).

---

## E: safe override ? (`low_fidelity_ = false`)

### Ce que le code prouve

- Mettre `low_fidelity_` à `false` désactive la branche SRC et force `ProcessGranular(in_, out_, size)` direct.
  - preuve: `granular_processor.cpp:204-211`.
- `set_low_fidelity(false)` déclenche `reset_buffers_` si changement de valeur, ce qui force réinitialisation propre au `Prepare()` suivant.
  - preuve: `granular_processor.h:137-139`, `granular_processor.cpp:385-449`.
- Le reste du code gère explicitement les deux chemins (`buffer_8_` vs `buffer_16_`, persistence heads, quality bit).
  - preuves: `granular_processor.cpp:289-292`, `357-363`, `429-440`; `granular_processor.h:142-156`.

### Limite de preuve

- **INCONNU**: “safe” au sens qualité/perf globale système STM32 IRQ sans test runtime cible.
- Comment vérifier:
  1. Forcer `set_low_fidelity(false)` avant `Prepare()`.
  2. Mesurer CPU load IRQ et glitchs audio sur cible.
  3. Vérifier save/load state (`PreparePersistentData` / `LoadPersistentData`) après switch de qualité.

