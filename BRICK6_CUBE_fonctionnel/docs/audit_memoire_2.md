# BRICK6_CUBE_fonctionnel — Safe real-time memory reorganization plan (STM32H743)

## 1) Full memory mapping

### 1.1 Linker regions and actual placement

From Release map:

* `RAM_D1` (AXI SRAM) = `0x24000000`, 512 KB.
* `DTCMRAM` = 128 KB, `RAM_D2` = 288 KB, `RAM_D3` = 64 KB are declared but not used for app `.data/.bss` in current Release link.
* Current placement in `RAM_D1`:

  * `.data` = `0xA8` (168 B)
  * `.bss` = `0x613EC` (398,316 B)
  * `.ram_d1` = `0x2000` (8,192 B)
  * `._user_heap_stack` = `0x600` (1,536 B = 512 B heap + 1 KB stack reserve)

**RAM_D1 static occupancy** ≈ 408 KB before real runtime stack growth.

### 1.2 Large buffers (>1 KB)

| Buffer / symbol                                   |                      Size | Location now        | Evidence                         |
| ------------------------------------------------- | ------------------------: | ------------------- | -------------------------------- |
| `(anonymous namespace)::g_state` (granular state) | `0x5DDEC` = **384,492 B** | `.bss` in RAM_D1    | `Release/BRICK6_CUBE.map` / `nm` |
| `tx_buffer` (audio DMA ping-pong)                 |        `0x1000` = 4,096 B | `.bss` in RAM_D1    | `audio.c`, map                   |
| `rx_buffer` (audio DMA ping-pong)                 |        `0x1000` = 4,096 B | `.bss` in RAM_D1    | `audio.c`, map                   |
| `Buffer0` (SD DMA DBM)                            |        `0x1000` = 4,096 B | `.ram_d1` in RAM_D1 | `sd_stream.c`, map               |
| `Buffer1` (SD DMA DBM)                            |        `0x1000` = 4,096 B | `.ram_d1` in RAM_D1 | `sd_stream.c`, map               |
| `tracks` (audio float track buffers)              |         `0x60C` = 1,548 B | `.bss` in RAM_D1    | map                              |
| `drv_display` framebuffer (`buffer`)              |         `0x400` = 1,024 B | `.bss` in RAM_D1    | map                              |

### 1.3 Biggest consumers (clear ranking)

1. **Granular `g_state`: 384,492 B** (dominant consumer).
2. Audio DMA `rx+tx`: 8,192 B.
3. SD DBM `Buffer0+Buffer1`: 8,192 B.
4. Tracks: 1,548 B.

---

## 2) Access pattern analysis (critical)

### 2.1 Audio IRQ path confirmation

Audio processing executes in DMA RX IRQ callbacks (`HAL_SAI_RxHalfCpltCallback`, `HAL_SAI_RxCpltCallback`) and calls `audio_process_block_int32()` on every half-buffer (64 frames). This is hard real-time.

### 2.2 Per-buffer access characterization

| Buffer                                           | In audio IRQ?  | Frequency                           | Access pattern                                             | Determinism sensitivity |
| ------------------------------------------------ | -------------- | ----------------------------------- | ---------------------------------------------------------- | ----------------------- |
| `rx_buffer` / `tx_buffer`                        | **Yes**        | Per-block (64f), DMA + CPU each IRQ | Sequential contiguous half-buffer                          | **Very high**           |
| `tracks` + `bus_main_*` + `bus_cue_*`            | **Yes**        | Per-sample inside block loop        | Sequential per frame                                       | **Very high**           |
| `g_state.buffer_l/r` (inside granular)           | **Yes**        | Per-sample + per-active-grain reads | Mixed: sequential writes + quasi-random interpolated reads | **Highest risk**        |
| `g_state` metadata (`grains`, counters, indices) | **Yes**        | Per-sample/per-grain                | Mostly linear over small arrays                            | High                    |
| SD `Buffer0/Buffer1`                             | No (audio IRQ) | SD DMA callbacks + tasklet          | Chunk/ block based, sequential                             | Medium/low              |
| `drv_display buffer`                             | No             | UI tasklet                          | sequential/random UI                                       | Low                     |

---

## 3) HOT / WARM / COLD classification

### HOT (must stay in fast internal RAM)

