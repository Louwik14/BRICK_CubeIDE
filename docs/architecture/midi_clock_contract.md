# MIDI Clock output contract

BRICK uses the sequencer audio-sample timeline as the sole authority for MIDI
Clock output. The sequencer step period is expressed in Q16 samples; dividing
that period by six yields 24 clocks per quarter note because one sequencer step
is a sixteenth note. No independent timer computes a second tempo.

On an internal-clock transport start, BRICK queues `FA` first, enables clock
production, and anchors the first `F8` one clock period after the current sample
position. Each audio boundary advances the Q16 deadline and queues every due
`F8`. USB I/O is not performed in the audio interrupt: the MIDI queue is drained
by the cooperative TinyUSB service. At 48 kHz with 64-frame audio blocks, the
scheduling quantization is bounded to less than 1.34 ms; USB framing can add its
normal host-side frame latency. The long-term cadence retains the Q16 fractional
sample remainder.

`FC` disables production before any later audio boundary can queue another
clock. Internal resume emits `FB` and re-anchors the clock deadline. Normal PLAY
starts from the beginning and emits `FA`; Song Position Pointer is available in
the MIDI API but is not part of the current product transport workflow. External
MIDI clock mode consumes `F8`/`FA`/`FB`/`FC` and never echoes clock pulses.

USB MIDI System Realtime messages use cable 0 and CIN `0xF`. Clock and transport
share the same bounded 128-packet transmit queue and retry path as channel voice
traffic. Queue saturation is observable through `midi_usb_tx_drop_count()` rather
than being silent. At 120 BPM, clock load is 48 four-byte USB-MIDI event packets
per second.

Run `tools/test_midi_clock_contract.ps1` for deterministic 120 BPM quarter/bar
counts, alternate-tempo spacing, transport ordering, USB packetization, and the
MIDI note-output regression contract.
