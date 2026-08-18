# COMP insert: Airwindows Pop2

The per-entity Audio FX `COMP` is a local float port of Airwindows Pop2. It is
the fixed pre-fader COMP stage; the separate master Deluge compressor is not
part of this path.

At 48 kHz the macros map as follows (normalized controls are `0..1`):

- `AMOUNT`: `A = amount`; zero is a BRICK hard bypass. Pop2 `D` stays at its
  original default `0.5`, so makeup is `sqrt(1 / threshold) * 0.5`.
- `PUNCH`: `B = punch`, `C = punch`. Thus both native nonlinear time laws are
  retained; the midpoint is exactly Pop2's default `B = C = 0.5`.
- `PAR`: Pop2 is evaluated as a 100% wet branch and BRICK applies the single
  additive parallel sum `output = dry + wet_compressed * (par / 127)`. The
  dry coefficient is always `1.0`; native `E` is therefore fixed conceptually
  at full wet rather than used as a crossfade.

The native threshold/pre-gain, alternating mu coefficients and speeds,
attack/release/max-release, makeup, independent L/R state, and post-compressor
ClipOnly2 stage are retained. At 48 kHz `floor(48000 / 44100) = 1`, so each
ClipOnly2 channel needs only its last sample plus a two-float intermediate
array. The VST wrapper, heap use, double path, denormal RNG, and dither are not
ported.

The additive sum is intentionally not normalized or limited. Pop2 ClipOnly2
limits its wet branch near full scale, while the untouched dry branch can also
approach full scale; at `PAR=127` the seam can therefore approach roughly
`±1.955` for same-polarity peaks. BRICK's float path has ample numeric
headroom, but downstream gain staging can legitimately clip this larger sum.
