# CPU load audit (STM32H743, DWT->CYCCNT)

## Scope audited
- `Src/cpu_load.c`
- `Inc/cpu_load.h`
- `Src/audio.c`
- `Src/stm32h7xx_it.c`
- `Src/dma.c`
- `Src/main.c`
- `Src/sai.c`

## Findings

### 1) DWT lock access register (LAR) is not handled
**Severity:** critical

**Where:** `Src/cpu_load.c` (`cpu_load_init`)

**Problem:**
`cpu_load_init()` enables `TRCENA` and sets `CYCCNTENA`, but never unlocks DWT via `DWT->LAR` on cores/devices where the DWT is write-locked after reset. On Cortex-M7 designs this can leave `CYCCNT` stuck at 0 (or unchanged), producing constant 0% load and a broken measurement chain.

**Fix:**
- Add conditional LAR unlock before writing `CYCCNT` / `CTRL`:
  - `#if defined(DWT_LAR)` then `DWT->LAR = 0xC5ACCE55;`
- Add a post-enable sanity check (`read1 != read0` after a few NOPs) and expose an `enabled`/`valid` flag.

---

### 2) Missing synchronization barriers after enabling trace/counter
**Severity:** major

**Where:** `Src/cpu_load.c` (`cpu_load_init`)

**Problem:**
After changing debug/trace control (`DEMCR`, `DWT->CTRL`), no `__DSB()` / `__ISB()` is issued. On Cortex-M7 pipeline/bus timing, this can cause first measurements to be inconsistent or stale.

**Fix:**
Insert barriers around enable sequence:
1. Enable TRCENA (+ optional LAR unlock)
2. `__DSB(); __ISB();`
3. Reset `CYCCNT`, set `CYCCNTENA`
4. `__DSB(); __ISB();`

---

### 3) “IRQ total” mode does not measure total IRQ cost
**Severity:** major

**Where:** `Src/audio.c`, `Src/stm32h7xx_it.c`

**Problem:**
In `CPU_LOAD_MODE_IRQ_TOTAL`, timing starts in `HAL_SAI_RxHalfCpltCallback` / `HAL_SAI_RxCpltCallback`. This excludes:
- IRQ entry/exit overhead
- `HAL_DMA_IRQHandler()` overhead before callback dispatch
- other DMA ISR work done in the same interrupt path

So reported value is not true “total IRQ” cost.

**Fix:**
- Start/stop timing in `DMA1_Stream1_IRQHandler()` around `HAL_DMA_IRQHandler(&hdma_sai1_b)` for RX-total measurement.
- Keep a second metric for DSP-only if needed.

---

### 4) TX DMA IRQ overhead is unmeasured
**Severity:** major

**Where:** `Src/dma.c`, `Src/stm32h7xx_it.c`, `Src/audio.c`

**Problem:**
Both DMA stream IRQs are enabled at high priority (`DMA1_Stream0_IRQn`, `DMA1_Stream1_IRQn`). CPU load timing is only updated in RX callbacks (`audio.c`). TX DMA interrupt handling cost (stream0) is therefore invisible in reported load, biasing system-level CPU load low.

**Fix:**
Either:
- measure both stream IRQ handlers and accumulate, or
- disable unnecessary TX half/full interrupts if no callback work is needed, or
- explicitly document metric as "RX DSP path only" and provide separate “audio ISR total” metric.

---

### 5) Preemption time is counted as audio CPU load
**Severity:** major

**Where:** `Src/audio.c` + NVIC config in `Src/dma.c`/other modules

**Problem:**
Elapsed cycles are wall-clock cycles between start/end reads. Any higher-priority interrupt that preempts the measured section is counted as audio load. This overestimates DSP/IRQ load and introduces jitter in measurements.

**Fix:**
- Set and enforce IRQ priorities to avoid unexpected preemption of audio DMA IRQ.
- Optionally keep a second metric measured with interrupts masked in a tiny benchmark window (for characterization only).
- Document metric semantics: "wall-time occupancy of callback window", not pure DSP cycles.

