# Live input clock bridge

The live-input capture clock is TIM5 on both board variants. TIM5 is a free-running
32-bit counter configured with the existing prescaler and is not reset by MIDI
clock scheduling. Its frequency is read from the active APB1 timer clock and is
converted to audio samples using the fixed 48 kHz audio rate.

The audio owner publishes an anchor at the beginning of every SAI RX half
callback, before `process_half()` advances or renders the half:

```text
anchor_tim5_tick = TIM5->CNT
anchor_audio_sample = first sample of the TX half about to be written
```

This sample is the next CPU-renderable sample in the audio timeline. It is not
the sample currently leaving the codec. Producers convert their captured TIM5
tick against the latest coherent anchor; they do not read the audio timeline
directly.

Anchor publication and reads use a short interrupt-masked critical section so a
reader cannot combine the tick from one anchor with the sample from another.
TIM5 deltas use signed 32-bit modulo arithmetic, which handles the counter wrap
provided the capture-to-anchor interval remains below `2^31` timer ticks.

## Validated encoder detents

The quadrature decoder keeps the existing accumulated delta for UI navigation,
but also publishes one fixed-width `encoder_detent_event_t` for every complete
validated increment. Publication occurs in the encoder fast-poll IRQ, after the
transition count reaches a detent and before the UI consumes any delta. The
event carries only `direction`, `capture_tick`, `ingress_serial` and
`encoder_id` (plus a reserved field); it contains no pointer or allocation.

The validated-detent stream is a 32-entry single-producer/single-consumer ring.
The IRQ is the producer, the future control/audio bridge is the consumer, and
the event is committed with a data barrier before the head cursor is published.
If the ring is full, the newest detent is dropped deterministically and
`encoder_detent_event_overflow_count()` increments. The ingress serial still
advances, making the loss observable without changing the order of accepted
events. Navigation continues to consume the legacy accumulated delta and does
not consume or create a DSP event in this pass.

The event format and ring contract are deliberately pointer-free and fixed
width so the producer can later move to M4/shared RAM without changing the M7
consumer contract. Cache protocol, HSEM and the actual M4/M7 split remain out
of scope here.
