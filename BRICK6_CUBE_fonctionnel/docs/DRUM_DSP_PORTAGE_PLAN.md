# Portage DSP Drum (`md-drum-synth-main`) vers firmware STM32H743

## Contexte et périmètre de cette passe

Ce document définit un **plan de portage minimal, structuré et track-aware** des moteurs drum DSP présents dans `md-drum-synth-main`, sans redesign global et sans mélange prématuré runtime/UI.

Objectifs de cette passe:
- isoler le périmètre DSP réellement portable;
- qualifier les paramètres moteur par moteur;
- proposer une première normalisation UX (`TONE` / `COLORS`);
- séquencer une intégration incrémentale compatible hard real-time STM32H743 (pas d’allocation dynamique dans le thread audio).

---

## 1) Fichiers à conserver pour un portage DSP minimal

### 1.1 Noyau d’interface (à adapter côté firmware)

- `md-drum-synth-main/DrumModel.h`

> Remarque: `RenderControls()` + sérialisation `std::iostream` ne sont pas adaptées telles quelles à l’embarqué; conserver l’idée d’interface `Init/Trigger/Process`, mais découpler UI/sérialisation.

### 1.2 Moteurs DSP drum (cœur audio)

- `TRXBassDrum.cpp/.h`
- `TRXClaves.cpp/.h`
- `TRXHiHat.cpp/.h`
- `TRXSnareDrum.cpp/.h`
- `FmKickModel.cpp/.h`
- `FmSnareModel.cpp/.h`
- `FmTomModel.cpp/.h`
- `FmRimshotModel.cpp/.h`
- `FmClapModel.cpp/.h`
- `FmCowbellModel.cpp/.h`
- `FmCymbalModel.cpp/.h`

### 1.3 Dépendances utilitaires potentiellement réutilisables

Ces fichiers sont utiles **uniquement** pour les moteurs FM qui utilisent `plaits::fm::Operator`:
- `mi/operator.h`
- `mi/sine_oscillator.h`
- `mi/dsp.h`
- `mi/stmlib.h`

> Les autres fichiers `mi/*` (DX voice/patch/resources/lfo...) ne sont pas requis pour ce portage drum minimal.

### 1.4 Périmètre minimal recommandé (v1)

- 1 interface drum runtime interne (sans UI desktop):
  - `init(sample_rate)`
  - `trigger(velocity)`
  - `process()` mono sample
  - `set_param(id, value_norm)`
- 1 premier moteur simple pour valider la chaîne (voir stratégie étape 2).
- 1 mapping param -> `TONE`/`COLORS` limité aux paramètres musicaux.

---

## 2) Fichiers à exclure du portage embarqué (hors moteur sonore)

### 2.1 UI desktop / OpenGL / audio PC

- `main.cpp`
- `CustomControls.cpp/.h`
- `glad.c`, `glad.h`, `khrplatform.h`
- `stb_image.h`
- `CMakeLists.txt`

### 2.2 Ressources démo / média

- `resources/background.png`
- `resources/background_png.h`
- `resources/drum_params.txt`
- `resources/README.md`
- `md-drum-synth.jpg`
- `md-drum-synth-examples.mp3`

### 2.3 Dépendances MI non nécessaires au drum minimal

- `mi/algorithms.*`
- `mi/dx_units.*`
- `mi/envelope.h`
- `mi/lfo.h`
- `mi/parameter_interpolator.h`
- `mi/patch.h`
- `mi/resources.*`
- `mi/rsqrt.h`
- `mi/voice.h`

---

## 3) Analyse moteur par moteur: paramètres utiles vs techniques

## 3.1 TRXBassDrum

Paramètres source: `pitch`, `decay`, `ramp`, `rampDecay`, `start`, `noise`, `harmonics`, `clip`.

- **Essentiels (utilisateur)**
  - `pitch` (accord/taille de kick)
  - `decay` (longueur)
  - `ramp` (attaque/punch de pitch)
- **Secondaires intéressants**
  - `noise` (click/attaque)
  - `harmonics` (corps/grit)
  - `clip` (drive)
- **À cacher en v1**
  - `rampDecay` (fin mais redondant avec `ramp`+`decay`)
  - `start` (gain d’attaque interne, risque de confusion avec volume/velocity)

## 3.2 TRXClaves

Paramètres source: `pitch`, `interval`, `decay`, `balance`, `clip`.