* `rx_buffer`, `tx_buffer`.
* `tracks`, `bus_main_*`, `bus_cue_*`, small DSP states used each sample.
* Granular metadata needed each iteration (`grains[]`, counters, write/read heads, mix params).

### WARM (can move with caution)

* SD DBM buffers `Buffer0/Buffer1` (not in audio IRQ, but DMA-active and throughput sensitive).
* Non-audio service buffers that can tolerate extra latency and are not scheduling-critical.

### COLD (safe SDRAM candidates)

* Large long-history audio memories not directly required for deterministic core arithmetic each instruction cycle.
* In this project, primary candidate: **granular long circular audio history (`buffer_l` / `buffer_r`)** with strict validation.
* UI/diagnostic buffers.

---

## 4) SDRAM migration plan (safe, deterministic-first)

## Target memory layout

* **Internal RAM (RAM_D1/DTCM as needed):**

  * All HOT real-time block buffers and control state.
  * IRQ-executed DSP core states and DMA audio ping-pong.
* **SDRAM:**

  * Large history/tail buffers only.
  * Non-IRQ service buffers where latency jitter is acceptable.

### Move order (safe-first)

### Move #1 (lowest risk)

* Move `drv_display buffer` (1 KB) and other UI/diagnostic non-IRQ buffers to SDRAM.
* Benefit: tiny but zero audio risk; validates SDRAM sectioning/toolchain flow.

### Move #2 (low risk, useful)

* Move SD-stream staging buffers `Buffer0`/`Buffer1` (currently `.ram_d1`) to SDRAM **only if SD DMA path remains stable**.
* Keep strict DMA/cache coherency policy if D-cache ever enabled later.
* Benefit: frees 8 KB RAM_D1.

### Move #3 (major RAM relief, highest risk)

* Split granular memory:

  * keep HOT granular metadata internal.
  * migrate only long history arrays (`buffer_l`/`buffer_r`) to SDRAM.
* This move unlocks ~384 KB-class RAM but must be validated in worst-case density/pitch/freeze settings.

### Must NEVER move

* `rx_buffer` / `tx_buffer` audio DMA ping-pong.
* Block-working sets directly touched every sample in audio IRQ (`tracks`, buses, tight DSP states).

---

## 5) Risk analysis per proposed move

| Proposed move                | Glitch risk   | Latency/cache impact                                                            | Worst-case scenario                                          |
| ---------------------------- | ------------- | ------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| Move UI/display buffers      | Very low      | Negligible for audio                                                            | None on audio path                                           |
| Move SD `Buffer0/1`          | Low to medium | External RAM latency on SD side; DMA coherency concern if D-cache enabled later | SD underrun/throughput drop, but audio IRQ core still stable |
| Move granular history arrays | **High**      | Extra SDRAM latency on per-sample/per-grain reads; jitter from FMC/refresh      | IRQ budget overrun in dense grains => audible xruns/glitches |

### Additional architectural risks to track

* Current firmware enables I-Cache only; D-Cache is not enabled now. If D-Cache is enabled later, SDRAM/DMA buffers need explicit coherency handling.
* Stack reserve is only 1 KB in linker; functional but narrow safety margin for future complexity.

---

## 6) Priority roadmap (actionable sequence)

1. **First safe move**

   * Relocate non-audio non-DMA UI/diagnostic buffers to SDRAM.
   * Verify no change in audio CPU load and no artifacts.

2. **Second move**

   * Relocate SD-stream `Buffer0/Buffer1` to SDRAM.
   * Stress SD read/write + audio concurrently; verify no refill starvation.

3. **Third move (major)**

   * Partial granular split (metadata internal, history buffers SDRAM).
   * Validate with worst-case presets (max density, high stereo offset, freeze on/off, fast pitch shifts).

4. **Optional optimizations (after stability)**

   * Use internal RAM partitioning (DTCM for hottest scalars/states where possible).
   * Revisit granular read strategy to improve locality before/while using SDRAM.

---

## Final decisions (real-time safety rules)

* Determinism > memory savings.
* Keep the entire audio IRQ hot path in internal RAM.
* Move only buffers that are outside strict per-sample deadlines first.
* Treat granular-history migration as a controlled high-risk change, not a first step.
