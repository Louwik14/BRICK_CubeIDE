# STM32H743 WAV Streaming Audit (Simplified Architecture)

## Scope
This audit focuses on reliable real-time streaming of **large 24-bit / 48 kHz stereo WAV files** from SD card to the audio callback path with minimal moving parts.

---

## 1) Current architecture: layers between SD and DSP

### Observed data path in project
1. **WAV discovery + physical location lookup**
   - `wav_loader_find_first_wav()` scans root directory with FatFs.
   - `wav_get_physical_location()` opens file and derives first data sector from starting cluster.
2. **Custom SD streamer**
   - `sd_stream_start_read(start_block, total_blocks)` starts a custom FSM.
   - Streaming uses `HAL_SDEx_ReadBlocksDMAMultiBuffer()` into two DMA buffers (`Buffer0/Buffer1`, 4096 B each).
3. **Intermediate block ring**
   - `sd_stream_process_ready_buffers()` copies DMA buffers into `sd_audio_block_ring` (`AUDIO_BLOCK_COUNT=4`, `AUDIO_BLOCK_SIZE=4096`).
4. **PCM unpack stage**
   - `sampler_stream_update()` consumes ring blocks, re-packs bytes into 24-bit stereo frames (6 bytes/frame), converts to float, and writes into a second circular sample buffer `stream_buffer`.
5. **Voice / DSP callback**
   - `sample_voice_process()` pulls from float circular buffer in DSP callback context (`my_dsp()`).
   - `audio.c` IRQ callbacks call `audio_process_block_int32()` every DMA half-buffer.

### Modules currently in file path
- Storage: `wav_loader`, `sd_stream`, `sd_callbacks`, `sd_owner`
- Inter-buffer transport: `sd_audio_block_ring`
- Stream decode/fill: `sampler_stream`
- Audio consumer: `sampler`, `brick6_app_init` DSP callback, `audio` / `audio_float`

---

## 2) Unnecessary complexity / risk areas

### A. Too many buffering stages for first reliable version
Current path has **three buffering layers** after SD DMA:
1. SD DMA double buffer (`Buffer0/Buffer1`)
2. Audio block ring (`sd_audio_block_ring`)
3. Float sample ring (`stream_buffer`)

For a first robust streamer this is over-layered and harder to debug latency/backpressure.

### B. Physical-sector streaming approach is fragile
`wav_get_physical_location()` computes first sector from first cluster and then streams sequential blocks. This implicitly assumes practical contiguity and bypasses FatFs file-level reads for the stream body. For reliability-first bring-up, this adds risk and debugging complexity versus plain `f_read()` on an open `FIL`.

### C. Ownership multiplexing adds coordination burden
`sd_owner` + callbacks route SD completion by owner mode (`FATFS` vs `STREAM`). This can work, but it complicates timing interactions when other code touches SD, and increases shared-state/race surface.

### D. Mixed callback/tasklet pipeline increases race potential
Producer/consumer indexes and flags are updated across IRQ callback + main loop tasklets + DSP callback. Even if mostly single-word writes, there are many synchronization points:
- SD DMA completion flags
- FSM state transitions
- ring fill/read/write indices
- stream read/write indices

### E. Heavy conversion path before DSP
24-bit unpack + float conversion happens in `sampler_stream_update()` while DSP consumes another buffer. For first success, avoid extra queueing and conversion layers unless required by final DSP format.

---

## 3) Proposed minimal streaming architecture (reliability-first)

### Target pipeline
**SD card → FatFs `f_read()` → fixed PCM staging buffer (ping/pong) → audio callback consumer → DAC/codec**

### Key simplifications
1. **Use FatFs file streaming directly (`f_open`, `f_lseek(data_offset)`, `f_read`)**.
2. **Use one buffering abstraction only**: ping/pong chunk buffers in RAM.
3. **Remove custom SD FSM for first milestone** (or bypass it entirely).
4. **No physical block math** for normal playback path.
5. **Single stream state object** with explicit counters and deterministic transitions.

This makes each failure mode obvious: `f_read` latency, starvation, format mismatch, or conversion budget.

---

## 4) Buffering strategy recommendation

### Compared
- **Ring buffer**
  - + Flexible occupancy
  - - More index logic, wrap bugs, watermark complexity
- **Double buffer (ping/pong)**
  - + Very deterministic, easiest to reason about, natural with DMA half/full rhythm
  - + Easiest first-debug instrumentation
  - - Less elasticity than a large ring
- **Chunk refill (single buffer burst)**
  - + Simplest code
  - - Highest underrun risk (no overlap margin)

### Recommendation for STM32H7 first working version
Use **double buffer + prefill**:
- 2 large PCM chunks (e.g. each 16 KB or 32 KB)
- Audio callback consumes active chunk
- Main/task context refills inactive chunk when marked empty
- Start playback only after both chunks are filled once

Then, if needed, extend to 3-4 chunk ring later.

---

## 5) SD read strategy (simple + robust)

### Chunk size
- Start with **16 KB** per `f_read()` (safe baseline)
- If stable, try **32 KB**
- Avoid tiny reads (<2 KB) due to overhead/jitter

At 48k stereo 24-bit, throughput is only ~288 KB/s, so chunking for latency margin matters more than raw bandwidth.

