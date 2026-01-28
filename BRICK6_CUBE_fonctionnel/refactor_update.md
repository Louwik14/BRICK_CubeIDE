# Refactor BRICK6 — Journal

## Étape en cours : STEP 1 — Instrumentation

### Changements effectués
- Ajout d'un fichier de configuration pour activer la macro `BRICK6_REFACTOR_STEP_1` et déclarer les compteurs d'instrumentation.
- Compteurs ajoutés dans les callbacks audio SAI (TX/RX half/full).
- Compteurs ajoutés dans les callbacks SD (Rx/Tx completion, double buffer, erreurs).
- Compteurs ajoutés pour les boucles USB host et MIDI host dans la main loop.
- Log 1 Hz des compteurs d'instrumentation (toujours hors IRQ).

### Fichiers modifiés
- Inc/brick6_refactor.h
- Src/brick6_refactor.c
- Src/main.c
- Src/sd_stream.c
- refactor_update.md

### Validation
- Non exécutée (modifications instrumentation uniquement, aucun changement de logique).

### Prochaine étape
- STEP 2 — Audio : IRQ → tasklet

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_1` à 0 dans `Inc/brick6_refactor.h`.
