# Prism contract

Prism exposes two pitch-modulation amounts, `P.MOD1` and `P.MOD2`, on the
`MOD / PHASE` page. `P.MOD` is the depth of pitch modulation by `AMOD`, from
zero to a maximum positive offset of 24 semitones:

`pitch offset = P.MOD * AMOD * 24 st`

This control is not audio-rate FM and does not cross-modulate the two Prism
oscillators. `PHASE` is one global trigger phase-reset switch for both oscillators.

`DETUNE` is the fixed bipolar pitch offset applied to OSC2 only (-24 to +24
semitones). `DRIFT` adds an independent random pitch offset to each oscillator at NOTE ON.
The offsets are held for the voice lifetime, are not mean-centred, and range up
to approximately +/-12 cents per oscillator.

For the temporary Braids A/B test, the `MOD / PHASE` UI exposes a local `RATE`
switch (`48K`/`96K`) in the former `DRIFT` slot. `DRIFT` retains its parameter ID,
backend, modulation route and project persistence; stored values still apply at
NOTE ON. `RATE` defaults to `48K` after runtime initialization and is not stored
in projects.

## Braids block clock

The BRICK callback remains 64 samples. Every Prism `MacroOscillator` generates
an atomic 24-sample Braids block and retains its unconsumed output in a local
24-sample cache. BRICK consumes that cache across callback boundaries; it never
splits a Braids render. Model preparation, block-rate decisions and parameter
interpolators therefore run at 2 kHz in `48K` and at the native 4 kHz in `96K`.
In particular,
the VOWEL consonant counter, WAVE_LINE smoothing/crossfade, GRANULAR_CLOUD grain
renewal, and WAVETABLES/CLOCKED_NOISE hysteresis do not treat a BRICK boundary
as a new Braids block. Model scratch storage is shared because oscillators are
rendered sequentially; cached audio remains private to each oscillator. In `96K`,
each 64-frame BRICK callback consumes 128 internal Braids samples. The same cache
bridges the 128/24 boundary, and a 15-tap unity-DC half-band FIR decimates the
internal stream 2:1 before the BRICK output. Changing RATE restarts oscillator,
render-cache, waveform-phase and decimator state for the affected voice while
keeping its note, gate and control values. The output envelope rises from zero
after a switch.

## Live waveform pages

Prism TONE is split into `TONE 1/2` LIVE and `TONE 2/2` CLASSIC with the same
four parameter banks. LIVE displays OSC1 or OSC2 full-width on their pages and
both oscillators side by side on COMMON and MOD / PHASE.

The display reuses the shared synth waveform snapshot protocol. One stable,
most-recent polyphonic voice is selected; changing that selection cancels the
partial capture. Samples come from the existing Braids render block, never from
an additional render. Models 0 through 25 use the primary carrier phase already
advanced by Braids (the sub-oscillator phase for the two SUB models); models 26
through 32 use a short fixed time window. Capture work is enabled only
while a Prism LIVE page requests it, and the last completed snapshot remains
visible between captures. A voice or model change cancels the partial session
before a new complete snapshot can be published.
