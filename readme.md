# BRICK6 — produit actuel

BRICK6 est une machine audio embarquée à huit pistes sonores homogènes. Le code courant est l'autorité de ce document.

## Topologie

- Low-Cost et Premium exposent exactement huit slots logiques, indexés `0..7`.
- L'index est l'unique identité d'une piste. Il n'existe aucun rôle Play/Special ni piste fixe Master, FX, Input ou Looper.
- Chaque slot choisit `Off`, `Synth`, `Sampler`, `Drum`, `MIDI` ou `External`.
- `Sampler / Looper` est un moteur assignable avec son runtime propre, indexé par le slot.
- `External` combine MIDI et une entrée physique. `track_input_ownership` garantit un propriétaire au plus par entrée et l'activation matérielle suit cette propriété réelle.
- Le Master est global, hors topologie. `SHIFT + STEP 16` ouvre ses traitements sans changer la piste active.
- STEP 1 à 8 sélectionnent les pistes; STEP 9 à 16 n'ont aucune identité de piste.

## Séquence et édition

Chaque piste possède 64 steps avec trig, note, vélocité, durée, microtiming, roll et p-locks complets, ainsi qu'un pool de 1024 entrées. Le modèle ne contient aucune action ni payload Special.

L'Undo/Redo structurel conserve au plus huit transactions. Copy n'en crée pas; les mutations de steps, Clear et Paste sont atomiques, n'enregistrent pas les no-op et purgent Redo lors d'une nouvelle branche. Un remplacement Pattern ou Project réussi invalide l'historique.

## Audio et UI

Le chemin critique reste borné, sans allocation dynamique dans l'IRQ. Les voies mixer sont consommées uniquement par les pistes audio réellement configurées. La piste FX et MacroFX ont été supprimés; reverb, delay, compresseur et gain Master restent des traitements globaux.

Les ensembles de piste sont `CFG`, `ENV`, `TONE`, `MOD`, `MIX`, `PLAY` et `MIDI FX`, filtrés selon le moteur du slot.

## Persistance

Les versions courantes strictes sont Pattern v6, Project v6, Kit v4 et Patch v4. Une version ou une taille différente est rejetée sans migration. Pattern, Project et Kit utilisent huit slots directs `0..7`; Patch est le snapshot d'un seul slot et ne stocke aucune identité topologique.

La carte détaillée est dans [docs/architecture/ARCHITECTURE_GLOBAL.md](docs/architecture/ARCHITECTURE_GLOBAL.md).
