# 🎛️ Embedded Audio Engine – Octatrack-Lite Vision

## 🎯 Product Vision

A standalone embedded audio machine inspired by Elektron Octatrack:

* 🎚️ Real-time **audio mixer** for external synths
* 🎛️ **Creative FX box** (DJ-style transitions, performance FX)
* 🎚️ **Cue / Main routing** like a DJ mixer
* 🎚️ **Crossfader with scenes (A/B morphing)**
* 🎹 Future: **MIDI sequencer + parameter locks + modulation**

---

## 🧠 Core Philosophy

Priority order:

1. **Audio stability (hard real-time)**
2. **Predictable CPU usage (bounded worst-case)**
3. **Performance-oriented UX (live playable)**
4. **Flexible but controlled routing**
5. **Simple, maintainable codebase**

---

## ⚙️ Hardware Context

* MCU: STM32H743 @ 480 MHz
* Audio codec:

  * Current: CS42448 (TDM8 via SAI)
  * Future: Dual codec (2× SAI, TDM4 each)
* Audio format:

  * 24-bit, 48 kHz
* DMA:

  * Double buffer (ping-pong)
* Block size:

  * 64 or 128 frames (configurable)

---

## ⏱️ Real-Time Model

* Audio processing runs **entirely in IRQ**
* No RTOS
* No dynamic allocation
* No blocking calls

### Timing Budget

| Block Size | Time Budget |
| ---------- | ----------- |
| 32         | 0.67 ms     |
| 64         | 1.33 ms     |
| 128        | 2.67 ms     |

👉 Larger blocks = more DSP headroom, less IRQ pressure
👉 Smaller blocks = lower latency, tighter constraints

---

## 🧱 Audio Architecture (Target)

### Tracks

* 8 logical tracks (UI / sequencer / runtime / mixer targets)
* Product audio inputs: `Input1..Input4` = 4 physical stereo resources
* Current devboard proto wiring: 3 physical stereo inputs effectively fed to DSP ingress
* DSP ingress frontier today: 4 lanes total (`MAX_TRACKS`), with lane 3 used as internal source path
* Each track:

  * Input source (ADC / TDM for `InputX`, internal render for `Synth`)
  * Gain (main + cue)
  * Sends (to FX buses)
  * Insert FX chain (fixed slots)

### Track families / engine families (runtime policy)

* `Synth` and `Drum` are two distinct engine families (track-aware routing authority remains `track_runtime`).
* `Drum` is **not** an alias of `Synth`.
* `DX7`, `MonoB`, `TB3` remain synth-engine types in the `Synth` family.
* `Drum` has its own runtime model catalog (`TRX` + `FM` drum models) and does not reuse `Synth` types.
* Mono/poly behavior for `PLAY` remains centralized by runtime capability declarations (no conceptual merge between `Synth` and `Drum`).

### UI families actually exposed (track-aware)

* `TONE`:
  * `Synth` types keep dedicated tone pages (`DX7`, `MonoB`, `TB3`).
  * `Drum` has a dedicated dynamic `TONE` catalog per active drum type (TRX/FM variants).
* `COLORS`:
  * Shared audio domain for active tracks.
  * On engine tracks (including `Drum`), `COLORS` exposes standard audio processing pages (`MAIN` with filter/EQ depending on filter mode, optional `MOD`, `CRUNCH` drive).
* `MOD`:
  * LFO destinations are filtered by **active track family+type+runtime status**.
  * Only valid `TONE` / `COLORS` parameters are selectable for the active track context.
  * No cross-engine fallback list.

---

### Buses

* `MAIN` (stereo output)
* `CUE` (independent or mirrored)
* `SEND0` (e.g. delay)
* `SEND1` (e.g. reverb)

---

### FX Structure

#### Inserts (per track)

* Fixed number of slots (e.g. 3)
* Light FX only:

  * EQ
  * Filter
  * Saturation
  * Compressor

#### Sends (global)

* Heavy FX:

  * Delay
  * Reverb
* Shared across tracks

#### Master FX

* 2 slots
* Used for transitions / global processing

---

## 🔁 Processing Pipeline (per block)

1. Unpack TDM → track buffers (float)
2. Apply track processing:

   * gain / pan / mute
   * insert FX chain
3. Accumulate:

   * MAIN bus
   * CUE bus
   * SEND buses
4. Process send FX:

   * SEND → FX → RETURN
