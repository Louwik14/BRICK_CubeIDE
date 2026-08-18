# LOFI runtime engine bench

LOFI is fixed at the canonical POST FILTER seam. `P1` is BIT, `P2` is SRR,
and `P3` selects `HYBRID`, `FLOAT`, `DAISY`, `DEREZ`, or `DEREZ3`. HYBRID chains
the native DeRez3 BIT quantizer into the Deluge FLOAT SRR stage. All five kernels are in
the same firmware image; `P3` dispatches exactly one kernel and clears the
newly selected kernel's temporal state.

## Hardware comparison

Use the same firmware, source, note/sample, track, gain and filter settings.
For each test point, turn only P3 through all five engines and record IRQ load,
peak level, RMS level and listening notes:

| Test | BIT / SRR |
|---|---:|
| transparent | 0 / 0 |
| BIT only | 127 / 0 |
| SRR only | 0 / 127 |
| combined | 64 / 64 |
| maximum | 127 / 127 |
| BIT maximum, SRR mid | 127 / 64 |
| BIT mid, SRR maximum | 64 / 127 |

For every row, measure the five P3 positions in order: HYBRID, FLOAT, DAISY,
DEREZ, DEREZ3. The test is invalid if the firmware is rebuilt or reflashed
between engines.

P3 slot 0 formerly selected the experimental Q31 engine. It now selects HYBRID,
so persisted slot-0 projects automatically use HYBRID without a data migration.
