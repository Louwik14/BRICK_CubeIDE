# SD Streaming Loop Gap Audit (STM32H7 + FatFs)

## Executive conclusion

Most likely root cause: **producer starvation at loop wrap due blocking filesystem I/O in a non-real-time context**, not a math bug in ring indexing.

At EOF, refill must do extra control-path work (`f_lseek` + first post-wrap `f_read`) before new loop-start audio is published. That work runs inside `audio_streamer_process()` (main loop), while audio consumption is hard real-time in IRQ (`audio_streamer_get_frame()` via DSP callback). If SD/FAT latency spikes at wrap, the ring drains to empty and IRQ enters underrun behavior.

The design is therefore latency-sensitive at exactly the loop boundary.

## What the code actually does (timing model)

1. Audio IRQ consumes exactly 1 frame per `audio_streamer_get_frame()` call and never blocks.
2. Refill runs only from main loop (`brick6_app_process()` -> `audio_streamer_process()`).
3. Refill performs blocking `f_read()` calls and, at EOF, blocking `f_lseek()` back to data start.
4. Refill publishes `write_pos` only after `write_frames_from_file()` exits, so frames decoded early in that function are invisible to IRQ until return.
5. If `rp == wp`, consumer reports underrun and repeats last sample.

## Why the gap appears specifically at loop

Loop point is the only deterministic place where refill path adds a **control discontinuity**:

- Normal steady state: one forward sequential `f_read`.
- Loop boundary: tail read -> EOF detect -> `f_lseek(data_offset)` -> head read.

That extra path increases worst-case service time exactly at wrap. In this architecture, any long main-loop stall (USB host work + FatFs/SD latency burst) directly converts to producer starvation. With 16,384-frame ring and 14,336 target fill, practical headroom is ~299 ms at 48 kHz; a larger latency burst causes audible interruption.

## Ring/EOF audit summary

- Ring math (`ring_used_frames`, `ring_space_frames`) is internally consistent for a 1-slot-empty SPSC ring.
- EOF/data-size logic clamps to whole frames and wraps to `data_offset`; no obvious off-by-one that would create ~1 s silence by itself.
- 64-frame DSP block size is not a mismatch source; consumption remains deterministic.
- Cache coherence is unlikely primary cause in this build (I-Cache enabled; D-Cache not enabled in `main.c`).

## Secondary amplifier

`write_frames_from_file()` keeps `write_pos` local and commits once at end. During any long call (especially around EOF wrap), already-decoded frames are not visible to IRQ until function returns. This reduces effective buffering during stalls.

## How to verify with instrumentation

Add low-overhead counters/timestamps (DWT cycle counter preferred):

1. `audio_streamer_process()`:
   - time between entries (`process_jitter_max_us`)
   - duration of each call (`process_exec_max_us`)
2. `write_frames_from_file()`:
   - per-call max `f_read` latency
   - per-call `f_lseek` latency
   - count/time of wrap events
3. IRQ side (`audio_streamer_get_frame()`):
   - underrun timestamps
   - minimum observed ring fill since last wrap
4. Correlate events:
   - wrap timestamp
   - latency spike
   - fill collapse to zero
   - underrun burst

A scope-friendly option: pulse GPIO A around wrap (`f_lseek`/first read), GPIO B on underrun. If B follows A by ~buffer drain time, the mechanism is confirmed.

## Correct architectural fix (not a patch)

Move streaming to a **deadline-aware producer architecture**:

- Dedicated high-priority producer context (task/RT thread) owns SD/FatFs I/O.
- Producer performs larger sequential prefetch windows and explicit loop-preload (tail+head assembled before deadline).
- Consumer IRQ reads from lock-free SPSC audio FIFO only (no file-system coupling).
- Use occupancy deadlines (e.g., must keep >X ms buffered) and telemetry-driven backpressure.
- Optionally split into two stages:
  1) file-byte ring (SD worker)
  2) PCM-frame ring (decoder)
  so FAT latency and decode latency are isolated.

Key design goal: **filesystem latency must be absorbed outside the IRQ service window**, including at loop wrap.
