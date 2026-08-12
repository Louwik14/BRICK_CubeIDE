# FM parameter-response audit

Scope: `OP LEVEL`, `FDBK`, and operator `DETUNE`, with `FM_KERNEL_BENCH=1`.

## OP LEVEL

The temporal envelope now runs at a fixed reference outlevel. Output level,
velocity scaling, and key scaling are prepared as a separate Q24 log-gain
offset. Changing `OP LEVEL` updates that offset without calling `Env::update`,
so the active EG stage, accumulated level, remainder, and oscillator phase are
unchanged. The next render block consumes the new offset and ramps the kernel
gain over that block.

A source-level host render (algorithm 32, OP6 alone, held MIDI note 60) measured
RMS 0.000356 at level 20 and RMS 0.239434 in the first block after changing to
level 99: a 672.2x response without note-on or phase reset.

## Feedback

The MSFA Q24 law and the log-kernel Q14 law are equivalent after conversion to
phase units. MSFA uses `(y0 + y1) >> (feedback_shift + 1)` in Q24 phase; the
log kernel uses `(y0 + y1) >> log_feedback_shift`, then converts Q14 samples to
Q32 phase. Therefore `log_feedback_shift = feedback_shift + 1`.

At maximum operator output, levels 1 through 7 span approximately 1/128,
1/64, 1/32, 1/16, 1/8, 1/4, and 1/2 cycle of peak injected phase. Level 0 is
zero. No feedback-strength increase is required.

Feedback operator routing from the DX7/MSFA table is:

- DX7 OP6: algorithms 1, 3-7, 11, 13, 14, 16, 19, 22-26, 29, 31, 32
- DX7 OP5: algorithms 28, 30
- DX7 OP4: algorithm 8
- DX7 OP3: algorithms 10, 18, 20, 21, 27
- DX7 OP2: algorithms 2, 9, 12, 15, 17

The specialized log kernel currently supports algorithm 1 only, whose
feedback operator is DX7 OP6/internal index 0. All other algorithms continue
through MSFA and use the table-selected feedback operator. Compile-time checks
require exactly one feedback operator in every topology and cover each internal
operator index used by the table.

Reference cases:

- A, algorithm 32/OP6 alone: feedback is on an audible carrier; 0 to 7 changes
  a sine-like waveform into a strongly self-phase-modulated waveform.
- B, algorithm 1: OP6 feedback is at the head of a deep modulation chain, so
  downstream levels strongly determine the audible and spectral difference.
- C, algorithm 28: OP5 carries feedback on a shorter modulation path; the
  effect is clearer than in a deeply nested or masked patch but remains
  dependent on carrier/modulator balance.

An 8,192-sample source-level host render at MIDI note 60 produced:

| Case | RMS FDBK 0 | RMS FDBK 7 | RMS waveform difference | harmonic energy 2-16 |
|---|---:|---:|---:|---:|
| A: algo 32, OP6 alone | 0.324700 | 0.327182 | 0.455663 | 2.184e4 |
| B: algo 1, deep OP6 path | 0.339357 | 0.314959 | 0.213515 | 4.931e3 |
| C: algo 28, OP5 path | 0.484560 | 0.478842 | 0.453170 | 1.714e4 |

The similar total RMS in case A does not mean a weak effect: its waveform
difference exceeds either signal RMS because feedback redistributes energy
into harmonics and changes sample polarity/phase. Case B quantifies the
expected masking by a deep modulation chain.

## Detune

Ratio mode uses the Dexed/MSFA law directly. Values below are the Q24 log-law
result before the shared frequency LUT; BRICK and Dexed are identical.

| MIDI note | DET -7 BRICK | DET 0 | DET +7 BRICK | Dexed -7 / +7 |
|---:|---:|---:|---:|---:|
| 36 | -13.9141 cents | 0 | +13.8783 cents | -13.9141 / +13.8783 |
| 60 | -8.3693 cents | 0 | +8.3855 cents | -8.3693 / +8.3855 |
| 84 | -4.7395 cents | 0 | +4.7360 cents | -4.7395 / +4.7360 |

These figures include the shared `Freqlut` quantization and were measured from
the actual phase increments, not only from the pre-LUT formula.

Fixed mode also follows Dexed: negative detune does not lower the fixed
frequency, while positive steps add 13,457 Q24 log units each (about 0.9625
cent per step, 6.7376 cents at +7). The phase-increment conversion uses the
same `Freqlut` result in both renderers; the log kernel only widens Q24 phase
increment to Q32 with `<< 8`, so no detune precision is lost there.

## Builds and cost

Low-Cost and Premium release builds pass with `FM_KERNEL_BENCH=1`. The change
adds one prepared Q24 offset per operator and one addition per operator per
render block. It adds no per-sample work and does not change the log-domain,
AoS, unroll-by-two, ITCM, or voice-versioning paths.