- **Essentiels**
  - `pitch`
  - `decay`
  - `interval` (identité harmonique du timbre)
- **Secondaires**
  - `balance` (couleur entre les 2 oscillateurs)
  - `clip`
- **À cacher v1**
  - aucun obligatoire (moteur déjà compact)

## 3.3 TRXHiHat

Paramètres source: `gap`, `decay`, `lpfFreq`, `hpfFreq`, `peak`, `metal`.

- **Essentiels**
  - `decay`
  - `metal` (bruit blanc vs composante métallique)
  - `hpfFreq` (brillance)
- **Secondaires**
  - `lpfFreq`
  - `gap` (comportement type open/closed simplifié)
- **À cacher v1**
  - `peak` (actuellement non exploité dans le DSP: paramètre trompeur)

## 3.4 TRXSnareDrum

Paramètres source: `pitch`, `decay`, `snap`, `noise`, `tone`, `tune`, `bump`, `clip`.

- **Essentiels**
  - `pitch`
  - `decay`
  - `snap`
  - `noise`
- **Secondaires**
  - `tone` (balance entre 2 osc)
  - `clip`
- **À cacher v1**
  - `tune` (intervalle secondaire, peu évident)
  - `bump` (micro-transitoire technique)

## 3.5 FmKickModel

Paramètres source: `f_b`, `d_b`, `f_m`, `I`, `d_m`, `b_m`, `A_f`, `d_f`, `use_ratio_mode`, `ratio_index`, `mod_env_sync`.

- **Essentiels**
  - `f_b` (pitch)
  - `d_b` (decay)
  - `I` (attaque FM)
  - `A_f` (pitch sweep)
- **Secondaires**
  - `d_f`
  - `f_m` (ou ratio verrouillé)
  - `d_m`
  - `b_m` (feedback/roughness)
- **À cacher v1**
  - `use_ratio_mode`, `ratio_index` (UI experte)
  - `mod_env_sync` (technique)

## 3.6 FmSnareModel

Paramètres source: `f_b`, `d_b`, `f_m`, `I`, `d_m`, `Abrus`, `dbrus`, `fhp`.

- **Essentiels**
  - `f_b`
  - `d_b`
  - `Abrus` (niveau de bruit)
  - `fhp` (teinte du bruit)
- **Secondaires**
  - `I`
  - `f_m`
  - `d_m`
  - `dbrus`
- **À cacher v1**
  - aucun strictement, mais `f_m`+`d_m` peuvent être groupés en macro “FM attack”.

## 3.7 FmTomModel

Paramètres source: `f_b`, `d_b`, `f_m`, `I`, `d_m`, `A_f`, `d_f`, `start_phase`.

- **Essentiels**
  - `f_b`
  - `d_b`
  - `A_f`
- **Secondaires**
  - `I`, `f_m`, `d_m`, `d_f`
- **À cacher v1**
  - `start_phase` (calibration de transitoire, peu musical en façade)

## 3.8 FmRimshotModel

Paramètres source: `f_bB`, `d_bB`, `I_B`, `f_bA`, `d_bA`, `I_A`, `A_A`, `d_m`, `f_hp`.

- **Essentiels**
  - `f_bB` (rim principal)
  - `d_bB`
  - `A_A` (mix body)
- **Secondaires**
  - `f_bA`, `d_bA`
  - `I_B`, `I_A`
  - `f_hp`
- **À cacher v1**
  - `d_m` (second ordre)

## 3.9 FmClapModel

Paramètres source: `f_b`, `f_m`, `I`, `d_m`, `d1`, `d2`, `clap_count`, `clap_interval`, `fhp`, `bm`.

- **Essentiels**
  - `clap_count`
  - `clap_interval`
  - `d2` (tail)
  - `fhp`
- **Secondaires**
  - `d1`
  - `I`, `f_m`, `bm`
- **À cacher v1**
  - `f_b` (peu déterminant musicalement comparé aux paramètres de pattern de claps)
  - `d_m` (technique FM interne)

## 3.10 FmCowbellModel

Paramètres source: `fbA`, `d_b1`, `db2`, `fm`, `I`, `dm`, `bm`, `Ab1`.

- **Essentiels**
  - `fbA` (pitch de base)
  - `d_b1` / `db2` (double décroissance caractéristique)
  - `I`
- **Secondaires**
  - `fm`, `bm`, `Ab1`
- **À cacher v1**
  - `dm` (détail FM)

