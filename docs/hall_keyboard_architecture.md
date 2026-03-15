# Hall Keyboard Scanning Architecture Review (STM32H743)

## 1) System constraints recap

- Audio DSP executes in the audio DMA IRQ and is the top-priority real-time workload.
- Audio IRQ latency/jitter must remain tightly bounded and unaffected by control-plane work.
- No RTOS: all non-audio work runs via deterministic superloop + tasklets.
- Hall keyboard uses 2 external 8:1 analog muxes feeding ADC1/ADC2 (16 keys total).
- One mux index yields two key samples (one per ADC).
- Velocity depends on precise time between threshold crossings; target practical timing granularity is around 100 µs or better.

These constraints mean keyboard scanning must be deterministic, bounded, and explicitly subordinated to the audio IRQ.

---

## 2) ISR architecture comparison

### Option A — 100 µs burst-scan ISR

**Behavior**
- Timer ISR every 100 µs (10 kHz).
- Each ISR scans all 8 mux positions:
  - set mux address,
  - wait fixed settle delay (~6–8 µs),
  - trigger/read ADC1 + ADC2,
  - process 16 key samples.

**Pros**
- Simple scheduling model (1 ISR = 1 full frame).
- Natural frame-level coherency (all keys sampled within one bounded window).
- Easy to reason about and instrument (WCET, overrun).
- Low interrupt rate (10 k IRQ/s).

**Cons**
- ISR execution time is longer (order tens of microseconds, implementation-dependent).
- Longer contiguous occupancy of CPU at that priority.

**Audio interaction**
- If keyboard ISR priority is lower than audio IRQ, audio preempts safely.
- Main risk is not blocking audio (it will preempt), but added preemption nesting and potential pressure on other lower-priority ISRs.

### Option B — 12.5 µs incremental ISR (8 phases)

**Behavior**
- Timer ISR at ~80 kHz.
- Each ISR handles one mux position (2 keys).
- Full frame completes after 8 ticks (~100 µs).

**Pros**
- Very short ISR per invocation (small worst-case contiguous occupancy).
- Lower single-hit interference for peer interrupts.

**Cons**
- 8x interrupt entry/exit overhead.
- Higher global interrupt traffic and complexity.
- Greater implementation complexity (phase state machine, partial frame consistency, fault handling).
- Frame coherence is split across 8 substeps.

**Audio interaction**
- Audio IRQ still preempts if priority is lower.
- High ISR frequency increases opportunities for preemption churn and cache/pipeline disturbance.

### Practical verdict for STM32H743

For 16 keys and ~100 µs full-frame target, **Option A (100 µs burst ISR)** is generally the better first implementation:
- fewer interrupts,
- simpler determinism argument,
- easier maintenance and debugging,
- likely enough CPU headroom on H743.

Use Option B only if real measurements show Option A burst WCET causes unacceptable latency to non-audio peripherals or misses scan deadlines.

---

## 3) ADC strategy with external muxes: polling vs ADC IRQ vs DMA

With external muxes, each sample step is constrained by:
1. GPIO mux address update,
2. analog settle delay,
3. ADC conversion/read.

The settle delay and sequencing dominate architecture choices more than ADC data movement.

### Polling ADC (recommended baseline)
- In a timer-driven bounded ISR, start conversion(s), wait with tight bounded loops, read results.
- Minimal moving parts, highly deterministic for small sample count.
- Very suitable for 2 ADC values × 8 steps per frame.

### ADC end-of-conversion IRQ
- Adds extra IRQs and scheduling complexity.
- Usually not beneficial for such small batches.
- Can increase jitter due to nested interrupt choreography.

### ADC DMA
- Useful when acquiring long streams or large bursts.
- For muxed scan with per-step GPIO+settle control, DMA advantage is limited unless full hardware pipeline is engineered.
- More setup complexity for little gain at 16 channels/frame.

**Recommendation:** start with **timer ISR + direct ADC control/polling**. Consider DMA only after profiling proves a concrete benefit.

---

## 4) Timer architecture recommendation

- **Primary scan timer frequency:** 10 kHz (period = 100 µs).
- **ISR duration target:** design for bounded WCET comfortably below period (preferably with measurable margin).
- **Priority:** keyboard timer IRQ strictly lower (numerically larger priority value) than audio DMA IRQ; higher than non-critical service interrupts as needed.
- **Overrun handling:** maintain overrun/late counters; never spin indefinitely.

