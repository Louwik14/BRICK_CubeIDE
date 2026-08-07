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

The quadrature decoder publishes one fixed-width `encoder_detent_event_t` for
every complete validated increment. Publication occurs in the encoder fast-poll
IRQ, after the transition count reaches a detent and before the UI consumes any
delta. The event carries `direction`, `capture_tick`, `ingress_serial`,
`encoder_id` and a pointer-free binding snapshot. Each snapshot entry packs the
parameter/action, scope, resolved track, slot, SHIFT and track-modifier states,
route and validity.

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
consumer contract. The UI publishes the four-entry binding through a bounded
double buffer; TIM7 reads one complete buffer and never locks or retries. Cache
protocol, HSEM and the actual M4/M7 split remain out of scope here.

## Encoder detent to parameter command

`encoder_control_dispatcher_service()` consumes the validated detent ring once
at the control-context boundary, before the out-of-queue shift/track input
mirror is updated. Button/page events generated during the tick are dispatched
afterwards. The dispatcher uses the binding embedded in each
detent; it never rereads the current page, track or SHIFT state. A later UI
change therefore cannot rebind an older detent to another target.

All four encoder indices address the four entries of the active parameter bank;
the historical `ENC_PAGE` enum name does not make index zero navigation-only.
An encoder whose bank entry is an audio/runtime parameter produces one
`live_parameter_event_t` per accepted
detent. The event stores the captured TIM5 tick and ingress serial, the
resolved parameter/scope/track, and a canonical final `SET_TARGET` value; the
value is stored as float bits in the fixed `int32_t` field. Structural and
sequence controls continue through the legacy UI path and do not enter this
DSP command stream.

The dispatcher uses a bounded 32-detent drain, matching the capture ring
capacity, and keeps a static bounded per-target value shadow so a burst emits successive
canonical targets instead of repeatedly resolving from the pre-burst UI value.
Keeping this 32-entry scratch outside the call stack is required: the expanded
binding-aware entry otherwise raises the dispatcher frame from 128 to 632 bytes
and can overwrite UI state on the constrained control stack.
The UI shadow advances only after the command ring accepts the event; a rejected
command leaves it unchanged. Audio-routed detents are excluded from the legacy
delta accumulator, while navigation, structural and multi-track modifier
bindings remain there. The command ring is fixed at 64 entries and drops newest
commands on saturation with an observable counter.

Its head and tail are monotonic 16-bit cursors used for depth accounting. Every
array access masks the cursor with `capacity - 1`; advancing a cursor without
masking the physical slot writes beyond the 64-event/1280-byte array from the
65th lifetime command onward. The long-run wrap test submits and consumes
100,000 commands (including 16-bit cursor wrap) and also exercises
full/drop/reuse behavior.

## Audio-owned parameter schedule

The control-side `live_parameter_event_t` ring is drained into a separate,
fixed-width `live_parameter_audio_event_t` schedule. The handoff is the only
place that calls `live_clock_tim5_to_guarded_sample_time()`: the resulting
`effective_sample_time` is retained with the original capture tick and ingress
serial, so audio performs no second TIM5 conversion.

The schedule is independent from the NoteFx live-note queue. Audio asks both
queues only for their next deadline and shortens the current render segment to
the earliest one. At the segment boundary notes are processed first; due
parameter events then apply their new target before the following render span.
Encoder traffic therefore has no access to note capacity, note-off, or panic
budgets.

Every detent is retained while capacity is available. If the parameter schedule
is genuinely saturated, only successive unapplied events for the same target
may coalesce to the newest target; unrelated events are reported as drops.
Late and stale conversions, schedule saturation, coalescing, and due-FIFO
saturation are exposed through `live_parameter_audio_queue_get_diag()`.

## Sample-accurate target application

At each segment start the audio owner drains only parameter events whose
`effective_sample_time` is due. The event target is applied before rendering
that sample, so an event in the middle of a DMA half changes the render at the
corresponding sample offset rather than at the next UI or audio block.

`live_parameter_audio_runtime` keeps the target and application diagnostics per
target. It does not fabricate a generic current/increment/remaining-samples
ramp: after the exact event boundary, the DSP owner receives the target and
owns any parameter-specific transition. This prevents a central ramp from
being mistaken for audio smoothing when the backend already owns the signal.

## Encoder parameter migration matrix

The encoder dispatcher currently sends only this first continuous migration
set to the audio authority:

| Group | Parameters | Runtime contract |
| --- | --- | --- |
| VCA envelope | `PARAM_VCA_ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE`, `ENV_TYPE` | Active track voices and new voices see the target; the existing VCA envelope keeps its current phase/level/gate, and a live type change retargets the active trajectory without restarting the voice. |
| Filter | `PARAM_FILTER_CUTOFF`, `PARAM_FILTER_RESONANCE` | Active and new voices use the track target; mixer filter smoothing continues from its current value. |
| Mix | `PARAM_MIX_LEVEL`, `PAN`, `SEND1`, `SEND2` | Active and new track audio uses the target; mixer-owned current/target smoothing starts from the value actually reached. |
| Wave position | `PARAM_WAVE_OSC1_POS`, `PARAM_WAVE_OSC2_POS` | Active wave runtime and future voices share the target; position smoothing remains owned by the wave backend. |
| Prism/Stack | `PARAM_PRISM_FINE`, `COARSE`, `FM`, `TIMBRE`, `MODULATION`, `COLOR`, `LEVEL` and the matching `PARAM_PRISM_OSC2_*`; `PARAM_STACK_OSC1/2/3_LEVEL`, `NOISE_LEVEL`, `OSC1/2/3_TUNE`, `OSC1/2/3_TIMBRE`, `OSC1/2/3_COLOR`, `OSC_DETUNE` | Active runtime and new voices share the target; model, phase-reset and other structural selectors stay outside this file. |
| Delay/reverb | `PARAM_MIX_REVERB_WET`, `ROOM_SIZE`, `DAMPING`, `WIDTH`, `HPF`, `LPF`; `PARAM_MIX_DELAY_WIDTH`, `FEEDBACK`, `SPECTRAL_POSITION`, `SPECTRAL_WIDTH`, `FBW`, `MOD`, `MOD_RATE`, `REV`, `VOL` | The global effect target changes at the effective sample; existing effect smoothing remains active. |

Selectors for engine/model, filter type, routing, effect type/mode/time,
phase reset and Wave position policy are deliberately not migrated here.
ADC, MIDI and other non-listed parameters retain their existing command path.

## Single authority after pass 6

The migrated set is declared once by `live_parameter_is_audio_owned()` and is
shared by the encoder dispatcher, UI value surface and registry query surface.
For these parameters, the UI encoder path updates only a cold UI shadow and
submits the timestamped command. It no longer calls the DSP backend, marks the
audio parameter dirty, or reads a private voice value. Track queries resolve
from the audio-authoritative runtime target cache; global queries resolve from
the UI logical shadow until the audio command is applied.

The audio runtime is the only live writer for migrated DSP targets. Its target
application commits the track cache after the backend write, while structural,
selector, sequence, ADC and MIDI parameters retain their existing UI/model
command paths.

## Grouped control transactions (pass 7B)

Clipboard clear and paste operations collect their audio-owned values in one
bounded `live_parameter_audio_bulk_t` payload. The payload contains only fixed
width parameter IDs, scope/track/slot, flags and float bits; it contains no UI
pointer, callback or private address. TIM5 is captured once when the operation
is validated, and the common guarded conversion is performed once by the
audio-queue authority. All members receive one effective sample time and a
deterministic in-transaction order.

The schedule reserves the complete group before publishing any member. If the
capacity is insufficient, the complete transaction is rejected and
`bulk_reject_count` is incremented; no member shadow is advanced. Notes,
note-off and panic remain on their separate priority queue. Due transfer also
reserves the complete group, so the audio segment boundary cannot expose a
half-paste. Structural and continuous parameters are collected separately:
structural values keep their existing transition contract, while migrated
continuous values are applied by the audio owner at the shared boundary.

## Active VCA envelope retargeting (pass 7C)

The synth VCA owns the active voice level and phase. Live Attack, Decay and
Release changes rebuild only the current stage trajectory from the level
already reached. A Sustain change during DECAY rebuilds the target and
increment immediately; a Sustain change after the decay boundary uses a
bounded transition to the new target, with no note restart or gate change.

Both DAISY and LINEAR active trajectories follow the same retargeting contract.
Changing `PARAM_VCA_ENV_TYPE` updates the track envelope and every allocated
poly voice, preserving level, stage and gate. New voices inherit the track
configuration at note-on. The sampler/paraphonic peak envelope applies the
same live Attack/Decay/Sustain/Release retargeting; it has no DAISY/LINEAR
type selector and therefore has no type migration contract.

## Audio-owned smoothing contract (pass 7D)

The audio runtime is an event scheduler and target dispatcher, not a second DSP
engine. The migrated parameters are classified by their real owner:

| Parameters | Contract | Scope |
| --- | --- | --- |
| Track level, pan, sends; filter cutoff/resonance | A — mixer current/target ramps from the reached value | Track mix/filter, active and new audio |
| Wave OSC1/OSC2 position | A when position smoothing is enabled; B immediate when the selected quality policy disables it | Wave voice/runtime |
| Prism fine/coarse/FM; Prism timbre/modulation/color/level | A — pitch and level use existing ramps; timbre/color move at Braids render-subblock cadence | Active/new oscillator voices |
| Stack levels/noise, tune/detune; timbre/color | A — exact per-frame level/pitch/timbre/color ramps | Active/new oscillator voices |
| VCA Attack/Decay/Sustain/Release/EnvType | A/D — active envelope retargeting preserves current level, phase and gate; type is a trajectory contract, not a generic ramp | Active voices and new voices |
| Reverb wet/room/damping/width/HPF/LPF | A — effect wrapper advances target over its bounded smoothing span | Global track effect |
| Delay width/feedback/volume and REV send | A — delay engine sample smoothing | Global delay effect |
| Delay FBW and spectral position/width; Wave model/phase/reset selectors | B/D — routing/topology or derived filter-window changes are applied at their engine boundary; they are not naively interpolated | Global/voice structural state |

For category A, a new target starts from the value actually reached by the
owning DSP state. Category B is an intentional immediate or boundary update;
category D is latched or structural. There is no generic duplicate lissage in
the timestamp runtime. The fixed-width target command remains pointer-free and
preserves the future M4 producer/M7 DSP-owner contract.