### Alignment
- Buffer base aligned to **32 bytes** (cache line)
- Chunk size multiple of **32 bytes** and ideally **512 bytes**

### Memory placement
- **Do not place DMA-visible streaming buffers in DTCM** (DMA cannot access DTCM)
- Prefer **RAM_D2 / AXI-accessible SRAM** for active I/O buffers
- SDRAM can be used for larger secondary buffers, but start with internal SRAM for deterministic bring-up

### DCache guidance
- If DCache remains OFF during bring-up, behavior is simpler.
- If DCache ON, perform cache maintenance around DMA-visible buffers (clean before DMA write-out, invalidate after DMA read-in as applicable).
- Keep all DMA buffers 32-byte aligned and lengths rounded to cache line size.

---

## 6) Minimal module architecture

### `wav_parser.c/.h`
Responsibilities:
- Open WAV file
- Parse RIFF chunks (`fmt`, `data`)
- Validate: PCM, stereo, 48k, 24-bit (or reject)
- Return `data_offset`, `data_size`, bytes/frame

### `sd_reader.c/.h`
Responsibilities:
- Own `FIL` handle and sequential `f_read`
- Fill inactive ping/pong chunk
- Track EOF and short reads
- Expose timing counters (read us, max us)

### `audio_streamer.c/.h`
Responsibilities:
- Stream state machine: STOPPED / PREFILL / RUN / UNDERRUN / EOF
- Buffer ownership flags (chunk A/B full/empty)
- Consumer API for audio callback (`get_next_frame()` or block pull)
- Underrun accounting and watermarks

This separation keeps parsing, I/O, and real-time consumption independent.

---

## 7) Minimal streaming pseudocode

```c
init_stream(path):
    wav = wav_parse(path)
    assert(wav.sr == 48000 && wav.bits == 24 && wav.ch == 2)
    sd_reader_open(path)
    sd_reader_seek(wav.data_offset)

    fill_chunk(A)
    fill_chunk(B)
    if (!A.full || !B.full) return ERROR

    streamer.state = RUN
    streamer.play_chunk = A
    streamer.play_offset = 0

audio_callback(frames):
    for i in 0..frames-1:
        if current chunk empty:
            mark it EMPTY (for refill task)
            switch to other chunk if FULL
            else:
                output silence
                underrun_count++
                continue

        read 6 bytes -> int24 L/R -> float -> output
        advance play_offset

main_loop_task():
    if chunk A is EMPTY and !A.refilling and !eof:
        fill_chunk(A)
    if chunk B is EMPTY and !B.refilling and !eof:
        fill_chunk(B)

    update watermarks / latency stats
```

---

## 8) Common STM32H7 pitfalls to check explicitly

1. **DCache coherency**
   - DMA + cacheable SRAM without maintenance leads to stale/corrupt samples.
2. **DMA-inaccessible memory**
   - DTCM is CPU-only; SDMMC/SAI DMA must use DMA-accessible RAM.
3. **Alignment**
   - Unaligned buffers break/slow cache maintenance and can cause SD DMA issues.
4. **FatFs blocking behavior**
   - `f_read()` can block for variable time; mitigate with chunk prefill and sufficient buffer headroom.
5. **Doing too much in IRQ**
   - Keep audio IRQ deterministic; no filesystem calls in callback.
6. **Assuming cluster contiguity**
   - Physical-sector shortcut can fail on fragmented files.

---

## 9) Minimal instrumentation (must-have)

Expose a compact `stream_stats` struct and print periodically (not every callback):
- `underrun_count`
- `chunk_a_full`, `chunk_b_full`
- `active_chunk`, `play_offset`
- `min_watermark_bytes`
- `last_read_us`, `max_read_us`
- `reads_ok`, `reads_err`, `short_reads`

Recommended checks:
- warn if watermark drops below one audio callback block worth of data
- log any read latency above threshold (e.g. >4 ms)
- log state transitions only (`PREFILL->RUN`, `RUN->UNDERRUN`, etc.)

---

## 10) Clean reference implementation outline for groovebox sampler

### Phase 1 (bring-up, single voice)
- One WAV file, stereo 24/48 only
- Ping/pong PCM byte buffers
- One consumer in DSP callback
- Silence on underrun + counter
- No seeks, no polyphony

### Phase 2 (musical usability)
- Add restart/loop point (file seek at loop boundary)
- Add optional third chunk if SD jitter observed
- Add simple voice allocator while reusing same streamer primitive

### Phase 3 (feature growth)
- Multiple streamers or shared cache
- Prefetch queue and background decode
- Optional block-level ring if truly needed

The key principle: **first ship a tiny pipeline that is impossible to misunderstand**, then scale up.

---

## Practical simplification recommendation for current codebase

For the next implementation pass, temporarily bypass these modules from playback path:
- `sd_stream` (custom SD DMA/FSM)
- `sd_audio_block_ring`
- `sampler_stream` (intermediate conversion ring)
- `wav_get_physical_location` (physical sector shortcut)

Keep only:
- `wav_loader` chunk parser logic (or extract to `wav_parser`)
- one new `audio_streamer` with ping/pong `f_read` refill
- existing DSP callback/audio output path

This minimizes moving parts and should make underrun root causes immediately observable.