Implementation notes:
- Keep per-step settle delay fixed and calibrated.
- Use fixed loop bounds (always 8 mux steps).
- No dynamic allocation, no lock contention in ISR.

---

## 5) Timestamping strategy for velocity

### Candidate sources

1. **DWT cycle counter (`DWT->CYCCNT`)**
   - Very high resolution (CPU cycle-level).
   - Excellent for short interval timing.
   - Requires enabling trace/cycle counter and careful wrap handling.

2. **Free-running hardware timer (1 MHz or higher)**
   - Stable, explicit hardware timebase.
   - Easy conversion to microseconds.
   - Robust and portable in firmware architecture.

3. **SysTick-based time**
   - Typically too coarse or noisy for this use.
   - Not recommended for microsecond velocity timing.

### Recommendation

Use **a dedicated free-running timer** as primary system timestamp for keyboard events (e.g., 1 MHz or higher), optionally cross-checked by DWT during profiling.

Why this is best here:
- explicit peripheral timebase independent of CPU clock changes,
- easy wrap-safe arithmetic,
- straightforward sharing across ISR and main context.

Store raw timestamp ticks at threshold crossings in ISR; derive velocity in main/control path.

---

## 6) ISR ↔ main-loop data exchange design

Goals:
- no blocking,
- no large critical sections,
- no race conditions,
- deterministic handoff.

### Preferred pattern: double-buffered snapshot + event ring

Use two structures:

1. **Frame snapshot buffer (double buffer)**
   - ISR writes full per-key analog/state snapshot into write buffer.
   - On frame completion, ISR atomically publishes buffer index/sequence.
   - Main loop reads latest complete snapshot without touching ISR active buffer.

2. **SPSC event ring (optional but recommended)**
   - ISR pushes compact edge events (press/release/threshold crossings + timestamp).
   - Main loop drains ring each `engine_tick()`.

Why both:
- Snapshot supports aftertouch/continuous position consumption.
- Ring preserves precise temporal events for velocity/note logic.

### Concurrency rules

- Single producer (ISR), single consumer (main).
- Use atomic index/sequence publication (or IRQ-safe volatile protocol with memory barriers).
- No memcpy of large blocks inside critical sections.
- Overflow counters for ring/snapshot misses.

---

## 7) Recommended module structure

Suggested module boundaries:

- `hall_kbd_init()`
  - configure mux GPIOs, ADC1/ADC2, scan timer, timestamp timer,
  - initialize buffers/FSM/calibration.

- `hall_kbd_scan_isr()`
  - runs at 10 kHz,
  - performs bounded 8-step scan,
  - updates lightweight per-key FSM,
  - records threshold timestamps,
  - publishes snapshot and/or events.

- `hall_kbd_poll()` (called from control path, e.g., `engine_tick()`)
  - consume latest snapshot/events,
  - compute higher-level outputs (velocity curves, filtering, mapping),
  - expose stable key state API.

- `hall_kbd_get_debug_stats()`
  - WCET max, jitter stats, overruns, ring overflow counters.

---

## 8) Final integrated architecture (recommended)

```text
Audio DMA IRQ (highest priority)
    -> audio DSP only (no keyboard work)

Keyboard Scan Timer IRQ @ 10 kHz (lower priority than audio)
    -> for mux_idx = 0..7
         set mux address
         wait fixed settle delay
         sample ADC1 + ADC2
         update per-key raw sample/state machine
         capture threshold timestamp (free-running timer)
    -> publish frame snapshot + edge events (lock-free SPSC/double buffer)

Main superloop
    -> engine_tasklet_poll()
         -> engine_tick() @ ~1500 Hz
             -> hall_kbd_poll()
                  consume snapshots/events
                  compute velocity/aftertouch outputs
                  dispatch note/control state
    -> other tasks (USB/MIDI/UI)
```

This architecture preserves audio dominance, keeps keyboard timing deterministic, and provides microsecond-grade event timing for velocity.

---

## 9) Bring-up and verification checklist

- Verify keyboard ISR never exceeds configured time budget (scope GPIO + cycle/timer stats).
- Verify audio IRQ latency unchanged under worst-case keyboard activity.
- Measure scan period jitter and frame overrun count over long runs.
- Validate velocity repeatability with controlled key actuation tests.
- Confirm ring/snapshot overflow counters stay at zero in nominal operation.

If measurements fail margins:
1. reduce ISR internal work (defer math to main),
2. optimize ADC setup/settle constants,
3. only then consider incremental scan or DMA-assisted variants.
