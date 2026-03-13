# Audio Architecture Migration Audit (CS42448/TDM8 -> dual AK4619/TDM4)

## Scope
- Analysis limited to user code and USER CODE sections.
- CubeMX-generated peripheral init code intentionally excluded.

---

## Section 1 – Architecture assumptions found in the code

### 1.1 Hard assumptions of TDM8 / 8-slot framing
- `audio.c` hard-codes 8 slots per frame (`AUDIO_TDM_SLOTS = 8`) and derives DMA buffer sizes from this. The comments and naming explicitly target "CS42448 TDM8". (`AUDIO_WORDS_PER_FRAME = AUDIO_TDM_SLOTS`, `AUDIO_BUFFER_WORDS = frames_total * words_per_frame`).
- `audio_io.c` hard-codes `AUDIO_TDM_SLOTS 8U` and advances RX/TX pointers by 8 words per frame.
- `audio_io_unpack()` reads only slots `0..5`, implicitly ignoring `6/7`; this is aligned with a single TDM8 stream where only 3 stereo tracks are consumed.
- `audio_io_pack()` writes MAIN/CUE to slots `0..3`, then forces slots `4..7` to zero.
- `audio_float.c` file-level documentation explicitly defines the legacy map: track0->slots 0/1, track1->2/3, track2->4/5, slots 6/7 unused in RX, and TX slots 4..7 zeroed.
- `audio_float.h` still declares the DSP boundary as TDM8 and sets `MAX_TRACKS` to 3, matching the 6 used slots pattern.

### 1.2 Hard assumptions of single-SAI audio path
- `audio.c` keeps exactly one TX handle and one RX handle (`sai_tx`, `sai_rx`) and a single RX/TX ping-pong DMA buffer pair.
- `audio_init()` accepts only one TX + one RX handle.
- `audio_start()` starts one RX DMA and one TX DMA transfer only.
- HAL SAI RX callbacks (`HAL_SAI_RxHalfCpltCallback`, `HAL_SAI_RxCpltCallback`) process only when `hsai == sai_rx` (single ingress source).
- `brick6_app_init.c` binds audio to SAI1 only via `audio_init(&hsai_BlockA1, &hsai_BlockB1);` despite SAI2 being available in project handles.

### 1.3 Channel indexing / slot ordering assumptions
- Input channel extraction is positional (`s0..s5` -> track0/1/2 L/R in fixed order) in `audio_io_unpack()`.
- Output channel ordering is positional (`ptx[0..3]` = mainL/mainR/cueL/cueR, `ptx[4..7]=0`) in `audio_io_pack()`.
- Downstream code assumes bus results are returned through `tracks[0]` (MAIN) and `tracks[1]` (CUE) before packing.

### 1.4 Codec-specific assumptions (CS42448)
- Application init includes and calls `CS42448_Init(0x48)` directly.
- `cs42448.c` is CS42448-only and encodes CS42448 register map + TDM-specific defaults (`CS42448_TDM_FUNCTIONAL_MODE`, `CS42448_TDM_INTERFACE_FORMAT`).
- Main user includes still reference `cs42448.h`.

### 1.5 Timing assumptions coupled to previous architecture
- Audio IRQ budget is documented around 64-frame half-buffer @48kHz (~1.33ms).
- `AUDIO_FRAMES_PER_HALF` is fixed to 64 and coupled to `AUDIO_BLOCK_SIZE` (64).
- `engine_tasklet_notify_frames()` is called once per RX half/full callback from single SAI RX source.
- `engine_tasklet_init(48000)` and DSP FX init paths assume fixed 48kHz sample rate.

---

## Section 2 – Code sections that must be refactored

### 2.1 Low-level transport / DMA front-end
- **`Src/Audio/audio.c`**
  - Must evolve from one RX/TX stream to dual-stream coordination (two SAI peripherals).
  - Must remove 8-slot sizing assumptions in buffer geometry.
  - Must define synchronization strategy when two RX callback sources are active (which callback triggers block processing, and only once per logical frame block).

### 2.2 TDM unpack/pack and channel map
- **`Src/Audio/audio_io.c`**
  - Must split or generalize the pack/unpack mapping for two TDM4 links.
  - Must replace fixed slot stepping by architecture-aware stepping per stream.
  - Must redefine where MAIN/CUE/aux channels are emitted across two codec outputs.

### 2.3 DSP boundary contracts and track model
- **`Inc/Audio/audio_float.h` + `Src/Audio/audio_float.c`**
  - Must update contract text and likely constants (`MAX_TRACKS` and block/channel mapping) if total available channels are redistributed across dual codecs.
  - `audio_process_block_int32()` currently expects one contiguous RX and one contiguous TX block; this boundary may need to ingest/emit two stream buffers.

### 2.4 Application bootstrap + codec bring-up
- **`Src/Core/brick6_app_init.c`**
  - Replace CS42448 init call with initialization of two AK4619 instances (addresses/reset pins/I2C handle assignment).
  - Replace single audio binding (`hsai_BlockA1/B1`) with dual-SAI audio path wiring.

