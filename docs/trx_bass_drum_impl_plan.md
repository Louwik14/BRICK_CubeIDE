# TRXBassDrum — plan de transformation concret (incrémental, réversible)

## 1) Base de départ (version cible simplifiée)

Point de départ retenu pour `TRXBassDrum` uniquement :

- noyau minimal = `sin` + `amp_env` + `pitch_env` + transient d’attaque court,
- une seule non-linéarité finale (`Drive`) bornée,
- calculs constants sortis du sample-rate,
- paramètres centrés sur rendement musical (Pitch/Decay/Sweep/Sweep Decay/Attack + Drive borné).

## 2) Traduction concrète dans le repo actuel

Le moteur actuel est dans :

- `BRICK6_CUBE_fonctionnel/md-drum-synth-main/TRXBassDrum.h`
- `BRICK6_CUBE_fonctionnel/md-drum-synth-main/TRXBassDrum.cpp`

et son mapping runtime/paramètres dans :

- `BRICK6_CUBE_fonctionnel/Src/Audio/drum_synth.cpp`
- `BRICK6_CUBE_fonctionnel/Src/Param/param_registry.c`

La transformation est organisée en **petites étapes indépendantes** pour garder un rollback simple.

## 3) Structures/états/calcule à modifier

### 3.1 États internes à supprimer

À supprimer dans `TRXBassDrum` :

- `t` (timer flottant sample-rate) → remplacé par compteur d’attaque entier,
- `prevSample` (non utilisé dans le DSP actuel),
- état implicite lié au double chemin de saturation (`harmonics` + `clip`) après fusion.

### 3.2 États internes à conserver

À conserver :

- `phase` (oscillateur),
- `env` (amplitude) — peut être renommé `ampEnv`,
- `rampEnv` (pitch sweep) — peut être renommé `pitchEnv`.

### 3.3 Nouveaux états/constantes dérivées à ajouter

À ajouter côté `TRXBassDrum` :

- `uint16_t attackSamplesLeft` (ou `uint32_t`),
- `float ampEnvCoef`,
- `float pitchEnvCoef`,
- `float basePhaseInc`,
- `float sweepHz`,
- `float driveGain`,
- `float attackLevel` (dérivé borné de `start`),
- `float noiseLevel` (si transient noise conservé, borné),
- `bool derivedDirty` (optionnel) pour recalcul différé hors sample-rate.

### 3.4 Calculs à sortir du sample-rate

Sortie obligatoire de la boucle `Process()` :

- `exp(-1/(decay*Fs))` → `ampEnvCoef`,
- `exp(-1/(rampDecay*Fs))` → `pitchEnvCoef`,
- `pitch/Fs` → `basePhaseInc`,
- `ramp*1000` → `sweepHz` borné,
- `1 + clip*5` (ou nouvelle loi drive) → `driveGain`,
- `0.01 s` de fenêtre attaque → `attackSamplesLeft` au trigger.

### 3.5 Paramètres à borner / figer

Dans le plan de transition :

- **Fusion** `harmonics` + `clip` vers un seul axe `Drive` (court terme : mapper les deux vers `driveGain`; moyen terme : figer `harmonics=0`),
- borner `start` vers une plage utile (ex. `[0, 1.2]`),
- borner `noise` vers une plage plus conservatrice (ex. `[0, 0.35]`) si conservé,
- borner `ramp` pour empêcher extrêmes peu musicaux/coûteux,
- garder `pitch`, `decay`, `rampDecay` mais avec clamp strict côté modèle.

## 4) Ordre de modification recommandé (TRXBassDrum seulement)

## Étape 1 — Externaliser les constantes d’enveloppe et dérivés hors sample-rate

- **Objectif** : supprimer le coût structurel le plus évident sans changer la topologie sonore.
- **Ce qui change** : ajout d’une routine interne `UpdateDerived()` ; `Process()` n’appelle plus `exp`.
- **Pourquoi maintenant** : faible risque sonore et gain CPU immédiat.
- **Risque sonore attendu** : faible (quasi nul si mêmes formules).
- **Gain CPU attendu** : moyen à fort (suppression de 2 `exp`/sample).

## Étape 2 — Remplacer `t` par compteur d’attaque entier

- **Objectif** : simplifier la gestion de la fenêtre d’attaque et préparer le transient minimal.
- **Ce qui change** : suppression `t`, ajout `attackSamplesLeft` décrémenté par sample.
- **Pourquoi ici** : dépend peu des autres changements et réduit la logique flottante temporelle.
- **Risque sonore attendu** : faible.
- **Gain CPU attendu** : faible à moyen.

## Étape 3 — Fusionner les non-linéarités en un seul `Drive`