## 3.11 FmCymbalModel

Paramètres source: `fb`, `fm`, `d_b`, `I`, `d_m`, `bb`, `sustain`, `f_hp`.

- **Essentiels**
  - `d_b`
  - `sustain`
  - `f_hp`
  - `I`
- **Secondaires**
  - `fb`, `fm`, `d_m`, `bb`
- **À cacher v1**
  - aucun obligatoire

---

## 4) Proposition de classement des paramètres entre `TONE` et `COLORS`

Règle retenue:
- `TONE` = identité de génération (pitch, ratio FM, index FM, structure de clap, intervalles osc)
- `COLORS` = teinte/filtrage/shaping (HPF/LPF, noise balance, clip/drive, feedback timbral)

## 4.1 Mapping générique (normalisation)

- Renommages UX conseillés:
  - `f_b` / `fb` / `fbA` -> `Pitch`
  - `d_b` / `decay` -> `Decay`
  - `I` -> `FM Amount`
  - `A_f` -> `Pitch Sweep`
  - `d_f` -> `Sweep Decay`
  - `Abrus` -> `Noise`
  - `fhp` / `f_hp` -> `HP Tone`
  - `clip` -> `Drive`
  - `bm` / `bb` / `b_m` -> `Feedback`

## 4.2 Mapping par moteur (v1)

- **TRXBassDrum**
  - `TONE`: `Pitch`, `Decay`, `Pitch Sweep`
  - `COLORS`: `Noise`, `Harmonics`, `Drive`
  - hors v1: `Ramp Decay`, `Start`

- **TRXClaves**
  - `TONE`: `Pitch`, `Interval`, `Decay`
  - `COLORS`: `Balance`, `Drive`

- **TRXHiHat**
  - `TONE`: `Decay`, `Metal`
  - `COLORS`: `HP Tone`, `LP Tone`
  - hors v1: `Gap` (peut devenir contrôle contextuel “Open/Close”), `Peak` (non utilisé)

- **TRXSnareDrum**
  - `TONE`: `Pitch`, `Decay`, `Snap`
  - `COLORS`: `Noise`, `Tone Mix`, `Drive`
  - hors v1: `Tune Interval`, `Bump`

- **FmKickModel**
  - `TONE`: `Pitch`, `Decay`, `FM Amount`, `Pitch Sweep`
  - `COLORS`: `Feedback`
  - hors v1: `Ratio mode/index`, `mod_env_sync`, `Mod Decay`, `Sweep Decay`

- **FmSnareModel**
  - `TONE`: `Pitch`, `Decay`, `FM Amount`
  - `COLORS`: `Noise`, `HP Tone`
  - secondaire: `Mod Freq`, `Mod Decay`, `Noise Decay`

- **FmTomModel**
  - `TONE`: `Pitch`, `Decay`, `Pitch Sweep`, `FM Amount`
  - `COLORS`: (optionnel) `Attack Character` (macro interne via `f_m/d_m`)
  - hors v1: `Start Phase`

- **FmRimshotModel**
  - `TONE`: `Rim Pitch`, `Rim Decay`, `Body Mix`
  - `COLORS`: `HP Tone`
  - hors v1: exposer séparément tous les indices A/B (trop bas niveau)

- **FmClapModel**
  - `TONE`: `Clap Count`, `Clap Spacing`, `Tail Decay`
  - `COLORS`: `HP Tone`, `Feedback`, `FM Amount`
  - hors v1: `Base Freq`, `Mod Decay`

- **FmCowbellModel**
  - `TONE`: `Pitch`, `Decay Short`, `Decay Long`, `FM Amount`
  - `COLORS`: `Feedback`, `Env Mix`
  - hors v1: `Mod Decay`

- **FmCymbalModel**
  - `TONE`: `Decay`, `Sustain`, `FM Amount`
  - `COLORS`: `HP Tone`, `Feedback`
  - secondaire: `Base Carrier`, `Base Mod`

### 4.3 Paramètres qui ne rentrent pas proprement `TONE/COLORS`

- `clap_count`, `clap_interval`: ce sont des paramètres de **structure temporelle** (presque “PLAY/SEQ”), mais gardables temporairement en `TONE` pour éviter d’ouvrir un 3e domaine.
- `use_ratio_mode`, `ratio_index`: paramètres d’édition experte (quasi “advanced”), à cacher en v1.
- `start_phase`, `start`: plutôt calibration/attack-shape interne.

