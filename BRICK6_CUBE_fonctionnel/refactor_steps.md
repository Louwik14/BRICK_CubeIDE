# Plan d’exécution — Refactor BRICK6 (STM32H743)

Ce document traduit `plan_refactor.md` en checklist **exécutable et progressive** pour restructurer le firmware sans RTOS, sans malloc, sans casser l’existant.

> ⚠️ État actuel du projet : **DCache / ICache / MPU sont désactivés dans CubeMX**.
> → Cela simplifie le debug DMA pour l’instant, **mais ce plan anticipe une future réactivation** (alignement, sections mémoire, clean/invalidate).

Contraintes :

* Ne pas toucher au code CubeMX hors `/* USER CODE */`
* Pas de RTOS
* Pas de malloc
* Changements **progressifs, réversibles, testables**
* L’audio DMA reste la **référence temporelle**

---

## Table des étapes

|  # | Étape                              | État | Validation rapide              |
| -: | ---------------------------------- | ---- | ------------------------------ |
|  1 | Instrumentation minimale           | ☐    | Compteurs visibles + log 1 Hz  |
|  2 | Audio : IRQ → tasklet              | ☐    | Audio OK, IRQ courtes          |
|  3 | Engine tasklet minimal             | ☐    | Compteur frames cohérent       |
| 4a | SD : suppression des waits en IRQ  | ☐    | Plus aucun blocage en callback |
| 4b | SD : FSM simple non bloquante      | ☐    | Streaming stable sans ring     |
|  5 | Ring buffer **par blocs** SD→audio | ☐    | Underflow géré proprement      |
|  6 | Budgets CPU des tasklets           | ☐    | USB/MIDI/UI bornés             |
|  7 | Nettoyage & diagnostics            | ☐    | Archi claire, stats stables    |

---

## Étape 1 — Instrumentation minimale

**But**

* Observer le système **sans changer son comportement**.

**Modifs**

* Ajouter compteurs `volatile` sous macro STEP 1
* Incrémenter dans :

  * callbacks audio DMA
  * callbacks SD
  * USB host poll
* `main.c` :

  * log **max 1 Hz**, jamais en IRQ
* Option : GPIO debug pin sous `BRICK6_DEBUG_PIN`

**Tests**

* Vérifier compteurs en UART / debugger

**Rollback**

* Désactiver la macro

---

## Étape 2 — Audio : IRQ → tasklet

**But**

* Les callbacks DMA audio ne font **QUE poser des flags**.

**Modifs**

* `audio_out.c` :

  * callbacks = set `audio_dma_half_ready` / `audio_dma_full_ready`
  * créer `audio_tasklet_poll()`
* `main.c` :

  * appeler `audio_tasklet_poll()` **en premier**
* Macro : STEP 2

**Attention STM32H7**

* Aujourd’hui cache désactivé → OK
* Plus tard : clean/invalidate obligatoire

**Tests**

* Audio OK
* IRQ rapides
* Pas de glitch évident

**Rollback**

* Rétablir ancien chemin sous macro

---

## Étape 3 — Engine tasklet **minimal**

> ⚠️ Important : **PAS un vrai moteur musical encore.**

**But**

* Juste :

  * accumuler des frames
  * générer un “tick moteur” à intervalle fixe

**Modifs**

* Nouveau :

  * `engine_tasklet.c/.h`
  * compteur de frames, rien de plus
* Appelé après audio tasklet
* Macro : STEP 3

**Tests**

* Log : ticks cohérents avec sample rate

**Rollback**

* Désactiver module

---

## Étape 4a — SD : suppression des waits en IRQ

**But**

* Plus **AUCUNE attente active** dans callbacks SD.

**Modifs**

* `sd_stream.c` :

  * callbacks = flags seulement
  * supprimer `Wait_SDCARD_Ready()` en IRQ
* Macro : STEP 4A

**Tests**

* Plus de blocage IRQ
* SD toujours fonctionnelle

---

## Étape 4b — SD : FSM simple non bloquante

**But**

* Déplacer la logique SD dans :

  * `sd_tasklet_poll()`

**États initiaux**

* IDLE
* FILL
* STREAM
* ERROR

**Modifs**

* `sd_stream.c/.h` :

  * ajouter FSM
* `main.c` :

  * appeler `sd_tasklet_poll()`

**Tests**

* Lecture continue stable

---

## Étape 5 — Ring buffer **par blocs** SD → audio

> ⚠️ On évite un ring byte-level.
> On fait un ring de **blocs audio**.

**Exemple**

```c
#define AUDIO_BLOCK_SIZE   4096
#define AUDIO_BLOCK_COUNT  4
```

**Modifs**

* Nouveau module :

  * `sd_audio_block_ring.c/.h`
* SD produit des blocs
* Audio consomme des blocs
* Si vide → silence

**Tests**

* Débrancher SD → audio ne plante pas
* Compteur underflow incrémenté

---

## Étape 6 — Budgets CPU des tasklets

**But**

* Empêcher USB/SD/UI d’affamer l’audio.

**Modifs**

* `midi_host_poll_bounded(max_packets)`
* `midi_poll_bounded(max_msgs)`
* `main.c` :

  * appeler versions bornées
* Pas de timers compliqués au début

**Tests**

* Sous charge USB + SD :

  * audio reste stable

---

## Étape 7 — Nettoyage & diagnostics

**But**

* Stabiliser l’architecture.

**Modifs**

* Créer :

  * `diagnostics_tasklet.c`
* Centraliser :

  * `brick6_config.h`
* Nettoyer hacks temporaires

---

# Garde-fous globaux

* Macros :

  * STEP_* (déprécié)
  * `BRICK6_DEBUG_PIN`
* Logs :

  * jamais en IRQ
  * max 1 Hz
* Pas de malloc

---

# ⚠️ Notes STM32H7 importantes

> Actuellement : **cache / MPU désactivés** → plus simple, mais plus lent.

Quand tu les réactiveras :

* Buffers DMA :

  * alignés 32 bytes
  * dans bonnes régions RAM
* Toujours :

  * `SCB_CleanDCache_by_Addr`
  * `SCB_InvalidateDCache_by_Addr`
* Sections :

  * `.ram_d1`, `.ram_d2` selon périph

---

# 🎯 Philosophie

> Ce plan **n’est pas un refactor en une fois**.
> C’est une **trajectoire de migration contrôlée** vers une architecture solide pour une groovebox complète (UI, séquenceur, synthés, streaming, etc).

---