- **Objectif** : réduire la richesse structurelle inutile et les doubles chemins de couleur.
- **Ce qui change** : un seul étage de saturation final ; `harmonics` neutralisé/mappé vers drive en transition.
- **Pourquoi ici** : après stabilisation de base, pour isoler l’impact timbral.
- **Risque sonore attendu** : moyen.
- **Gain CPU attendu** : moyen (moins de branches + moins de `tanh` potentiels).

## Étape 4 — Borner/figer les paramètres secondaires

- **Objectif** : bornage low-cost et robustesse comportementale.
- **Ce qui change** : clamps explicites dans `SetParamByIndex` + mappings resserrés côté runtime.
- **Pourquoi ici** : après fusion drive, pour éviter cumul de changements timbraux simultanés.
- **Risque sonore attendu** : moyen (limitation d’extrêmes).
- **Gain CPU attendu** : faible direct, fort indirect (coût borné garanti).

## Étape 5 — Stabilisation niveau/sortie et nettoyage final des états

- **Objectif** : finaliser un moteur simple, borné, répétable.
- **Ce qui change** : suppression des champs obsolètes (`prevSample`, éventuel `harmonics`), trim de sortie fixe si nécessaire.
- **Pourquoi ici** : étape de consolidation après validation audio des étapes 1–4.
- **Risque sonore attendu** : faible à moyen.
- **Gain CPU attendu** : faible à moyen (nettoyage + branches en moins).

## 5) Modifications concrètes, fichier par fichier (sans patch complet)

## A) `BRICK6_CUBE_fonctionnel/md-drum-synth-main/TRXBassDrum.h`

### Membres à modifier

- **Supprimer** : `t`, `prevSample`.
- **Conserver** : `phase`, `env`, `rampEnv`.
- **Ajouter** :
  - `attackSamplesLeft`,
  - `ampEnvCoef`, `pitchEnvCoef`,
  - `basePhaseInc`, `sweepHz`, `driveGain`,
  - `attackLevel`, `noiseLevel`,
  - `derivedDirty` (optionnel).

### Paramètres

- Transition paramétrique :
  - conserver les 8 indices pour compatibilité immédiate,
  - mais préparer la fusion logique `harmonics`+`clip` vers `drive` interne.

### Méthodes

- Ajouter méthode privée : `void UpdateDerived();`
- Ajouter helper clamp/mapping local (privé, simple, inlineable).

## B) `BRICK6_CUBE_fonctionnel/md-drum-synth-main/TRXBassDrum.cpp`

### Méthodes à toucher

- `Init()` : initialiser nouveaux états dérivés/compteurs.
- `Trigger()` :
  - recharger `env/rampEnv`,
  - initialiser `attackSamplesLeft`,
  - appeler `UpdateDerived()` si nécessaire.
- `SetParamByIndex(...)` (défini dans le header aujourd’hui) :
  - ajouter clamps stricts,
  - marquer `derivedDirty=true`.
- `Process()` :
  - retirer calculs `exp` per-sample,
  - utiliser `ampEnvCoef/pitchEnvCoef/basePhaseInc/sweepHz/driveGain`,
  - transient via compteur entier,
  - un seul étage de saturation final.
- `sine(...)` : inchangé dans un premier temps (pas de micro-optimisation cosmétique).

### Nettoyage

- retirer branches obsolètes liées à l’ancienne double couleur (`harmonics` séparé).

## C) `BRICK6_CUBE_fonctionnel/Src/Audio/drum_synth.cpp`

### Zone à toucher

- Switch `DRUM_MODEL_ID_TRX_BD` dans `drum_synth_set_param_for_instance(...)`.

### Changements

- court terme : conserver le mapping existant (8 paramètres) pour migration douce,
- moyen terme : rediriger `HARMONICS` et `DRIVE` vers un seul axe interne `Drive` (ou figer l’un des deux).

## D) `BRICK6_CUBE_fonctionnel/Src/Param/param_registry.c`

### Zone à toucher

- définitions `PARAM_DRUM_TRX_BD_*`.

### Changements

- phase 1 : conserver l’API actuelle pour non-régression UI/automation,
- phase 2 : resserrer min/max des paramètres secondaires (`ATTACK`, `NOISE`, éventuellement `HARM`) selon bornage final.

## 6) Meilleure première étape à implémenter immédiatement

**Étape à lancer maintenant : Étape 1 (externalisation des dérivés hors sample-rate).**

### Justification

- c’est le meilleur ratio **gain CPU / risque sonore**,
- elle prépare toutes les étapes suivantes sans verrouiller le design,
- elle est hautement réversible (rollback local à `TRXBassDrum`),
- elle reste strictement dans la simplification structurelle de ce moteur.

### Livrable attendu de cette première étape

- `Process()` sans `exp` per-sample,
- `UpdateDerived()` appelé sur trigger/changement paramètre,
- comportement sonore quasi identique à iso-paramètres (hors très légères différences numériques).
