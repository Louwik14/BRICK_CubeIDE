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
- `Synth`: `Braids`
- `Sampler`: `OneShot`, `Slicer`, `Clip`, `Looper`, `Multi`
- `Drum`: dedicated drum catalog
- `Master`: `FX`

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
- `Master/FX` existe comme contrat UI MacroFX 4 slots, sans traitement DSP actif dans cette passe

### Sampler
- stereo runtime playback through the normal track-aware mixer path
- paged sample cache with RAM-only audio reads
- `READY_FULL` and `READY_PARTIAL` served from sampler-owned SDRAM pages
- `OneShot` currently exposes `Shot`, `RevShot`, `Loop`, and `PingPong`; the active minimal RAM path plays global `kind=RAM/READY` slots from `sampler_ram_pool`, honors p-locked `Start`/`End`, and supports p-locked `RevShot`
- `Slicer` minimal playback is RAM-only: regular slices inside the p-locked `Start`/`End` region from global `kind=RAM/READY` slots, selected by `note % slice_count`
- `Clip` now exposes `Sample`, `Gain`, `Src BPM`, `Play Mode`, `Loop`, `Stretch`, and `Sync Len`
- `Clip` supports forward `Gate`/`Launch` playback with three stretch modes:
  - `Off`: 1x playback
  - `Speed`: varispeed (`ratio = project_bpm / source_bpm`), pitch changes
  - `Shifter`: varispeed cursor followed by the local stereo pitch-shifter
- `Sync Len` remains exposed for clip timing configuration; `Stretch=Off` stays 1x playback, `Stretch=Speed` keeps the existing varispeed path, and `Stretch=Shifter` uses `Grain` as the shifter window while `Hop/Search` are stored but inactive
- `Looper` TONE exposes `ARM` (`Off`/`Rec`/`Overd`), `LEN` (`Free`/`1`/`2`/`4`/`8`/`16`), and `PLAY` (`Off`/`Auto`); current implementation records simple `ARM=Rec` takes, keeps `ARM=Overd` as a bounded no-op until audio overdub exists, and streams playback from transient page-cache pages when `PLAY=Auto`
- `Multi` exposes TONE `INST` / `GAIN` / `LOOP`; `LOOP=ON` loops active notes from valid WAV `smpl` bounds, otherwise forced import-time auto-loop bounds with a mechanical 40%/55% fallback for usable WAVs
- Multi Browser page 3 `CLEAR` deletes only visible `.brickmulti` indexes in the current Multi folder, so indexes can be regenerated without deleting WAVs
- legacy slice handling remains internal compatibility, not a product mode

### Braids
- `Synth/Braids` is a track-aware mono engine exposed on `TONE`
- `TONE/EDIT`: `Edit`, `Fine`, `Coarse`, `FM`
- `TONE/TONE`: `Timbre`, `Modulation`, `Color`, `Phase Reset`
- `Phase Reset=Off` preserves the current Braids behavior
- `Phase Reset=On` sends a one-shot sync pulse on the first rendered sample after note-on for Braids models that consume sync; random state is not reset

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
- no product VU/peak meter in the mixer header
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



## 7. What must stay true

The project should keep these rules visible in new work:
- every feature should declare its owner layer
- runtime seams must stay explicit
- track-aware behavior must remain the default reasoning model
- future dual-core work should be prepared by clean seams, not by premature IPC or central buses
- avoid hidden coupling through ambiguous shared nodes
- reuse existing authorities before creating new ones

When adding a feature, hall mode, engine, UI behavior, or runtime seam, prefer the smallest change that preserves these boundaries.

## 7.1 Sampler/Looper

`Sampler/Looper` is an audio-routable Sampler type with a dedicated transient runtime:
- `ARM=Rec` records/replaces one loop take, then returns to `Off`
- `ARM=Overd` is visible but remains no-op in this pass
- `PLAY=Off` keeps the captured loop ready but silent
- `PLAY=Auto` starts playback with transport after the take is finalized and its first page is ready
- `SAVE` commits the temp WAV to `PROJECT/LOOPS` without routing playback through project sample slots

Looper playback does not full-load the WAV, does not use the project `sample_pool`, does not depend on the WAV catalogue, and does not reuse normal Sampler slots. It streams temp/final WAV files through transient `sample_page_cache` ids; audio IRQ reads only ready RAM pages and falls silent locally if a page is missing.

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

Some areas are stable, others are still under active stabilization.
When in doubt, trust the code and the architecture zone documents before broad assumptions.

## 11. Principle

Keep it simple, deterministic, and playable.

## Master track status

`Master/FX` is the only exposed Master track type. The former buffer workflow has been removed; Looper XFade remains available on `Sampler/Looper`.