---

## 5) Bases communes et couplages desktop à signaler

## 5.1 Bases communes entre moteurs

- Tous les moteurs partagent le pattern runtime `Init / Trigger / Process`.
- Les moteurs FM (`Fm*`) partagent:
  - enveloppes de décroissance;
  - logique opérateur FM;
  - feedback FM;
  - formes de filtre HP simples.
- `FmKickModel` et `FmSnareModel` utilisent `plaits::fm::Operator` (`mi/operator.h`).
- `FmTomModel`, `FmRimshotModel`, `FmClapModel`, `FmCowbellModel`, `FmCymbalModel` utilisent un FM sinus “manuel” (pas `plaits::fm::Operator`).

## 5.2 Couplages desktop à neutraliser explicitement

- Tous les `.cpp` moteur incluent soit `imgui.h`, soit `CustomControls.h` via `RenderControls()`.
- `DrumModel.h` impose `RenderControls()` et sérialisation `iostream`.
- `TRXHiHat` utilise `std::default_random_engine` + `std::random_device` pour le seed (non déterministe et peu contrôlable en embarqué).

Portage recommandé:
- conserver le DSP pur dans une couche runtime sans dépendances UI;
- remplacer le bruit aléatoire par PRNG embarqué déterministe, stateful par instance track.

---

## 6) Stratégie de portage incrémentale (5 étapes)

## Étape 1 — Socle commun minimal (hébergement drum)

- Ajouter une interface runtime drum **interne au firmware** (pas celle desktop) avec:
  - cycle de vie: `init(sr)`, `note_on/trigger`, `process_sample`;
  - param API normalisée (IDs fixes, valeurs normalisées 0..1);
  - état sans allocation dynamique dans audio thread.
- Créer une table de capacité moteur (mono/poly futur, nb params exposés, pages `TONE/COLORS`).
- Connecter la résolution track-aware (`family/type -> runtime engine`) sans casser l’autorité `track_runtime`.

## Étape 2 — Premier moteur preuve de portage

Moteur conseillé: **TRXBassDrum**.

Pourquoi:
- simple, lisible, faible dépendance;
- bon signal de validation de trigger/enveloppe/pitch sweep;
- permet d’éprouver immédiatement le mapping `TONE/COLORS`.

Exposition v1 recommandée:
- `TONE`: `Pitch`, `Decay`, `Pitch Sweep`
- `COLORS`: `Noise`, `Drive`

## Étape 3 — Généralisation aux autres moteurs

Ordre recommandé:
1. `TRXSnareDrum` (complète kick/snare de base)
2. `TRXHiHat` (ajoute bruit+filtrage)
3. `TRXClaves` (percussif tonal)
4. bloc FM (`FmKick`, `FmSnare`, puis autres)

Pour chaque ajout:
- garder un preset par défaut “musical”;
- limiter d’abord l’UI à 4–6 paramètres max.

## Étape 4 — Harmonisation param/UI

- Unifier noms et plages (Pitch/Decay/Noise/Drive/HP Tone/FM Amount...).
- Éviter les doublons de sens entre moteurs (ex. plusieurs “decay” non comparables -> normalisation perceptive).
- Définir clairement ce qui est caché (advanced/calibration).

## Étape 5 — Optimisation STM32H743

- Remplacer `std::exp` fréquent dans `Process()` par décays itératifs pré-calculés au `Trigger` quand possible.
- Réduire coût trigonométrique (`sinf`) si nécessaire (LUT/approx ciblées, uniquement après mesure).
- Vérifier clipping interne/headroom fixe pour éviter saturations non contrôlées.
- Vérifier coût CPU par moteur et budget poly/voices en conditions réelles.
- Valider déterminisme temporel (pas d’aléa système ni allocations).

---

## 7) Première passe de code minimale (recommandation)

Pour cette passe, **ne pas intégrer tout de suite tous les moteurs**.

Minimum structurant suggéré:
1. créer un adaptateur d’interface drum runtime firmware (abstraction DSP pure);
2. porter uniquement `TRXBassDrum` (sans `RenderControls`, sans iostream);
3. brancher 5 paramètres max (`Pitch`, `Decay`, `Pitch Sweep`, `Noise`, `Drive`) dans `TONE/COLORS`.

Cette approche valide l’architecture sans dette UI prématurée et sans casser la cohérence existante (`DX7`, `MonoB`, `TB3`, tracks input).
