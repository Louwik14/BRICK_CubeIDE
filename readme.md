# 🎛️ Embedded Audio Engine – Product Overview

## 1. Vision

Standalone embedded audio machine inspired by the Elektron Octatrack.

Core use:
- real-time audio mixer for external synths
- creative FX box for live performance
- cue / main style routing
- crossfader-oriented performance workflow
- integrated MIDI sequencer with parameter locks and modulation

This repository targets a playable, deterministic instrument firmware, not a generic DSP sandbox.

---

## 2. Product priorities

Priority order:
1. audio stability
2. predictable CPU usage
3. live-playable UX
4. flexible but controlled routing
5. maintainable codebase

Guiding rule:
- if it risks audio stability, reject it
- if it breaks worst-case predictability, rethink it
- if it improves live performance without breaking invariants, prioritize it

---

## 3. Hardware and runtime constraints

### Hardware
- MCU: STM32H743 @ 480 MHz
- codec: CS42448 via SAI TDM
- audio format: 24-bit / 48 kHz
- DMA: double buffer (ping-pong)
- block size: 64 or 128 frames depending on configuration

### Runtime model
- audio processing runs in IRQ
- no RTOS
- no dynamic allocation in the critical runtime path
- no blocking calls in the hard real-time path

These constraints are structural, not optional.

---

## 4. High-level product model

### Tracks
The product is track-aware.

Current model:
- 14 logical tracks in UI / runtime / sequencer / logical mixer space
- physical DSP ingress remains distinct from logical tracks
- logical track identity and physical audio lane must not be conflated

Current logical layout:
- 8 flexible musical tracks
- 4 input-oriented tracks
- 2 reserved master/global slots

### Families
Current families:
- `Off`
- `Input1`
- `Input2`
- `Input3`
- `Input4`
- `Synth`
- `Drum`
- `Master`

### Notable types
- `InputX`: `Audio`, `Hybrid`
- `Synth`: `Sampler`
- `Drum`: dedicated drum catalog
- `Master`: `Buffer`

---

## 5. Current feature shape

### Audio / mixer
- track-aware audio routing
- main / cue separation
- sends and returns
- insert-style processing
- master-oriented performance processing

### Sequencer
- integrated sequencer
- transport / clock / scheduler
- parameter locks
- modulation baseline
- live-performance oriented behavior

### UI
- contextual UI based on active track family/type/runtime
- track-aware page exposure
- hall-based interaction model
- keyboard / arp / pattern / mute workflows
- boot default (normal path): track 1 focused on `CFG` (hall calibration path stays prioritary)

### Parameter system
- UI-side parameter control
- runtime-side application
- modulation and track-aware filtering of valid targets

### Persistence
- pattern live state
- pattern save/load
- project save/load
- boot context restore

---

## 6. Current special case: Master/Buffer

`Master/Buffer` is a special track identity currently under integration.

Current intent:
- unique `Master` family / `Buffer` type
- single instance in a project
- reuse of the existing buffer backend
- capture selected logical track sources
- replay through the normal track-aware system
- no second concurrent recorder architecture

Current exposed controls:
- `TRACK + REC` → record/start buffer workflow
- `TRACK + SHIFT + REC` → clear buffer
- `ARP` context becomes `ROUT` for source selection
- `TONE` exposes buffer-specific parameters

`Master/Buffer` is still considered integration in progress.

---

## 7. What this repo is optimizing for

This project is optimizing for:
- deterministic embedded behavior
- bounded real-time cost
- coherent track-aware routing
- strong live interaction model
- incremental evolution of the existing codebase

This project is not optimizing for:
- abstract architecture purity at any cost
- speculative redesigns
- feature growth that breaks timing guarantees
- convenience patterns that add hidden authorities

---

## 8. Documentation map

Use the documents according to their role:

- `AGENT.md`
  - work rules
  - modification discipline
  - global invariants to respect

- `ARCHITECTURE_GLOBAL.md`
  - orientation map
  - which architecture zone to read first

- `docs/architecture/z*.md`
  - detailed zone-level architecture
  - real authorities, boundaries, dependencies

This `README.md` is intentionally product-oriented.
It is not the authoritative architecture document.

---

## 9. Current status

The codebase already contains:
- 14 logical tracks
- track-aware runtime binding
- contextual UI families
- sequencer / clock / scheduler foundations
- parameter and modulation infrastructure
- persistence layers
- ongoing `Master/Buffer` integration

Some areas are stable, others are still under active stabilization.
When in doubt, trust the code and the architecture zone documents before broad assumptions.

---

## 10. Principle

Keep it simple, deterministic, and playable.
