# Hall live event contract

## Authority split

Low-Cost and Premium execute the same bounded Hall state machine from the ADC
acquisition callback. The threshold crossing that produces note-on, note-off,
or retrigger publishes one compact event. `hall_loop_process()` no longer
decides a musical edge; it remains a non-real-time maintenance hook.

The producer owns only acquisition data:

```c
typedef struct {
    uint32_t tim5_tick;
    uint32_t ingress_serial;
    uint32_t occurrence_id;
    uint8_t key;
    uint8_t pressed;
    uint8_t velocity;
    uint8_t source;
} live_event_t;
```

The structure is fixed at 16 bytes, contains no pointer, and is copied as a
whole. `occurrence_id` is reserved for a producer that has one; Hall currently
uses `ingress_serial` for stable ordering and lets the keyboard/NoteFx owner
allocate the authoritative note occurrence.

## Queue and timing

The current H743 implementation uses a bounded 64-slot static M7 FIFO
(63 usable entries with the ring sentinel). Submission is a short critical
section with deterministic rejection and a drop counter. The Premium raw ADC
FIFO is diagnostic-only and cannot delay Hall detection.

The event producer captures only TIM5. It never reads the audio timeline, the
voice state, the mixer, or M7-private memory. The keyboard bridge carries the
capture tick through the existing input/occurrence path. The audio owner then
uses `live_clock_tim5_to_sample_time()` to convert the tick against the
published SAI anchor. Exact block segmentation and any fixed live guard remain
audio-pipeline responsibilities of later passes.

## H747 compatibility

`live_event_submit_from_hall()` is the producer/consumer seam. On H743 its
implementation is a local bounded FIFO. On H747 it can be replaced by an
M4/M7 shared-RAM or IPC implementation without changing the Hall detector,
the 16-byte event format, or the M7 timestamp conversion and audio pipeline.
No HSEM, cache, or dual-core-specific code is part of this pass.
