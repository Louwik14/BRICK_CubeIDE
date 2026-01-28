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

## Step 2 — Move audio processing out of IRQ

### Changements effectués
- Ajout de `BRICK6_REFACTOR_STEP_2` pour activer le déplacement du traitement audio hors IRQ.
- Ajout des flags DMA `audio_dma_half_ready` / `audio_dma_full_ready` et création de `audio_tasklet_poll()` pour déclencher le remplissage buffer hors IRQ.
- Les callbacks audio (TX half/full) ne font plus que poser des flags et incrémenter les compteurs existants.
- Appel de `audio_tasklet_poll()` en tout début de boucle principale.
- Ajout de TODO pour la maintenance cache à venir (DCache/MPU STM32H7 désactivés pour l'instant).

### Fichiers modifiés
- Inc/brick6_refactor.h
- Inc/audio_out.h
- Src/audio_out.c
- Src/main.c
- refactor_update.md

### Ce qui reste identique
- La logique de génération/remplissage audio est inchangée, elle est seulement déplacée hors IRQ.
- Les compteurs STEP 1 continuent de s'incrémenter.

### TODO cache STM32H7
- Ajouter les opérations de maintenance DCache/MPU autour du buffer DMA audio quand elles seront activées.

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_2` à 0 dans `Inc/brick6_refactor.h` pour revenir au traitement audio en IRQ.
