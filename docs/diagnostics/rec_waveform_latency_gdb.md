# REC / waveform latency: hardware GDB capture

For each S0–S8 scenario, halt the target, reset diagnostics and resume:

```gdb
set variable g_latency_diag_reset_requested = 1
continue
```

After the scenario, halt the target and collect:

```gdb
p g_seq_latency_diag
p g_seq_late_trace_head
p g_seq_late_trace_ring
p g_idle_latency_diag.max_cycles
p g_idle_latency_diag.storage_max_cycles
p g_idle_latency_diag.storage_category_max_cycles
p g_idle_latency_diag.storage_category_last_job
p g_idle_latency_diag.storage_category_last_bytes
p g_waveform_latency_diag
p g_waveform_page_diag_head
p g_waveform_page_diag_ring
p g_idle_latency_diag.core_clock_hz
```

`g_seq_late_trace_ring` contains the last 32 publication-cursor delays of at
least `g_seq_late_trace_threshold_samples` (default 64 samples). Index an
entry with `(g_seq_late_trace_head - 1) % 32` for the newest event. The
`previous_service` field is the service completed immediately before the
sequencer call. `gap_worst_service` and `gap_worst_storage_job` are the
longest measured service and STORAGE subjob since the previous sequencer
call; these give context when the immediate predecessor is a short poll.
`storage_job` is the last completed STORAGE subjob. `sd_owner` and
`sd_background_active` are sampled when the sequencer detects the delay;
they do not reconstruct a released gate's earlier owner. `rec_active` is
the recorder state, while `waveform_active` means a request was seen in the
preceding 100 ms. Requested and displayed levels are captured with the
event. Level 255 means REC overview.

STORAGE categories are 0 RECORDER, 1 STREAM/PAGE CACHE, 2 WAVEFORM MINMAX,
3 OTHER BG/FILESYSTEM. The existing per-service enum in
`idle_latency_diag.h` identifies `storage_category_last_job` and
`storage_max_cycles`. `storage_category_last_bytes` is an actual read count
only where directly available (the local waveform min/max reader); zero for
other jobs means the byte count is unavailable, not necessarily no I/O.
For STREAM polls, the category job value is
`IDLE_LATENCY_STORAGE_COUNT + IDLE_LATENCY_SERVICE_STREAM` (or
`+ IDLE_LATENCY_SERVICE_AUDIO_BG_LOCAL`).

`pcm_no_page`, `pcm_reserved`, `pcm_loading`, `pcm_bad_key_epoch`, and
`pcm_other` classify unsuccessful exact-PCM requests. `pcm_bad_key_epoch`
also includes an incompatible registered format. `page_reservation_to_ready_*`
measures pages newly reserved by a waveform request or its one-page
prefetch. `page_preexisting_to_ready_max_cycles` instead measures from the
first waveform demand for a page that was already RESERVED or LOADING;
its original reservation time is unavailable. The page ring has 32 entries.
`state == 1` is still waiting; `state == 2` reached READY. Its overwrite
counter warns when a READY latency could not be matched. The ideal-level
min/max counters distinguish a tile absent from a tile in construction and
sidecar data pending.
`minmax_gate_not_now` counts SD background admission refusals while a local
tile build was active; `minmax_last_sd_owner` captures the owner observed at
that attempt.

Convert cycles to seconds with `g_idle_latency_diag.core_clock_hz`. The
32-bit cycle counter wraps after roughly 9 seconds at 480 MHz, so a page
waiting longer than one wrap cannot have an unambiguous cycle duration.
`max_publication_late_samples` is measured before the existing CONTROL
cursor rebase; at 48 kHz, 48 samples are 1 ms. This is a missed publication
window, not proof of a lost note. For an individual track/step, the existing
`g_seq_note_trace_ring` records production, invalidation and consumption;
select its track/step before running the scenario.
