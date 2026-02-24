# 🎛 BRICK — Architecture Firmware Cible (Mixer + FX + Sequencer)

---

## 1. Vision globale

Firmware temps réel pour STM32H743 combinant :

- 🎚 Mixer audio (multi entrées stéréo)
- 🎛 FX internes (pool dynamique sans malloc)
- 🔌 DSP externes (SPI link type Ksoloti)
- 🎹 Séquenceur 16 pistes avec p-lock
- 💾 Sauvegarde projet (Flash / SD)
- ⏱ Clock dérivée de l’audio (sample-accurate)

---

## 2. Architecture globale

```

UI / SEQUENCER
↓
CONTROL ROUTER
↓
PARAM STORE
↓       ↓
DSP     SPI LINK
↓
AUDIO

```

---

## 3. Modules principaux

---

## 🔊 AUDIO (Hard realtime)

### `audio.c`
- SAI + DMA TDM
- IRQ callbacks
- appelle DSP

```

IRQ:
→ unpack
→ dsp_process_block()
→ pack

````

⚠️ contraintes :
- no malloc
- no log
- no HAL bloquant

---

### `audio_io.c`
- conversion int32 ↔ float
- mapping TDM

---

### `audio_mix.c`
- sommation tracks
- routing main / cue

---

## 🎛 DSP ENGINE

### `dsp_engine.c`

Entrée unique DSP :

```c
void dsp_process_block(float **in, float **out, uint32_t frames);
````

---

## 🎚 FX POOL (CRITIQUE)

### `dsp_fx_pool.c`

👉 cœur du système FX

* pool statique d’effets
* pas de malloc
* activation/désactivation runtime

```c
typedef struct {
    uint8_t active;
    fx_type_t type;
    void *state;
} fx_slot_t;

fx_slot_t fx_pool[MAX_FX];
```

---

### `dsp_fx_chain.c`

* routing des FX :

  * insert par track
  * send FX (2 max)

```
Track → Filter → Dist → Send → Return → Mix
```

---

### `dsp_fx_types/`

Chaque FX :

```
fx_filter.c
fx_distortion.c
fx_delay.c
fx_reverb.c
```

Règles :

* state local
* pas d’allocation
* reset safe

---

## ⚙️ PARAM SYSTEM

### `param_ids.h`

Tous les paramètres :

```
TRACK1_GAIN
TRACK1_FILTER_CUTOFF
FX1_MIX
SEND1_LEVEL
...
```

---

### `param_store.c`

Source de vérité globale

```c
float param_get(param_id_t id);
void  param_set(param_id_t id, float v);
```

---

### `param_smoother.c`

* interpolation audio-safe
* utilisé côté DSP

---

### `param_router.c`

Route vers :

* DSP interne
* SPI externe
* sequencer

---

## 🎛 CONTROL ROUTER (chef d’orchestre)

### `control_router.c`

Entrées :

* UI
* Sequencer

Sorties :

* param_store
* events externes

```c
void control_set_param(param_id_t id, float value);
```

👉 point unique de mutation

---

## 🎹 SEQUENCER

### `seq_model.c`

* steps
* p-locks
* patterns

---

### `seq_engine.c`

* lecture temps réel
* déclenché par clock

---

### `seq_param_bridge.c`

Convertit step → param

```c
→ control_set_param()
```

---

### `seq_clock.c`

⚠️ basé sur audio

---

## ⏱ ENGINE CLOCK (AUDIO DRIVEN)

### `engine_tasklet.c`

Clock dérivée des buffers audio :

* 32 frames → 1 tick
* 48kHz → 1500 Hz

### Flow :

```
IRQ audio:
    engine_tasklet_notify_frames()

Main loop:
    engine_tasklet_poll()
        → engine_tick()
```

---

### Avantages

* sample-accurate
* stable
* pas de jitter
* synchro parfaite DSP / sequencer

---

## 🧠 UI

### `ui_input.c`

* boutons
* encodeurs

---

### `ui_controller.c`

* logique UI
* mapping param

---

### `ui_pages_*.c`

* FX
* mixer
* sequencer

---

### `ui_renderer.c`

* écran
* LEDs

---

## 🔌 DSP EXTERNE

### `spi_link.c`

* transport SPI

---

### `spi_protocol.c`

* protocole param
* envoi commandes DSP externes

---

## 💾 SAUVEGARDE

### `storage/`

---

### `storage_manager.c`

API globale :

```c
void save_project(uint8_t slot);
void load_project(uint8_t slot);
```

---

### `storage_flash.c`

* sauvegarde rapide
* presets
* état courant

---

### `storage_sd.c`

* projets complets
* patterns
* backup

---

### `project_format.h`

Structure :

```
struct project {
    mixer_state
    fx_config
    seq_patterns
    routing
}
```

---

## 🧩 Règles critiques

---

### 🔥 AUDIO THREAD

* jamais bloquer
* jamais malloc
* accès lecture uniquement

---

### ⚙️ PARAM

* write : UI / SEQ
* read : DSP

→ lock-free ou double buffer

---

### 🔌 FX

* pool statique
* activation runtime
* aucun malloc

---

### 💾 STORAGE

* jamais dans IRQ
* jamais dans audio thread

---

## 🚫 Dépendances interdites

* DSP → UI
* DSP → storage
* UI → DSP direct
* SEQ → DSP direct
* AUDIO → param_store (write)

---

## ✅ Dépendances autorisées

```
UI ─────┐
        │
SEQ ────┼──→ CONTROL → PARAM → DSP
        │
        └──→ SPI / STORAGE
```

---

## 4. Ce que permet cette architecture

✔ Mixer scalable (3 → 4 entrées)
✔ FX dynamiques sans malloc
✔ DSP externe plug & play
✔ Séquenceur avec p-lock
✔ Sauvegarde projet complète
✔ Timing ultra stable audio-sync

---

## 5. Étapes de migration recommandées

### STEP 1

→ Introduire `control_router`

### STEP 2

→ Créer `param_store`

### STEP 3

→ brancher DSP sur params

### STEP 4

→ introduire FX pool

### STEP 5

→ ajouter storage

---

## 6. Philosophie

👉 Audio = sacré
👉 Param = centre du monde
👉 UI = client
👉 DSP = esclave déterministe

---

```

---
