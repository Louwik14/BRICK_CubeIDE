# Drumboy FX DSP Index

Source auditee:
`C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6\Inspiration\Drumboy-main\02 Software`

Destination d'extraction:
`C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6\Inspiration\Drumboy_FX_DSP_Extract`

Contraintes appliquees:
- Exploration limitee a `Inspiration/Drumboy-main/02 Software`.
- Aucun code BRICK6 modifie hors `Inspiration`.
- `ARCHITECTURE_GLOBAL.md` absent a la racine du repo au moment de la passe.
- Documents audio/param lus: `docs/architecture/z1_audio_hard_rt_mix.md`, `docs/architecture/z3_param_modulation_control.md`.
- Aucun `z*.md` trouve dans `Inspiration`.

## Fichiers Drumboy source contenant les FX

### `Inspiration\Drumboy-main\02 Software\Drumboy-App-H723\Core\Library\Controller\Controller.cpp`

Fonctions / zones:
- `Controller::processAudioEffect(uint8_t effectNum_, int32_t audio_)`
- Branches `EF_DELAY`, `EF_CHORUS`, `EF_FLANGER`, `EF_PHASER`, `EF_COMPRESSOR`, `EF_EXPANDER`, `EF_OVERDRIVE`, `EF_DISTORTION`, `EF_BITCRUSHER`
- Setters associes: `effect_setAData`, `effect_setBData`, `effect_setCData`, `effect_setDData`, `effect_setEData`
- Transitions associees: `effect_genTransition`, `effect_mixTransition`, `interruptTransition`

### `Inspiration\Drumboy-main\02 Software\Drumboy-App-H723\Core\Library\Global\Global.h`

Types / constantes:
- `struct Delay`
- `struct ChorusDelay`, `struct Chorus`
- `struct Flanger`
- `struct Phaser`
- `struct Compressor`, `struct Expander`
- `struct Overdrive`, `struct Distortion`
- `struct Bitcrusher`, `BitcrusherMode`
- `EffectType`, `Effect`, `SubEffect`, `EffectGenTransition`, `EffectMixTransition`

## Nouveaux fichiers d'extraction

| Effet | Nouveaux fichiers | Statut compilation probable | Notes |
| --- | --- | --- | --- |
| Delay | `delay_dsp.h`, `delay_dsp.cpp` | compilable seule | Buffer delay fourni par appelant; adresses SDRAM Drumboy retirees. |
| Chorus | `chorus_dsp.h`, `chorus_dsp.cpp` | compilable seule | Buffer chorus fourni par appelant; double lecture modulee conservee. |
| Flanger | `flanger_dsp.h`, `flanger_dsp.cpp` | compilable seule | Buffer local conserve dans `FlangerState`. |
| Phaser | `phaser_dsp.h`, `phaser_dsp.cpp` | compilable seule | Recalcule les coefficients par sample comme Drumboy. |
| Compressor | `dynamics_dsp.h`, `dynamics_dsp.cpp` | compilable seule | Regroupe avec Expander; detection dB et smoothing conserves. |
| Expander | `dynamics_dsp.h`, `dynamics_dsp.cpp` | compilable seule | Regroupe avec Compressor; logique threshold/rate inverse conservee. |
| Overdrive | `drive_dsp.h`, `drive_dsp.cpp` | compilable seule | Regroupe avec Distortion; waveshaper cubique + filtre tone conserve. |
| Distortion | `drive_dsp.h`, `drive_dsp.cpp` | compilable seule | Regroupe avec Overdrive; waveshaper `atanf` + filtre tone conserve. |
| Bitcrusher | `bitcrusher_dsp.h`, `bitcrusher_dsp.cpp` | compilable seule | Sample hold, mask 24-bit, clip/fold conserves. |

## Verdict par effet

| Effet | Verdict | Traitement audio present dans le fichier separe |
| --- | --- | --- |
| Delay | trouve clairement | Oui: `delayProcessSample(...)`. |
| Chorus | trouve clairement | Oui: `chorusProcessSample(...)`. |
| Flanger | trouve clairement | Oui: `flangerProcessSample(...)`. |
| Phaser | trouve clairement | Oui: `phaserProcessSample(...)`. |
| Compressor | trouve clairement | Oui: `compressorProcessSample(...)`. |
| Expander | trouve clairement | Oui: `expanderProcessSample(...)`. |
| Overdrive | trouve clairement | Oui: `overdriveProcessSample(...)`. |
| Distortion | trouve clairement | Oui: `distortionProcessSample(...)`. |
| Bitcrusher | trouve clairement | Oui: `bitcrusherProcessSample(...)`. |

## Dependances restantes

- Syntaxe des 7 `.cpp` separes verifiee avec `arm-none-eabi-g++ -std=c++17 -fsyntax-only`: OK.
- Les fichiers separes restent mono `int32_t` et gardent l'echelle Drumboy 24-bit (`INT24_MAX = 8388607` par defaut).
- Delay/Chorus n'allouent pas leur memoire; l'appelant doit fournir un buffer de taille correcte.
- Les transitions Drumboy `EffectGenTransition` / `EffectMixTransition` ne sont pas portees; seuls les dry/wet directs sont conserves.
- Les tables de parametres ont ete reduites a leurs valeurs DSP; les labels UI Drumboy ne sont pas repris.
- Les fonctions math C++ restent necessaires: `sin`, `cos`, `tan`, `atan`, `pow`, `log10`, `log`, `exp`, `fabs`.
- Aucun mapping stereo, bloc BRICK6, smoothing BRICK6 ou integration hard-RT BRICK6 n'a ete ajoute.

## Effets simples a porter en premier

1. `bitcrusher_dsp.*`: pas de buffer externe, etat court, cout previsible hors boucle `while` a borner avant IRQ.
2. `drive_dsp.*`: pas de buffer externe, mais fonctions math et filtre tone a pre-calculer hors audio.
3. `flanger_dsp.*`: buffer court local, algorithme lisible, modulation simple.

## Elements conserves de l'extraction precedente

- `Controller.cpp`: copie brute complete, gardee comme reference de tracabilite.
- `Global.h`: copie brute complete, gardee comme reference de tracabilite.
