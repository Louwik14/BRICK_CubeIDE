# Plan d’exécution — Refactor BRICK6 (STM32H743)

Ce document traduit `plan_refactor.md` en checklist exécutable, avec des étapes sûres et réversibles. Chaque étape est isolée via macros, sans RTOS, sans malloc, et sans toucher au code CubeMX hors `/* USER CODE */`.

## Table des étapes

| # | Étape | État | Validation rapide |
|---:|---|---|---|
| 1 | Instrumentation minimale | ☐ | Compteurs visibles + log 1 Hz |
| 2 | Audio : IRQ -> tasklet | ☐ | Audio OK, IRQ courtes |
| 3 | Engine tasklet | ☐ | Tick moteur aligné audio |
| 4 | SD non bloquant (FSM) | ☐ | Plus de wait en IRQ |
| 5 | Ring buffer SD→audio | ☐ | Underflow géré |
| 6 | Budgets CPU tasklets | ☐ | USB/SD/UI bornés |
| 7 | Nettoyage & diagnostics | ☐ | Code clair, flags stables |

---

## Étape 1 — Instrumentation minimale

**But**
- Mesurer sans changer le comportement : temps boucle main, compteurs IRQ audio/SD/USB.

**Modifs (petits ajouts, sans logique)**
- Ajouter des compteurs atomiques `volatile` + macros d’activation : `BRICK6_REFACTOR_STEP_1`.
- `Src/main.c` :
  - Ajouter un mini module de stats dans `/* USER CODE BEGIN 0 */`.
  - Log 1 Hz max (dans main loop uniquement), jamais en IRQ.
- `Src/stm32h7xx_it.c`, `Src/audio_out.c`, `Src/sd_stream.c`, `Src/midi_host.c` :
  - Incrémenter des compteurs dans callbacks IRQ/USB, sans logique.
- Option debug pin (GPIO) sous macro `BRICK6_DEBUG_PIN` (toggle sur boucle ou tasklets), **optionnelle**.

**Tests**
- Compilation OK.
- Vérifier que les compteurs augmentent (UART log ou watch). Log à 1 Hz max.

**Rollback**
- Désactiver `BRICK6_REFACTOR_STEP_1` dans un header (ex: `Inc/brick6_config.h`) ou retirer les ajouts isolés.

---

## Étape 2 — Audio : IRQ -> tasklet

**But**
- Déplacer tout le traitement audio hors IRQ DMA SAI. IRQ = flag uniquement.

**Modifs**
- `Src/audio_out.c` :
  - Dans les callbacks DMA SAI, remplacer tout traitement par :
    - `audio_dma_half_ready = 1;` / `audio_dma_full_ready = 1;` (flags).
  - Créer `audio_tasklet_poll()` qui consomme les flags et appelle le traitement existant.
- `Inc/audio_out.h` :
  - Déclarer `audio_tasklet_poll()` et flags.
- `Src/main.c` :
  - Appeler `audio_tasklet_poll()` **en premier** dans la boucle principale.
- Macro de contrôle : `BRICK6_REFACTOR_STEP_2`.

**Risques / attention**
- DMA/Cache H7 : s’assurer que les buffers audio sont en RAM non cacheable ou nettoyer/invalidater au bon moment.
- Ne pas modifier le code CubeMX hors `USER CODE`.

**Tests**
- Audio OK (pas de glitch évident) + IRQ courtes en debug.
- Compteurs audio IRQ = progression normale ; temps loop stable.

**Rollback**
- Réactiver traitement audio dans callbacks sous macro si nécessaire.

---

## Étape 3 — Engine tasklet

**But**
- Introduire la couche moteur synchronisée par l’audio, sans logique lourde.

**Modifs**
- Nouveau fichier : `Src/engine_tasklet.c` + `Inc/engine_tasklet.h`.
  - API minimale : `void engine_tasklet_poll(uint32_t frames_processed);`
  - Accumuler `samples_accum` et déclencher `engine_tick()` quand `samples_per_tick` atteint.
- `Src/audio_out.c` :
  - Après rendu audio d’un bloc, appeler `engine_tasklet_notify(frames)` ou stocker `frames_processed`.
- `Src/main.c` :
  - Appeler `engine_tasklet_poll()` après `audio_tasklet_poll()`.
- Macro : `BRICK6_REFACTOR_STEP_3`.

**Risques / attention**
- Ne pas faire de calcul lourd. Juste des compteurs/flags.

**Tests**
- Logs 1 Hz : compteur de ticks moteur cohérent avec fréquence audio.

**Rollback**
- Désactiver la compilation de `engine_tasklet` via macro et linker guard.

