# BRICK6 - produit actuel

BRICK6 est une machine audio embarquee a huit pistes top-level homogenees. Le code courant est l'autorite de ce document.

## Topologie

- BRICK expose seize identites logiques: huit top-level `0..7` et huit children GROUP `8..15`, actifs lorsque le top-level `7` est GROUP master.
- L'index est l'identite d'une entite. Il n'existe aucun role Play/Special ni piste fixe Master, FX, Input ou Looper.
- Chaque entite choisit `Off`, `Synth`, `Sampler`, `Drum`, `MIDI` ou `External`.
- `Sampler / Looper` est un moteur assignable avec son runtime propre.
- `External` combine MIDI et une entree `LINE` ou `USB`; `MIC` reste reserve au chemin Recorder / Audio REC. `track_input_ownership` garantit un proprietaire au plus par entree.
- Le Master est global, hors topologie. `SHIFT + STEP 16` ouvre ses traitements sans changer la selection.
- STEP 1 a 8 selectionnent les top-level; en GROUP, STEP 9 a 16 selectionnent les children `8..15`.

## Execution

`CONTROL_RT` possede les decisions et publie la FIFO fonctionnelle unique vers AUDIO. `STORAGE_IO`, `USB_SERVICE`, `UI_SERVICE` et `AUDIO_BG_LOCAL` restent des services cooperatifs separes; l'IRQ SAI conserve la timeline hard-RT.

## Sequence et edition

Chaque lane possede 64 steps avec trig, note, velocite, duree, microtiming, roll et p-locks complets, avec un pool de 512 entrees par lane. Le modele ne contient aucune action ni payload Special.

L'Undo/Redo structurel conserve au plus huit transactions. Copy n'en cree pas; les mutations de steps, Clear et Paste sont atomiques, n'enregistrent pas les no-op et purgent Redo lors d'une nouvelle branche. Un remplacement Pattern ou Project reussi invalide l'historique.

## Audio et UI

Le chemin critique reste borne, sans allocation dynamique dans l'IRQ. Les voies mixer sont consommees uniquement par les entites audio configurees. Reverb, delay, compresseur et gain Master restent des traitements globaux.

Les ensembles de piste sont `CFG`, `ENV`, `TONE`, `MOD`, `MIX`, `PLAY` et `MIDI FX`, filtres selon le moteur de l'entite.

## Persistance

Les formats persistants utilisent exclusivement B6CP v5 avec des DTO explicites. Une version ou une taille differente est rejetee sans migration. Pattern contient les seize identites logiques; Patch est le snapshot d'une entite et Project contient le Pattern de travail, les assets et les donnees produit associees.

La carte detaillee est dans [docs/architecture/ARCHITECTURE_GLOBAL.md](docs/architecture/ARCHITECTURE_GLOBAL.md).
