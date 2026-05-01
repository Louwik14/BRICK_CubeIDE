# Embedded Audio Engine - Product Overview

## 1. Vision

Standalone embedded audio machine for live use.

The project is track-aware by construction: the meaningful unit is the logical track, not a hidden global node and not a physical lane.

Core use:
- real-time audio mixer for external synths
- performance FX and routing
- cue / main style routing
- track-aware sequencer and modulation
- contextual UI driven by the active track identity

This repository targets a playable, deterministic instrument firmware. It is not a generic DSP sandbox and it does not rely on ambiguous central nodes.

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
- `Sampler`

### Notable types
- `InputX`: `Audio`, `Hybrid`
- `Synth`: `Plaits`
- `Sampler`: `OneShot`, `Slicer`, `Clip`
- `Drum`: dedicated drum catalog
- `Master`: `Buffer`

### Ownership model

The architecture is organized around three distinct layers:
- canonical control state
- runtime projection
- execution

Features should hook into the layer that owns the decision:
- canonical control state for source-of-truth edits
- runtime projection for track-aware binding and capability resolution
- execution for hard real-time audio, transport, and other bounded runtime work

This separation is intentional. Do not add a second authority for the same state.

## 5. Current feature shape

### Audio / mixer
- track-aware audio routing
- main / cue separation
- sends and returns
- insert-style processing
- master-oriented performance processing

### Sampler
- stereo runtime playback through the normal track-aware mixer path
- paged sample cache with RAM-only audio reads
- `READY_FULL` and `READY_PARTIAL` served from sampler-owned SDRAM pages
- `OneShot` currently exposes only `Shot`, `RevShot`, `Loop`, and `PingPong`
- `Clip` now exposes `Sample`, `Gain`, `Src BPM`, `Play Mode`, `Loop`, `Stretch`, and `Sync Len`
- `Clip` supports forward `Gate`/`Launch` playback with three stretch modes:
  - `Off`: 1x playback
  - `Speed`: varispeed (`ratio = project_bpm / source_bpm`), pitch changes
  - `Stretch`: preserve-pitch via the local `brick6_clip_stretch` OLA path
- `Sync Len` remains exposed for clip timing configuration; `Stretch=Off` stays 1x playback, `Stretch=Speed` keeps the existing varispeed path, and `Stretch=Stretch` keeps the existing WSOLA path
- legacy slice handling remains internal compatibility, not a product mode

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

## 6. Current special case: Master/Buffer

`Master/Buffer` is a special track identity.

Current intent:
- unique `Master` family / `Buffer` type
- single instance in a project
- reuse of the existing buffer backend
- capture selected logical track sources
- replay through the normal track-aware system
- no second concurrent recorder architecture

Current exposed controls:
- `TRACK + REC` -> record/start buffer workflow
- `TRACK + SHIFT + REC` -> clear buffer
- `ARP` context becomes `ROUT` for source selection
- `TONE` exposes buffer-specific parameters
- `TONE` buffer pages now include local timestretch controls (`TStr`, `Grain`, `Hop`, `Sync Len`, `Src BPM`, `Pitch`) on the recorded-buffer playback path only

`Master/Buffer` is an explicit track identity, not a global mode.

## 7. What must stay true

The project should keep these rules visible in new work:
- every feature should declare its owner layer
- runtime seams must stay explicit
- track-aware behavior must remain the default reasoning model
- future dual-core work should be prepared by clean seams, not by premature IPC or central buses
- avoid hidden coupling through ambiguous shared nodes
- reuse existing authorities before creating new ones

When adding a feature, hall mode, engine, UI behavior, or runtime seam, prefer the smallest change that preserves these boundaries.

## 8. What this repo is optimizing for

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

## 9. Documentation map

Use the documents according to their role:

- `AGENT.md`
  - work rules
  - modification discipline
  - global invariants to respect

- `docs/architecture/ARCHITECTURE_GLOBAL.md`
  - orientation map
  - which architecture zone to read first

- `docs/architecture/z*.md`
  - detailed zone-level architecture
  - real authorities, boundaries, dependencies

This `README.md` is intentionally product-oriented.
It is not the authoritative architecture document.

## 10. Current status

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

## 11. Principle

Keep it simple, deterministic, and playable.
