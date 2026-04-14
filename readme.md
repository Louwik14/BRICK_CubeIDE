# 🎛️ Embedded Audio Engine – Octatrack-Lite Vision

## 🎯 Product Vision

A standalone embedded audio machine inspired by Elektron Octatrack:

* 🎚️ Real-time **audio mixer** for external synths
* 🎛️ **Creative FX box** (DJ-style transitions, performance FX)
* 🎚️ **Cue / Main routing** like a DJ mixer
* 🎚️ **Crossfader with scenes (A/B morphing)**
* 🎹 Integrated **MIDI sequencer + parameter locks + modulation** baseline

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

* 14 logical tracks (UI / runtime / sequencer / mixer targets)
* Product audio inputs: `Input1..Input4` = 4 physical stereo resources
* Current devboard proto wiring: 3 physical stereo inputs effectively fed to DSP ingress
* DSP ingress frontier today: 4 lanes total (`MAX_TRACKS`), with lane 3 used as internal source path
* Mixer logical track capacity has been extended to 14 tracks
* Physical DSP ingress and logical mixer/runtime tracks remain distinct concepts

#### Current logical track layout

* 8 flexible musical tracks
* 4 input-oriented tracks
* 2 reserved master/global slots

#### Current special runtime cases

* `DX7` remains an exclusive synth-engine type
* `Master/Buffer` is a unique special track type in progress
  * only one instance is allowed in a project
  * it reuses the existing buffer backend rather than introducing a second concurrent recorder
  * it is intended to capture selected track sources post-fader and replay them through the normal track-aware system

### Track families / engine families (runtime policy)

* `Synth` and `Drum` are distinct engine families (track-aware routing authority remains `track_runtime`).
* `Drum` is **not** an alias of `Synth`.
* `DX7` and `MonoB` remain synth-engine types in the `Synth` family.
* `Drum` has its own runtime model catalog (`TRX` + `FM` drum models) and does not reuse `Synth` types.
* Mono/poly behavior for `PLAY` remains centralized by runtime capability declarations.

#### Special family / type currently being integrated

* `Master` is a dedicated non-standard family for global/special tracks.
* `Buffer` is the current `Master` type under integration.
* `Master/Buffer` is handled as a real track identity in the runtime, with dedicated parameter domain and dedicated UI contexts.
* `Master/Buffer` is exclusive-source / singleton-like, in the same spirit as other exclusive runtime resources.

### UI families actually exposed (track-aware)

* `TONE`
  * `Synth` types keep dedicated tone pages (`DX7`, `MonoB`).
  * `Drum` has a dedicated dynamic `TONE` catalog per active drum type (TRX/FM variants).
  * `Master/Buffer` uses a dedicated contextual `TONE` family with buffer-specific parameters.

* `COLORS`
  * Shared audio domain for active tracks.
  * On engine tracks (including `Drum`), `COLORS` exposes standard audio processing pages (`MAIN` with filter/EQ depending on filter mode, optional `MOD`, `CRUNCH` drive).

* `MOD`
  * LFO destinations are filtered by active track family/type/runtime status.
  * Only valid `TONE` / `COLORS` parameters are selectable for the active track context.
  * No cross-engine fallback list.

* `MIX`
  * Remains track-aware and available for routable tracks, including `Master/Buffer`.

* `PLAY`
  * Remains capability-driven.
  * `Master/Buffer` does not expose the normal `PLAY` page.

* Contextual controls in progress for `Master/Buffer`
  * `ARP` hall context becomes `ROUT` for source selection
  * `KBD` behavior is intentionally kept conservative until the dedicated performance workflow is finalized

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

## 🎚️ Crossfader

* The system already contains a crossfader-oriented architecture.
* Scene-style parameter morphing remains part of the broader design direction.
* In the current branch, `Master/Buffer` is intended to use a dedicated `XFade` parameter to blend between:
  * live selected sources
  * recorded buffer playback
* This blend is treated as part of the buffer workflow, not as a separate second recorder system.

### Design intent

* Avoid double-listening / double-summing when blending live and recorded material
* Keep the blend deterministic and track-aware
* Reuse the existing buffer backend and crossfade-oriented infrastructure rather than creating parallel systems


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

### Master/Buffer controls (integration in progress)

* `TRACK + REC`:
  * arms / starts `Master/Buffer` recording
  * should work independently of the currently focused track by targeting the unique `Master/Buffer` instance

* `TRACK + SHIFT + REC`:
  * clears the `Master/Buffer` contents

* `ARP` on `Master/Buffer` becomes `ROUT`
  * halls `1..14` represent logical track sources
  * lit hall = source routed into the buffer
  * dark hall = source not routed into the buffer

* `TONE` on `Master/Buffer`
  * Page 1: `Rec Len`, `Q Rec`, `Q Play`, `Rate`
  * Page 2: `Fade In`, `Fade Out`, `XFade`

> Note: `Master/Buffer` integration is still under active stabilization in the current branch. UI/routing/LED behavior should be considered work in progress until the remaining bugs are resolved.

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
  * `DX7` synth instances are move-on-paste when pasting to another track
  * `Input1..4`: paste uses a free input family first; otherwise move-on-paste
  * clipboard source is updated after move, enabling chained pastes from the same clipboard object

### Not Prioritized (yet)

* Micro-optimizations
* SIMD / fixed-point
* RTOS migration

---

## 🔮 Future Features

* Advanced sequencer pages/workflows
* Further stabilization of `Master/Buffer`
* Dedicated `Master FX` track/block integration
* LFO / modulation expansion
* Presets / scenes memory
* Additional performance-oriented sampling / buffer workflows
---

## 🧪 Current branch status

The current branch has already introduced:

* 14 logical tracks in UI / runtime / logical mixer space
* exclusive `Master/Buffer` track identity
* dedicated buffer parameter domain
* dedicated buffer `TONE` pages
* ongoing integration of buffer source routing, recording workflow, playback blend, and recorder LED state machine

The `Master/Buffer` feature should currently be considered **integration in progress**, not fully stabilized product behavior.

---

## 🧭 Guiding Principle

> “Keep it simple, deterministic, and playable.”

* If it risks audio stability → reject
* If it complicates worst-case timing → rethink
* If it helps live performance → prioritize

---
