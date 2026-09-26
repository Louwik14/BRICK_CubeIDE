# USB Audio GOOD/BAD RAM diagnostic

This temporary diagnostic observes the PC-to-BRICK path without changing its
ring size, thresholds, feedback law, conversion, scheduling, or IRQ policy.
The snapshot and two 1024-entry traces live in the existing non-cacheable,
shareable 256 KiB recorder SDRAM arena. They occupy 32,992 bytes, do not use
DTCM or either audio DMA buffer, and remain directly readable after halt.

The USB trace has one writer (USB IRQ/superloop) and records every OUT packet,
ring reset, and interface change. The AUDIO trace has one writer (SAI RX IRQ)
and records every accepted block and every PC-to-BRICK ring read. Per-service
and per-feedback tracing is deliberately reduced to counters/min/max/current
values so the trace retains roughly the last second and instrumentation does
not dominate the superloop.

## Capture

Run the following single GDB block. `continue` waits; press Ctrl+C only after
hearing the desired GOOD or BAD state. Repeat from the first line for the other
state and rename the two `.bin` files between captures.

```gdb
shell cls
call usb_audio_diag_reset()
continue
p g_usb_audio_diag
p g_usb_audio_diag.usb_rx_packets
p g_usb_audio_diag.usb_rx_gap_max
p g_usb_audio_diag.ring_fill_current
p g_usb_audio_diag.ring_fill_min
p g_usb_audio_diag.ring_fill_max
p g_usb_audio_diag.usb_rx_overflow_count
p g_usb_audio_diag.audio_underflow_count
p g_usb_audio_diag.audio_zero_blocks
p g_usb_audio_diag.feedback_min
p g_usb_audio_diag.feedback_max
p g_usb_audio_diag.feedback_current
p g_usb_audio_diag.usb_service_gap_max
p g_usb_audio_diag.audio_block_gap_max
p g_usb_audio_diag.ring_reset_count
p g_usb_audio_diag.usb_trace_write
p g_usb_audio_diag.audio_trace_write
dump binary memory usb_audio_diag_usb.bin &g_usb_audio_diag_usb_trace[0] &g_usb_audio_diag_usb_trace[1024]
dump binary memory usb_audio_diag_audio.bin &g_usb_audio_diag_audio_trace[0] &g_usb_audio_diag_audio_trace[1024]
```

Decode offline with the two printed write indices, for example:

`python tools/decode_usb_audio_diag.py --usb-write 12345 --audio-write 9876`

## GOOD/BAD comparison

- `usb_rx_packets`, byte-size bins, `usb_rx_gap_max`, unaligned/short reads:
  distinguish host/TinyUSB delivery faults from downstream faults.
- `usb_rx_frames` versus `usb_rx_frames_written`, refused frames,
  `usb_rx_overflow_count`: identify loss at the TinyUSB-to-float-ring boundary.
- ring raw cursors plus fill current/min/max, per-event fills, below-start and
  reset counts: reveal threshold rearming and fill-phase oscillation.
- requested/read frames, underflows, ready drops and zero blocks: show whether
  a 64-frame AUDIO block was replaced entirely by silence.
- feedback fill/error/current/min/max/update count: correlate feedback direction
  and range with ring evolution and packet sizes.
- USB service count/gap: identify superloop starvation independently of packet
  arrival. All gaps are raw TIM5 media-clock ticks.
- AUDIO callbacks, rejected callbacks, half mismatches, recovering blocks and
  block gap: identify stale/coalesced callbacks or a discontinuous AUDIO
  timeline independently of USB transport.

Interpret only values captured before Ctrl+C. Do not resume the same trial
after inspecting it; reset diagnostics and start a fresh GOOD or BAD capture.