---

## Étape 4 — SD non bloquant (FSM)

**But**
- Supprimer tout blocage SD dans IRQ/callbacks et déplacer la progression en main loop.

**Modifs**
- `Src/sd_stream.c` :
  - Remplacer `Wait_SDCARD_Ready()` dans callbacks par des flags d’état.
  - Introduire FSM : `IDLE/PREFILL/STREAMING/UNDERFLOW/ERROR`.
  - Ajouter `sd_tasklet_poll()` qui fait avancer la FSM hors IRQ.
- `Inc/sd_stream.h` :
  - Exposer `sd_tasklet_poll()` + états.
- `Src/main.c` :
  - Appeler `sd_tasklet_poll()` après engine tasklet.
- Macro : `BRICK6_REFACTOR_STEP_4`.

**Risques / attention**
- DMA SD + DCache : buffers alignés et invalidation si nécessaire.
- Pas d’attente active dans IRQ.

**Tests**
- Lecture SD continue sans blocage (profiling IRQ + compteur SD progressif).

**Rollback**
- Revenir aux anciens callbacks SD sous macro.

---

## Étape 5 — Ring buffer SD → audio

**But**
- Découpler SD de l’audio via un ring buffer logique.

**Modifs**
- Nouveau module : `Src/sd_ringbuffer.c` + `Inc/sd_ringbuffer.h`.
  - API proposée :
    - `rb_init(void* storage, size_t size)`
    - `rb_write(const void* src, size_t bytes)`
    - `rb_read(void* dst, size_t bytes)`
    - `rb_level()` / `rb_free()`
  - Pas de malloc, stockage statique.
- `Src/sd_stream.c` :
  - Les buffers SD DMA écrivent dans le ring buffer.
- `Src/audio_out.c` :
  - Lire du ring buffer lors du rendu ; si underflow → silence.
- Macro : `BRICK6_REFACTOR_STEP_5`.

**Risques / attention**
- Sous/sur débit : watermarks et prefill strict.
- Bien gérer l’alignement DMA et les tailles bloc.

**Tests**
- Underflow simulé : vérifier que l’audio passe en silence sans blocage.

**Rollback**
- Désactiver ring buffer et revenir au chemin direct (sous macro).

---

## Étape 6 — Budgets CPU tasklets

**But**
- Borne explicite pour USB/MIDI/UI/logs afin de protéger l’audio.

**Modifs**
- `Src/midi_host.c` :
  - Ajouter `midi_host_poll_bounded(max_ops)`.
- `Src/midi.c` :
  - Ajouter limites de traitement par frame.
- `Src/main.c` :
  - Introduire un mini scheduler coopératif :
    - `usb_tasklet_poll_bounded()`
    - `ui_tasklet_poll_bounded()`
    - `diagnostics_tasklet_poll()` (1 Hz)
- Macro : `BRICK6_REFACTOR_STEP_6`.

**Risques / attention**
- Ne pas créer de starvation : toujours faire un minimum par tasklet.

**Tests**
- USB/MIDI réactifs sous charge SD + audio, sans glitch audio.

**Rollback**
- Désactiver les versions bornées et revenir aux poll simples.

---

## Étape 7 — Nettoyage & diagnostics

**But**
- Stabiliser l’architecture, séparer diagnostics et enlever les hacks temporaires.

**Modifs**
- Regrouper macros dans `Inc/brick6_config.h`.
- Isoler `diagnostics_tasklet.c` (logs 1 Hz, stats).
- Nettoyer les tests transitoires dans les callbacks.

**Tests**
- Compilation + test audio + SD + USB/MIDI OK.

**Rollback**
- Garder les macros d’étape pour désactiver modules si besoin.

---

## Garde-fous concrets (à appliquer dès les premières étapes)

- Macros de build :
  - `BRICK6_REFACTOR_STEP_1..7`
  - `BRICK6_DEBUG_PIN`
- Compteurs/flags `volatile` (IRQ-safe).
- Logs max 1 Hz **hors IRQ**.
- Aucune allocation dynamique (`malloc` interdit).

---

## Pièges STM32H7 (à surveiller)

- **DCache + DMA** : invalider/nettoyer les buffers DMA audio/SD/USB au bon moment.
- **Alignement** : buffers alignés (32 bytes) et placés dans la bonne RAM (D1/D2).
- **Sections mémoire** : utiliser `.ram_d1` / `.ram_d2` si nécessaire pour DMA.
- **IRQ time** : éviter tout appel HAL bloquant en IRQ.
- **Priorités NVIC** : audio DMA en priorité supérieure à SD/USB.

