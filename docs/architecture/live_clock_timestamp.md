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
