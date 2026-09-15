# REC / waveform latency: GDB snapshot

This instrumentation is an initial subset of the hardware audit. It does not
prove a lost note. `g_seq_latency_diag.max_publication_late_samples` measures
how far the CONTROL publication cursor was behind the current audio sample
before the existing cursor rebase. At 48 kHz, 48 samples are 1 ms. The
`late_over_*` counters count passes above 64, 256 and 1024 samples.

For each S0–S8 scenario, stop the target in GDB and use:

```gdb
call memset((void *)&g_seq_latency_diag, 0, sizeof(g_seq_latency_diag))
call memset((void *)&g_waveform_latency_diag, 0, sizeof(g_waveform_latency_diag))
call memset((void *)&g_idle_latency_diag.call_count, 0, sizeof(g_idle_latency_diag.call_count))
call memset((void *)&g_idle_latency_diag.max_cycles, 0, sizeof(g_idle_latency_diag.max_cycles))
call memset((void *)&g_idle_latency_diag.storage_call_count, 0, sizeof(g_idle_latency_diag.storage_call_count))
call memset((void *)&g_idle_latency_diag.storage_max_cycles, 0, sizeof(g_idle_latency_diag.storage_max_cycles))
continue
```

Run the selected scenario, interrupt the target, then collect:

```gdb
p g_seq_latency_diag
p g_waveform_latency_diag
p g_idle_latency_diag.max_cycles
p g_idle_latency_diag.storage_max_cycles
p g_idle_latency_diag.core_clock_hz
p g_seq_note_trace_head
p g_seq_note_trace_ring
```

`max_service_gap_cycles` and the superloop service maxima convert to seconds
by dividing by `g_idle_latency_diag.core_clock_hz`. The waveform counters
distinguish exact PCM, ideal local or sidecar min/max, lower-resolution
fallback, and REC overview. `last_display_level == 255` means overview.
`pcm_not_ready` includes an unavailable stream and a missing/LOADING page;
this subset does not yet distinguish them. `ideal_*_missing` means the
corresponding ideal range could not be rendered at request time.

The pre-existing `g_seq_note_trace_ring` follows a selected track/step through
production and consumption. Set `g_seq_note_trace_track` and
`g_seq_note_trace_step` before a run. Its skip markers are the current source
for genuinely invalidated events. Storage maxima come from the existing
`idle_latency_diag` service timers. The `previous_service` snapshot records
the last *slow* superloop service, so it is a clue rather than a causal trace.

This subset does not provide per-job SD bytes, admission wait, reservation to
READY age, page evictions before use, or an event ring correlating the exact
preceding service with each late sequencer pass. Hardware measurements should
therefore be treated as exploratory until those metrics are available.