---

### 6) Budget uses nominal sample rate constant, not measured effective rate
**Severity:** minor (can become major if clocks are misconfigured)

**Where:** `Src/audio.c` (`AUDIO_SAMPLE_RATE_HZ = 48000U`), `Src/cpu_load.c` budget formula, `Src/main.c`/`Src/sai.c` clock tree + SAI setup

**Problem:**
Budget formula itself is correct:
`budget = SystemCoreClock * frames / sample_rate`
but `sample_rate` is a compile-time nominal constant. If effective SAI Fs differs (PLL fractional settings, codec clocking mismatch, drift, or alternate rate configuration), the computed load is biased.

**Fix:**
- Derive Fs from active SAI kernel clock and divider settings (or measured callback period).
- Recompute budget if clock tree or audio rate changes at runtime.

---

### 7) No guard against invalid CYCCNT operation at runtime
**Severity:** major

**Where:** `Src/cpu_load.c`

**Problem:**
No detection/reporting exists for cases where CYCCNT is not running (debug/trace disabled, lock not cleared, atypical low-power/debug state). The API continues returning values as if valid.

**Fix:**
- Add `cpu_load_is_valid()` flag updated during init and periodically checked.
- If invalid, expose sentinel values and increment an error counter.

---

### 8) Minor robustness issues in shared metrics
**Severity:** minor

**Where:** `Src/cpu_load.c`

**Problem:**
- `overruns_count` can wrap silently.
- `last_permille`, `max_permille`, `overruns_count` are individually atomic (32-bit), but multi-field snapshots in main loop are not coherent.

**Fix:**
- Saturate `overruns_count` at `UINT32_MAX` or use 64-bit.
- Add an optional snapshot getter with IRQ-off critical section to return coherent tuples.

---

## Checks requested by the audit brief

### DWT/CYCCNT init
- TRCENA and CYCCNTENA are set, but LAR unlock and barriers are missing.

### Measurement boundaries
- DSP-only mode brackets only `audio_process_block_int32()`.
- "IRQ total" mode brackets RX callback body, not full ISR path.

### IRQ execution model
- Hidden costs exist outside measured window (HAL DMA handler, IRQ prologue/epilogue, TX stream IRQ).

### Compiler/optimization
- Volatile peripheral register reads prevent easy elimination of timestamp reads.
- Still recommended: barriers in init sequence for architectural ordering.

### Budget calculation
- Formula implementation is mathematically correct.
- Inputs are potentially biased if configured Fs differs from effective Fs.
- Frames-per-half is consistent with DMA half-size in current code.

### Data integrity
- 32-bit wrap handling for CYCCNT delta (`end - start`) is correct.
- 64-bit arithmetic prevents overflow in permille calculation.

### Consistency expectations
- Heavy synthetic load inside measured region should increase permille.
- If not, likely CYCCNT not running (critical findings #1/#7) or wrong bracketing mode.

## Why strange behavior can still happen even if formula is right
- If `CYCCNT` is not incrementing, readings stay near zero regardless of workload.
- If workload is added outside brackets (e.g., ISR overhead, TX IRQ), displayed load barely changes.
- If preemption occurs, load can spike without DSP changes.
- If effective Fs differs from 48 kHz nominal, baseline shifts proportionally.

## Two independent validation methods (minimum)

1. **GPIO pulse width + oscilloscope/logic analyzer**
   - Set GPIO high at measurement start, low at end.
   - Observe pulse width versus block period (`frames/Fs`).
   - Do this for DSP-only and ISR-total placements.

2. **Independent free-running timer (TIM2/TIM5 at known clock)**
   - Capture timer ticks at same start/end points.
   - Compare computed load against DWT-based load.

3. **Synthetic workload sweep (recommended additional)**
   - Add deterministic busy-loop in measured block with multiple levels.
   - Verify near-linear increase in permille and matching slope against expected cycles.

