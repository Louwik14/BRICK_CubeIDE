# DX7 SysEx import core

The import core is split into two independent layers:

- `dx7_sysex` validates Yamaha framing, device nibble, format, encoded length,
  7-bit payload, checksum, reserved bits and every DX7 parameter range. It
  accepts concatenated single-voice (`155` data bytes) and 32-voice bank
  (`4096` packed data bytes) messages and emits logical `dx7_voice_t` values.
  Operators are reordered from dump order OP6..OP1 to BRICK order OP1..OP6.
- `dx7_import` converts each logical voice into a normal
  `persist_control_patch_t`. No DX7 state reaches the audio runtime or the
  persistent codec.

## Mapping and fidelity

Operator envelopes, keyboard level scaling, rate scaling, velocity
sensitivity, output level, ratio/fixed mode, coarse/fine, detune, algorithm,
feedback, oscillator key sync, pitch envelope, transpose and name are copied
in their native DX-compatible units. These mappings are exact. Hidden FM base
fields remain part of the normal v14 patch body and therefore survive normal
save/load.

DX7 LFO speed uses the firmware-derived non-linear rate conversion. Waveforms
map as follows, with phase offsets chosen to preserve initial polarity:

| DX7 | BRICK |
| --- | --- |
| triangle | triangle, 180 degrees |
| saw down | reverse saw |
| saw up | saw |
| square | square, 180 degrees |
| sine | sine, 180 degrees |
| sample & hold | random S&H |

Key Sync uses per-voice `POLY_TRIG`; disabled Key Sync uses `FREE`. S&H has the
same cadence and polarity class but not the Yamaha random sequence.

PMD times the non-linear PMS table is converted to a native LFO route targeting
`FM Transpose`. AMD times each operator's non-linear AMS coefficient is
converted only when AMS and AMD are non-zero, targeting that operator's native
Level parameter. Pitch and amplitude routes share LFO1. The attenuation-only
DX7 amplitude range is centered around the bipolar BRICK LFO by applying the
corresponding static level offset. Extreme low operator levels can clamp, so
amplitude modulation is quasi-exact rather than bit-exact.

When delay is non-zero, all generated routes use `MULTI1 = LFO1 * ENV3`.
ENV3's attack time is derived from the DX7 delay/fade increments. This preserves
a correlated fade-in but approximates the DX7 two-stage hold/fade contour with
a single attack curve and consumes ENV3 plus MULTI1. The maximum generated
matrix use remains seven routes: one pitch route and six amplitude routes.

Fields without a DX7 equivalent use neutral BRICK defaults: open filter,
neutral VCA, disabled FX, one voice, neutral macros, unused LFOs and disabled
unused routes.

Fixed-frequency detune now applies symmetrically for values -7..+7 in the FM
engine. Ratio mode, zero detune and positive fixed detune retain their existing
behavior.

## Persistence and scope

Imported voices are ordinary FM BRICK patches encoded by codec v14. There is
no DX7 mode, DX7 runtime, separate patch format or new persistent field.

Storage discovery and materialization are deliberately deferred: scanning
`/Patches`, detecting `.syx`, transactionally writing and validating `.B6C`,
resolving filename collisions, and deleting the source after success belong to
the storage integration pass.