### 2.5 Codec drivers / abstraction layer
- **`Drivers/Drv_app/Src/cs42448.c`** + call sites
  - Legacy driver path is architecture-specific and should be retired from active startup path.
- **`Drivers/Drv_app/Src/ak4619.c` + `Inc/ak4619.h`**
  - Existing AK4619 driver supports one handle at a time and is reusable, but startup/orchestration for two codecs is missing in app init.

### 2.6 Callbacks / ISR-triggered processing
- **`Src/Audio/audio.c` callbacks**
  - Must be reworked for dual RX DMA callback sources so block processing and scheduler frame accounting remain coherent and non-duplicated.

---

## Section 3 – Code sections that can remain unchanged

### 3.1 Modules not tied to slot count or SAI instance count
- `mixer.c` processing is track/bus based and not directly coupled to physical SAI slot numbering.
- `fx_chain.c`, `fx_pool.c`, and most DSP effects remain channel-buffer based and architecture-agnostic as long as upstream/downstream buffer contracts stay consistent.
- Recorder/control plumbing (`sd_multitrack_recorder`, control events/tasklets) appears block-oriented and can remain if block size/timing contract is preserved.

### 3.2 Potentially reusable codec code
- `ak4619.c` low-level reset/read/write/init routines are generic per-handle and can remain as a base driver.

### 3.3 Interrupt vector wiring already present for both SAIs
- DMA IRQ handlers for both SAI1 and SAI2 streams are present and call `HAL_DMA_IRQHandler` for each DMA handle; this infrastructure can remain if higher-level callbacks are adapted.

---

## Section 4 – Potential hidden risks (timing, DMA, buffer alignment)

1. **Dual-callback race / double-processing risk**
   - If both SAI RX streams trigger half/full IRQ independently and each path runs full DSP, logical audio time may advance twice per real block.

2. **Inter-stream desynchronization risk**
   - Two SAI peripherals may not reach half/full DMA boundaries at exactly the same cycle unless tightly clocked and started coherently. Without explicit synchronization, channel grouping can drift between codecs.

3. **Scheduler frame accounting drift**
   - `engine_tasklet_notify_frames()` currently increments per callback in single-stream model; dual-stream handling can overcount frames if called per SAI callback without aggregation.

4. **Buffer geometry mismatch risk**
   - Existing DMA buffer dimensions are derived from 8-slot framing. Any partial migration (e.g., changing CubeMX only) can produce wrong DMA lengths, indexing overruns, or silent channel misrouting in user code.

5. **Slot-map semantic regressions**
   - Current output intentionally zeroes slots 4..7. In new architecture those paths may become active outputs; leaving zero-fill logic unchanged can mute expected channels.

6. **Codec startup sequencing risk**
   - Old startup includes CS42448 delays and power/mute choreography. Two AK4619 codecs require deterministic dual reset/init order; missing one device init can appear as intermittent channel loss.

7. **Fixed-rate assumptions**
   - FX init and scheduler initialization use hard-coded `48000`. If final dual-codec clocking changes rate, filters/compressor time constants and tasklet timing become incorrect.

8. **DMA/cache coherency constraints remain critical**
   - DMA buffers are 32-byte aligned via `DMA_BUFFER`; any new buffers for second stream must keep the same memory placement/alignment policy.

---

## Section 5 – Estimated migration difficulty per component

| Component | Difficulty | Why |
|---|---|---|
| `audio.c` (DMA/SAI front-end + callbacks) | **High** | Core architectural shift: single-stream -> dual-stream synchronized processing, callback arbitration, frame accounting. |
| `audio_io.c` (pack/unpack slot mapping) | **High** | Hard-coded TDM8 indices and zero-fill logic must be redesigned for two TDM4 links and new channel map. |
| `audio_float.[ch]` boundary/constants | **Medium-High** | Track/channel contract likely changes; may require API and buffer-interface adjustments while preserving DSP behavior. |
| `brick6_app_init.c` bootstrap wiring | **Medium** | Replace codec init path and bind dual SAI handles; conceptually straightforward but startup sequencing sensitive. |
| Codec drivers (`cs42448`, `ak4619`) | **Low-Medium** | AK4619 driver exists; integration and dual-instance orchestration required. CS42448 path removal is simple. |
| Mixer/FX core (`mixer`, `fx_*`) | **Low** | Mostly independent from physical transport; should remain with minimal/no change if track contract preserved. |
| IRQ vector file (`stm32h7xx_it.c`) | **Low** | DMA handlers for both SAI already present; likely no direct changes beyond ensuring callback-level logic correctness elsewhere. |

---

## Overall migration outlook
- **Primary effort concentration**: audio transport boundary (`audio.c` + `audio_io.c`) and startup wiring (`brick6_app_init.c`).
- **Secondary effort**: contract alignment in `audio_float` and robust dual-stream synchronization policy.
- **Lower effort**: DSP effect modules and mixer internals, provided channel contract remains stable.
