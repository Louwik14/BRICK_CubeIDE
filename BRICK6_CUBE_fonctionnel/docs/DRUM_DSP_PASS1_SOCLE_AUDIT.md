# Drum DSP portage — Passe 1 (socle commun + audit de réutilisation)

Référence de décision: `docs/DRUM_DSP_PORTAGE_PLAN.md`.

## 1) Audit de réutilisation réel (fichier par fichier)

## 1.1 Réutilisable depuis `mutable_instruments`

Conservé comme noyau DSP commun cible (compatibilité prouvée sur la chaîne FM utilisée par `FmKick`/`FmSnare`, sous réserve d’AB audio en passe 2):

- `mutable_instruments/plaits/dsp/fm/operator.h`
- `mutable_instruments/plaits/dsp/oscillator/sine_oscillator.h`
- `mutable_instruments/stmlib/dsp/dsp.h`
- `mutable_instruments/stmlib/dsp/parameter_interpolator.h`
- `mutable_instruments/stmlib/dsp/rsqrt.h`
- `mutable_instruments/stmlib/stmlib.h`
- `mutable_instruments/plaits/resources.h`

Motif: même rôle fonctionnel que la chaîne `md-drum-synth-main/mi/*` identifiée dans l’audit de preuve, avec base déjà portée dans ce repo.

## 1.2 À garder côté `md-drum-synth-main` pour fidélité sonore

À conserver comme autorité sonore drum tant que le portage moteur n’est pas validé par AB test:

- `TRXBassDrum.*`
- `TRXClaves.*`
- `TRXHiHat.*`
- `TRXSnareDrum.*`
- `FmKickModel.*`
- `FmSnareModel.*`
- `FmTomModel.*`
- `FmRimshotModel.*`
- `FmClapModel.*`
- `FmCowbellModel.*`
- `FmCymbalModel.*`

Motif: ce sont les implémentations DSP de référence actuellement auditées (enveloppes, ratios, HPF, ordre d’accumulation, politique RNG), donc sensibles au son.

## 1.3 Doublons / divergences identifiés

Comparaison binaire: `md-drum-synth-main/mi/*` vs homologues `mutable_instruments/*` → **fichiers divergents** (pas de copie identique) pour:

- `operator.h`
- `sine_oscillator.h`
- `dsp.h`
- `parameter_interpolator.h`
- `rsqrt.h`
- `stmlib.h`
- `resources.h`
- `resources.cc`

Décision conservatrice passe 1:
- ne pas remplacer les moteurs drum de référence;
- préparer le découplage UI/desktop sans toucher les équations DSP.

## 2) Socle DSP commun posé dans cette passe

## 2.1 Découplage desktop/UI

Ajout d’un pont de compilation:
- `md-drum-synth-main/DrumUiAbstraction.h`
  - macro `MD_DRUM_HAS_DESKTOP_UI` (défaut `0`).

Effet:
- `imgui` et `CustomControls` ne sont plus des dépendances obligatoires de compilation DSP;
- les fonctions `RenderControls()` restent conservées, mais neutralisées quand UI desktop désactivée.

## 2.2 Contrat modèle drum allégé pour l’embarqué

`md-drum-synth-main/DrumModel.h`:
- passage de `<iostream>` à `<iosfwd>`;
- `RenderControls()` devient optionnel (implémentation vide par défaut);
- hooks `saveParameters/loadParameters` deviennent optionnels (no-op par défaut).

Objectif:
- garder le contrat `Init/Trigger/Process` intact;
- retirer l’obligation de dépendances host/UI pour compiler le DSP.

## 2.3 Fichiers neutralisés / découplés

Découplage appliqué sur:
- `TRXBassDrum.cpp`
- `TRXClaves.cpp`
- `TRXHiHat.cpp`
- `TRXSnareDrum.cpp`
- `FmKickModel.cpp`
- `FmSnareModel.cpp`
- `FmTomModel.cpp`
- `FmRimshotModel.cpp`
- `FmClapModel.cpp`
- `FmCowbellModel.cpp`
- `FmCymbalModel.cpp`

Pattern appliqué:
- include `DrumUiAbstraction.h`;
- includes `imgui`/`CustomControls` sous garde `#if MD_DRUM_HAS_DESKTOP_UI`;
- `RenderControls()` gardé mais inactif quand macro = 0.

## 3) Doc architecture vérifiée / ajustée

Mises à jour minimales:
- `AGENT.md`
- `readme.md`
- `docs/ARCHITECTURE_GLOBAL.md`

Alignement explicite apporté:
- `Drum` = family distincte de `Synth`;
- `Drum` n’est pas un alias `Synth`;
- `DX7/MonoB/TB3` restent types `Synth`;
- `Drum` est documentée comme family réservée/en attente d’engines dédiés;
- la logique mono/poly de `PLAY` reste pilotée par capacité runtime centrale.

## 4) Fichiers `md-drum-synth-main` potentiellement supprimables plus tard

Potentiellement supprimables **après** migration complète vers socle embarqué + validation AB:

- host desktop/demo:
  - `main.cpp`
  - `glad.c`, `glad.h`, `khrplatform.h`
  - `stb_image.h`
  - `resources/background.png`, `resources/background_png.h`
  - médias/doc demo (`md-drum-synth.jpg`, `md-drum-synth-examples.mp3`)
- UI desktop:
  - `CustomControls.h`, `CustomControls.cpp`

À conserver tant que non migré/validé:
- moteurs drum `TRX*`/`Fm*`;
- chaîne `mi/*` utilisée par ces moteurs.

## 5) Invariants impactés

Préservés:
- logique track-aware existante (aucun remap runtime global ajouté);
- aucune réaffectation de paramètres Drum vers DX7/TB3/MonoB;
- aucune exposition UI produit `TONE/COLORS` drum ajoutée;
- aucune intégration scheduler/clavier/navigation large.

Nouveau garde-fou:
- compilation DSP drum possible sans dépendance obligatoire à `imgui`/desktop.

## 6) Reste à faire en passe 2

- brancher les moteurs drum dans le runtime produit, track-aware, sans alias `Synth`;
- finaliser la sélection de la source FM commune (`mutable_instruments` vs `md/mi`) moteur par moteur après AB tests;
- introduire les paramètres drum produit dédiés (sans mapping provisoire sur params synth);
- exposer UI `TONE/COLORS` drum réelle, uniquement après contrat param/routing validé;
- préparer la suppression finale des restes desktop `md-drum-synth-main` devenus inutiles.
