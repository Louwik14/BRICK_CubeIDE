# SYN — Moteurs de synthese et Drum

## Statut

CONFIRME, sauf import DX7 PARTIEL. Validation materielle : rendu sonore et charge polyphonique.

## Moteurs

- Prism : deux oscillateurs, 32 modeles actifs, param1/2, AMOD, volume/balance, tune/detune, pitch mod, reset phase, drift. Les domaines param1/2 peuvent devenir discrets selon modele.
- Stack : trois oscillateurs (SINE/TRI/SQUARE/SAW/SHAPE/TRIPLE SAW), model/tune, timbre/color pour OSC3, niveaux, noise, detune commun et reset/free phase.
- Wave : deux wavetable assets, position/start/length, volume/balance/tune/detune. Les tables/mipmaps sont chargees hors AUDIO puis publiees.
- FM : 32 algorithmes, six operateurs, on/mode/frequency/level/detune, EG par operateur, velocity/key scaling, Pitch EG R1..R4/L1..L4 et macros globales. Transpose au centieme de demi-ton. `fm_control_state` est owner unique.
- Acid : wave saw/square, tune, slide, accent, cutoff, resonance, env mod, decay et VCF rate.
- Drum MD : six modeles `TRX-BD/TRX-SD/TRX-CH/EFM-BD/EFM-SD/EFM-CB`, huit parametres P1..P8 dont labels/applicabilite dependent du modele.

## Comportement commun

TONE selectionne une famille de pages par type. ENV, MIX, MOD, MIDI FX, Audio FX et polyphonie sont superposes au moteur selon disponibilite. Un changement de PROGRAM ne retrigger pas une note tenue : le renderer est remplace/reinitialise par le contrat de runtime. Les valeurs de base et assets sont persistants ; phases, voix et caches ne le sont pas.

## Limites

FM Algorithm/OP ON/OP MODE sont p-lockables mais pas Matrix. Asset WAVE n'est ni Param, ni p-lock, ni Matrix. L'import DX7 parse les SysEx 32 voices/single voice et mappe vers FM, mais aucun workflow UI Release complet n'est prouve.

## Sources

`Src/UI/pages/ui_page_template_tone.c`, `Src/Audio/Engines/{prism_engine.cpp,stack_engine.c,wavetable_engine.c,fm_engine.cpp,acid_engine.c}`, `Src/Track/{tone_program_control.c,fm_control_state.c}`, `Src/Import/dx7_*`, catalogues `Src/Param/*catalog*`.
