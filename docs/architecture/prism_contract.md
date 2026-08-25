# Prism contract

Prism exposes two pitch-modulation amounts, `P.MOD1` and `P.MOD2`, on the
`MOD / PHASE` page. `P.MOD` is the depth of pitch modulation by `AMOD`, from
zero to a maximum positive offset of 24 semitones:

`pitch offset = P.MOD * AMOD * 24 st`

This control is not audio-rate FM and does not cross-modulate the two Prism
oscillators. `PH1` and `PH2` remain independent trigger phase-reset switches.

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
