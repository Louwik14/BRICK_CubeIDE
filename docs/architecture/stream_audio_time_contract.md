# Streamer audio-time contract

## Clock ownership

The streamer's scheduling time is a monotonic 64-bit count of rendered audio
frames. The audio IRQ is its only writer. Main-context readers use a sequence
counter, so a 64-bit snapshot cannot tear on Cortex-M7. The IRQ performs no
scheduling, file access or decoding work.

This clock is deliberately independent from transport state: stopping or
rebasing the sequencer cannot move streamer time backwards. It is the M7-owned
timebase that a future M7 to M4 request queue will carry.

## Deadline contract

A page request records:

- `created_audio_frame`;
- `consume_deadline_audio_frame`;
- DWT timestamps used only for profiling.

Source distance is converted once to output frames with the Q16 playback step,
then added to the current audio frame. An existing request may only tighten its
deadline when another owner of the same physical page needs it earlier. A
refresh can never postpone it. Selection compares stored absolute deadlines;
relative frame distances no longer age implicitly in the pending table.

## Inter-core ABI seed

`sample_stream_request_contract_t` is a pointer-free, fixed-width, 40-byte POD.
Its layout is protected by static assertions. It is not yet the active request
queue; it establishes the message contract for the later producer/scheduler
separation and eventual shared-memory M7 to M4 transport.

## Trace timestamps

The Release trace now carries absolute audio-frame timestamps for creation,
deadline, selection, transition to in-flight work and publication as ready.
Cycle timestamps remain available for physical latency measurements. Deadline
lateness is decided from audio frames rather than from a mutable projected DWT
deadline.
