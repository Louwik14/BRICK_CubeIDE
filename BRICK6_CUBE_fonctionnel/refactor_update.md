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

## Step 3 — Engine tasklet

### Description
- Ajout du module `engine_tasklet` cadencé par les frames audio (accumulateur + seuil de tick).
- `audio_tasklet_poll()` notifie le moteur à chaque demi-buffer/plein-buffer traité.
- `engine_tasklet_poll()` est appelé dans la boucle principale juste après l'audio.
- Ajout du compteur `engine_tick_count` dans le log 1 Hz de STEP 1.

### Fichiers modifiés
- Inc/brick6_refactor.h
- Inc/engine_tasklet.h
- Src/engine_tasklet.c
- Src/audio_out.c
- Src/main.c
- refactor_update.md

### Ce qui ne change pas
- Aucune logique musicale ni séquenceur.
- Aucun changement de comportement audio (même rendu, hors IRQ).
- Pas de modification SD/MIDI/USB/IRQ.

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_3` à 0 dans `Inc/brick6_refactor.h`.

## Step 4 — SD non bloquant (FSM)

### Changements effectués
- Ajout de `BRICK6_REFACTOR_STEP_4` et création d'une FSM SD non bloquante pilotée par `sd_tasklet_poll()`.
- Les callbacks SD ne font plus que poser des flags (RX/TX/erreur) et incrémenter les compteurs STEP 1.
- La progression des transferts SD (découpage en chunks, avance des compteurs, fin de transfert) est traitée dans la tasklet.
- Appel de `sd_tasklet_poll()` dans la boucle principale juste après `engine_tasklet_poll()`.

### Fichiers modifiés
- Inc/brick6_refactor.h
- Inc/sd_stream.h
- Src/sd_stream.c
- Src/main.c
- refactor_update.md

### Ce qui ne change pas
- Même API publique SD (start/read/write, stats, buffers).
- Même comportement fonctionnel attendu côté streaming et tests SD.
- Les compteurs STEP 1 continuent de s'incrémenter.

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_4` à 0 dans `Inc/brick6_refactor.h` pour revenir au flux SD avec attentes actives.

## Step 5 — Ring buffer blocs SD → Audio

### Changements effectués
- Ajout d'un module de ring buffer de blocs audio (tableau statique) avec API minimale de production/consommation.
- Le SD ne remplit plus directement les buffers audio : les callbacks SD posent des flags et la tasklet copie les buffers DMA dans le ring buffer.
- L'audio consomme désormais des blocs du ring buffer pour remplir le DMA, et joue du silence en cas d'underflow avec compteur dédié.
- Mise en place d'une logique de pré-remplissage des blocs côté SD avant d'entrer en streaming, et limitation du démarrage des chunks si le ring est plein.
- Ajout de la macro `BRICK6_REFACTOR_STEP_5` pour isoler le nouveau flux.

### Fichiers modifiés
- Inc/brick6_refactor.h
- Inc/audio_out.h
- Inc/sd_audio_block_ring.h
- Src/audio_out.c
- Src/sd_audio_block_ring.c
- Src/sd_stream.c
- refactor_update.md

### Ce qui ne change pas
- L'API publique SD reste identique (start/read/write, stats, buffers).
- Les callbacks IRQ restent courts (flags uniquement).
- Aucune modification CubeMX, pas de RTOS, pas de malloc.

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_5` à 0 dans `Inc/brick6_refactor.h` pour revenir au remplissage audio précédent.

## Step 6 — Budgets CPU des tasklets

### Changements effectués
- Ajout de `BRICK6_REFACTOR_STEP_6` et des budgets `USB_BUDGET_PACKETS`, `MIDI_BUDGET_MSGS`, `SD_BUDGET_STEPS` pour borner les pollers lourds.
- Création des versions bornées `usb_host_tasklet_poll_bounded`, `midi_host_poll_bounded` et `sd_tasklet_poll_bounded`.
- Ajout des compteurs de famine `usb_budget_hit_count`, `midi_budget_hit_count`, `sd_budget_hit_count` et intégration dans le log 1 Hz.
- Boucle principale mise à jour pour appeler les tasklets bornées, tout en gardant l’audio/engine en priorité.

### Fichiers modifiés
- Inc/brick6_refactor.h
- Src/brick6_refactor.c
- App/usb_stack/usb_host.h
- App/usb_stack/usb_host.c
- Inc/midi_host.h
- Src/midi_host.c
- Inc/sd_stream.h
- Src/sd_stream.c
- Src/main.c
- refactor_update.md

### Ce qui ne change pas
- Pas de RTOS, pas de malloc, pas de changement CubeMX.
- L’ordre de priorité audio/engine dans la main loop reste inchangé.
- Les APIs publiques existantes restent disponibles.

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_6` à 0 dans `Inc/brick6_refactor.h` pour revenir aux pollers non bornés.

## Step 7 — Nettoyage & diagnostics

### Changements effectués
- Ajout du module `diagnostics_tasklet` pour centraliser les logs 1 Hz, les compteurs et les diagnostics SD/SAI.
- Déplacement des logs et du test SDRAM alloc vers le module diagnostics, avec une boucle principale simplifiée.
- Centralisation du compteur `audio_underflow_count` dans `brick6_refactor`.
- Ajout d’un stub `ui_tasklet_poll()` pour clarifier la boucle principale.
- Ajout de la macro `BRICK6_REFACTOR_STEP_7` pour isoler le nettoyage.

### Fichiers modifiés
- Inc/brick6_refactor.h
- Src/brick6_refactor.c
- Inc/audio_out.h
- Src/audio_out.c
- Inc/diagnostics_tasklet.h
- Src/diagnostics_tasklet.c
- Inc/ui_tasklet.h
- Src/ui_tasklet.c
- Src/main.c
- refactor_update.md

### Ce qui ne change pas
- Aucun changement fonctionnel audio/SD/USB/MIDI.
- Pas de RTOS, pas de malloc, pas de changement CubeMX.
- Les logs 1 Hz et diagnostics restent identiques, simplement centralisés.

### Rollback
- Mettre `BRICK6_REFACTOR_STEP_7` à 0 dans `Inc/brick6_refactor.h` pour revenir au diagnostics/loop précédent dans `main.c`.

## Step 8 — PASS 1 : Suppression du code mort dans main.c

### Changements effectués
- Suppression de tout le code conditionnel `#if !BRICK6_REFACTOR_STEP_7` dans `main.c` (anciens tests SDRAM/SD, logs legacy, FSM de tests, helpers UART/CRC/hex).
- Simplification des sections USER CODE BEGIN 0/2/3 pour ne conserver que le chemin STEP 7 validé.
- Nettoyage des `#include` devenus inutiles après suppression du code mort.

### Fichiers modifiés
- Src/main.c
- refactor_update.md

### Ce qui ne change pas
- Aucun changement fonctionnel : initialisation, logs diagnostics, audio/SD/USB/MIDI et ordre d’exécution restent identiques.

### Rollback
- Revenir au commit précédent pour restaurer les blocs legacy supprimés.