5. Mix returns into MAIN / CUE
6. Apply master FX
7. Output limiting / soft clipping
8. Pack float → TDM

---

## 🎚️ Crossfader (Scenes A/B)

* Global crossfader (Elektron-style)
* Each parameter has:

  * Scene A value
  * Scene B value

### Runtime behavior

* At block start:

  * Interpolate parameters:
    `value = lerp(A, B, x)`
* Apply smoothing inside DSP where needed

### Affects:

* Track gains
* Send levels
* FX parameters
* Master FX

---

## 🔄 FX Management Strategy

### Key Constraint

👉 CPU must remain **predictable and bounded**

### Approach

* Large pool of FX algorithms available in firmware
* BUT only a **fixed number of active slots**:

| Category      | Slots        |
| ------------- | ------------ |
| Track inserts | 3 × 4 tracks |
| Master FX     | 2            |
| Sends         | 2            |

👉 Prevents CPU explosion regardless of user configuration

---

## 🔥 Hot FX Switching (No Pop Strategy)

When changing FX type in a slot:

1. Fade out current FX (few ms)
2. Reset / initialize new FX
3. Fade in

Alternative (advanced):

* Dual FX crossfade (short overlap)

---

## 🧠 Parameter System

* UI writes to **shadow parameters**
* Audio IRQ uses **snapshot per block**
* Smoothing applied in DSP

Avoids:

* race conditions
* zipper noise
* inconsistent state

---

## 🎯 Performance Model

### CPU Load Measurement

* Based on DWT CYCCNT
* Measures **DSP cost only** (not IRQ overhead)

### Design Goal

* Stay well below 100% per block
* Maintain headroom for:

  * parameter spikes
  * UI interaction
  * FX complexity

---

## 🚀 Optimization Strategy

### Current State

* DSP in IRQ → stable
* CPU ~10–15%
* No glitches

### Focus Areas (when needed)

* Memory access patterns
* Branch reduction
* Buffer layout (interleaved vs split)
* Avoid unnecessary clears/memset

---

## 🎛️ Current performance controls (implemented behavior)

### Hall modes / quick access

* `SHIFT + -` → `PATTERN RECALL`
* `TRACK + -` → `PATTERN STORE`
* `SHIFT + +` (hold `SHIFT`, press/hold `+`) → temporary `MUTE` hall mode:
  * active tracks visible on halls
  * muted tracks shown as muted state
  * `Off` tracks stay dark
  * hall press toggles mute immediately in quick mode
  * releasing `+` exits to previous hall mode
* While still holding `+` in quick mute, pressing `SHIFT` enters `MUTE PREPARE`:
  * initial mute snapshot is captured
  * hall presses edit prepared states only
  * prepared differences vs snapshot are marked as blinking states
  * pressing `+` validates prepared mutes and exits to previous mode

### Clipboard / copy-paste / clear

* `TRACK + COPY`:
  * copies active track config + runtime-allowed params
  * does **not** copy sequencer steps
* `TRACK + PASTE`:
  * pastes last copied track
* `TRACK + SHIFT + PASTE`:
  * clears active track to defaults (family `Off`, default params, empty sequence)
* Parameter scopes (same keys, context-sensitive):
  * hold a `PARAM` button + `COPY/PASTE` → copy/paste full ensemble
  * hold active `PAGE` button + `COPY/PASTE` → copy/paste active page
  * `SHIFT + PASTE` in these scopes clears all targeted params to each param minimum
* Ensemble/page paste compatibility is by intersection of common `param_id` (not strict identical layout matching).
* Exclusive-source paste policy:
  * `DX7` / `TB3` synth instances are move-on-paste when pasting to another track
  * `Input1..4`: paste uses a free input family first; otherwise move-on-paste
  * clipboard source is updated after move, enabling chained pastes from the same clipboard object

### Not Prioritized (yet)

* Micro-optimizations
* SIMD / fixed-point
* RTOS migration

---

## 🔮 Future Features

* MIDI sequencer (multi-track)
* Arpeggiator
* Parameter locks (per step)
* LFO / modulation
* Presets / scenes memory

---

## ⚠️ Non-Goals (for now)

* Dynamic DSP graph
* Unlimited FX chaining
* RTOS-based scheduling
* Sample playback engine

---

## 🧭 Guiding Principle

> “Keep it simple, deterministic, and playable.”

* If it risks audio stability → reject
* If it complicates worst-case timing → rethink
* If it helps live performance → prioritize

---
