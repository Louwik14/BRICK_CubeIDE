Architecture summary

    fx_reverb wraps Freeverb as a C API around a C++ revmodel object (fx_reverb_t contains revmodel model + bypass flag). The reverb state is a static singleton returned by fx_reverb_get_instance(), so no runtime allocation is required in the audio path.

    Although the API is block-based (fx_reverb_process_block(..., frames)), implementation is currently sample-looped: for each frame, it calls revmodel::processreplace(..., numsamples=1, skip=1). That adds per-sample function-call/loop overhead.

    Freeverb core is classic stereo topology: 8 comb + 4 allpass per side (16 comb + 8 allpass total), with combs in parallel then allpasses in series.

    Delay-line buffers are embedded directly inside revmodel (large float arrays), i.e., static/object memory, not heap. From tuning constants, this is ~25,450 floats (~101.8 KB) for delay lines alone.

    In the current DSP pipeline, reverb is not invoked: audio_dsp_process runs EQ → saturation → granular → mix, and no fx_reverb_process_block call appears in that path; my_dsp() currently only calls mixer_process().

Performance issues (ranked)

    Per-sample wrapper call pattern in fx_reverb_process_block (worst avoidable overhead).

    High algorithmic workload per sample: 16 comb + 8 allpass updates/sample with feedback memory traffic.

    Large delay-state footprint (~100 KB) may spill to slower SRAM/SDRAM depending linker placement, increasing cache miss/stall risk on M7.

    Parameter update cost bursts: each setwet/setroomsize/setdamp/... calls update(), which loops over all combs and writes coefficients twice (feedback + damp). Fine for control-rate, risky if called too often.

    Build/abstraction hygiene issue: .cpp files are included directly in fx_reverb.cpp, which hurts modularity/tooling and can block compiler-level optimizations across proper units.

Real-time suitability (STM32H743)

    If reverb is inserted as-is, it is likely workable on H743 FPU for moderate block sizes, but headroom depends heavily on where delay buffers land (AXI SRAM vs SDRAM) and current ISR budget.

    Determinism is generally good (no malloc/free in audio thread), but jitter risk rises with cache-unfriendly delay accesses and high-frequency parameter writes.

    Important behavioral note: bypass currently writes zeros (effect send-style), not dry passthrough; ensure this matches routing expectations when integrating.

Optimization roadmap (step-by-step)
Quick wins (no sound impact)

    True block call: replace per-frame processreplace(...,1,1) with one call for full frames.

        Estimated CPU gain: 5–12% in reverb wrapper overhead.

    Control-rate parameter batching: only call setters when value changed (and possibly once per block).

        Estimated CPU gain: 1–5% overall, more if UI sends dense updates.

    Proper compilation units: stop including comb/allpass/revmodel.cpp directly; compile separately with -O3 -ffast-math (if acceptable).

        Estimated CPU gain: 0–5% runtime, major maintainability gain.

Medium changes (small sound impact)

    Memory placement optimization: keep hot filter structs/coefs in DTCM, delay lines in fast AXI SRAM (avoid SDRAM for core delays).

        Estimated CPU gain: 10–30% (highly placement-dependent).

    Slight topology reduction mode (e.g., 6 comb / 3 allpass per side for “eco”).

        Estimated CPU gain: 20–35%, with mild tail-density loss.

    Control smoothing at low rate (e.g., 250–1kHz update) to avoid frequent full update() passes.

        Estimated CPU gain: 2–8% during heavy UI interaction.

Aggressive (CPU-focused)

    Mono reverb core + stereo decorrelation/widening output matrix (instead of full dual network).

        Estimated CPU gain: 35–50%; stereo richness reduced but usually acceptable for groovebox use.

    Dual-quality modes (normal/full vs eco/live).

        Estimated CPU gain: dynamic; enables predictable anti-overrun behavior under load.

    Fixed-point rewrite only if absolutely needed (high dev risk on M7 where float is already strong).

        Potential gain: 0–25% depending implementation; often not worth complexity vs float on H743.

STM32H7 best practices

    Use FPU-friendly float path (current code already float), but ensure flush-to-zero/denormal handling stays active; Freeverb uses manual denormal kill macros in comb/allpass.

    Keep audio ISR work strictly bounded per block; prefer block-level calls and fixed-size loops (already deterministic in structure).

    Ensure linker script places reverb delay state away from SDRAM unless unavoidable; SDRAM is a common overrun source for feedback-delay DSP.

    CMSIS-DSP: limited direct benefit for this topology (recursive feedback loops), but useful around surrounding stages (mixing/vector ops, format conversion), not the comb/allpass recursion itself.

    Add cycle instrumentation (DWT CYCCNT) around reverb block once integrated to verify worst-case ISR margin