# Drumboy FX DSP Extract

Provenance exacte:
`C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6\Inspiration\Drumboy-main\02 Software\Drumboy-App-H723\Core\Library`

Cette extraction separe le DSP FX initialement imbrique dans `Controller.cpp` et `Global.h`. Les fichiers `Controller.cpp` et `Global.h` restent copies ici uniquement comme reference brute.

## Fichiers copies bruts

- `Controller.cpp`
  - Source: `Core\Library\Controller\Controller.cpp`
  - Role: reference originale de `Controller::processAudioEffect(...)`.
- `Global.h`
  - Source: `Core\Library\Global\Global.h`
  - Role: reference originale des structures, constantes et tables Drumboy.

## Fichiers separes crees

- `delay_dsp.h`, `delay_dsp.cpp`
- `chorus_dsp.h`, `chorus_dsp.cpp`
- `flanger_dsp.h`, `flanger_dsp.cpp`
- `phaser_dsp.h`, `phaser_dsp.cpp`
- `dynamics_dsp.h`, `dynamics_dsp.cpp`
- `drive_dsp.h`, `drive_dsp.cpp`
- `bitcrusher_dsp.h`, `bitcrusher_dsp.cpp`

## Mapping effet -> fichier

| Effet | Fichiers | Fonction process |
| --- | --- | --- |
| Delay | `delay_dsp.h/.cpp` | `delayProcessSample(...)` |
| Chorus | `chorus_dsp.h/.cpp` | `chorusProcessSample(...)` |
| Flanger | `flanger_dsp.h/.cpp` | `flangerProcessSample(...)` |
| Phaser | `phaser_dsp.h/.cpp` | `phaserProcessSample(...)` |
| Compressor | `dynamics_dsp.h/.cpp` | `compressorProcessSample(...)` |
| Expander | `dynamics_dsp.h/.cpp` | `expanderProcessSample(...)` |
| Overdrive | `drive_dsp.h/.cpp` | `overdriveProcessSample(...)` |
| Distortion | `drive_dsp.h/.cpp` | `distortionProcessSample(...)` |
| Bitcrusher | `bitcrusher_dsp.h/.cpp` | `bitcrusherProcessSample(...)` |

## Etat de compilation probable

- Les fichiers separes sont ecrits pour etre compilables seuls avec un compilateur C++ standard.
- Verification syntaxe executee avec `arm-none-eabi-g++ -std=c++17 -fsyntax-only` sur les 7 fichiers `.cpp` separes: OK.
- Les fichiers bruts `Controller.cpp` et `Global.h` ne sont pas compilables seuls.

## Dependances restantes

- Echelle audio Drumboy mono `int32_t` 24-bit.
- `Delay` et `Chorus` exigent des buffers externes de taille `96000` et `5000`.
- Les transitions Drumboy ne sont pas reprises; les process separes gardent uniquement le chemin dry/wet direct.
- Les labels UI Drumboy ne sont pas repris; seules les valeurs DSP utiles des tables sont conservees.
- Les fonctions math restent utilisees dans `phaser`, `dynamics`, `drive`, `bitcrusher`.

## Limites avant portage BRICK6

- Pas d'adaptation aux APIs BRICK6.
- Pas de conversion float stereo.
- Pas d'analyse hard-RT complete.
- Pas de remplacement des fonctions math dans le chemin sample.
- Pas de bornage BRICK6 du `while` de fold dans `bitcrusher`.
- Pas de strategie memoire BRICK6 pour les gros buffers Delay/Chorus.

## Meilleurs candidats de premier portage

1. `bitcrusher_dsp.*`
2. `drive_dsp.*`
3. `flanger_dsp.*`
