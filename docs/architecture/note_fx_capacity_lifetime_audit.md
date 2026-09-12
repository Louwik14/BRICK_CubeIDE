# NoteFx capacity and output-lifetime audit

## Verdict and root causes

The pipeline remains appropriate provided that CONTROL is the only musical
lifetime authority and that its horizon reserve includes both of its independent
producers.  The implementation had two contract defects.

Bug A was not an invalid Gate parameter and `capacity = 5` was not a five-voice
limit.  Five was the numeric value of `AUDIO_COMMAND_APPLY_MAPPING`, copied into
the generic fatal field.  Gate RETRIG marks an ON as a retrigger.  When its
generated `0xC...` occurrence was not already alive, CONTROL nevertheless
published RETRIGGER, which expands to OFF then ON.  A synth voice allocator
rejected that invented OFF because the id had never been mapped.  CONTROL now
normalizes a first RETRIGGER to START, preserves RETRIGGER only for an existing
lifetime, and AUDIO treats every unknown STOP as the explicitly idempotent
operation required by the wire contract.  Mapping fatals now report the actual
eight-entry physical mapping capacity.

Bug B was a split capacity proof.  The 256-action staging array covered the
sequencer/source producer only.  `note_fx_engine_process()` is a second producer:
ARP and EUCLID can autonomously materialize terminal pairs from held state in the
same horizon.  Static chain admission was compared against 256, but runtime put
both producer classes into that same 256-entry array.  Thus an admitted horizon
could fill it exactly and fail on action 257.  The single product bound is now
256 source-driven actions plus 128 temporal NoteFx actions = 384.  Source fanout
admission deliberately still compares against its own 256 reserve, so enlarging
the shared scratch cannot accidentally admit a wider chain.

## Real pipeline and ownership

`seq_play_scheduler` and live KEY/MIDI ingress allocate source occurrence ids in
their reserved namespaces.  `note_fx_pipeline` owns persistent source HELD state,
the four-stage chain, delayed events and chain/source generations.  The engine
transforms grouped events; Echo and Gate materialize dated future events, while
ARP/Euclid retain compact held/phase/deadline state and generate only the current
horizon.  The terminal submits `{entity, occurrence_id, cause, generation}` to
`control_music_output`.  That CONTROL ledger owns admission, same-pitch/global
Multi stealing and the active lifetime.  It publishes an ordered STOP/START
stream through the CONTROL-to-AUDIO FIFO.  AUDIO owns only the eight-entry
execution mapping, renderer voice binding and physical release tails.

An output id denotes one logical sounding lifetime.  Echo and Harmonizer derive
stable child ids from the parent occurrence and stage/voice/repeat coordinates;
ARP and Euclid allocate FX-namespace tokens.  A retrigger keeps the id and means
STOP+START at one sample.  Revoice closes old CONTROL outputs and starts the new
set.  STOP is idempotent after stealing, panic, type change or stale-future
cleanup.  START of an already-live id is normalized to retrigger by CONTROL;
RETRIGGER of a non-live id is normalized to START.

## Capacities

| Storage/limit | Owner | Capacity and unit | Lifetime/full behavior |
|---|---|---:|---|
| source HELD | pipeline | 8 notes/track | source ON to matching OFF/reset; admission lowers usable count |
| slot HELD | engine | 8 notes/slot/track | stage ON to OFF/reset; ARP/Euclid compact state |
| A/B batch | pipeline | 32 events | one grouped stage pass; chain rejected if composed stage fanout exceeds it |
| future | pipeline | 512 dated events global | until due, causal purge, type reset or panic; aggregate config reservation must fit |
| command ring | ingress/pipeline | 31 usable commands | until CONTROL consumption; producer receives rejection when full |
| live queue | pipeline | 31 events | capture tick to due CONTROL window; stale policy is explicit |
| source action reserve | CONTROL | 256 actions/horizon | 64 emitting voices x two adjacent generations x transition pair |
| temporal FX reserve | CONTROL | 128 actions/horizon | 64 admitted terminal outputs x STOP/START pair |
| internal staging | CONTROL | 384 actions/horizon | exact sum of the two independent producer reserves |
| external staging | CONTROL | 128 actions/horizon | separate ingress overload domain |
| logical outputs | CONTROL | 8/entity | product polyphony; deterministic oldest/same-pitch victim selection |
| physical mapping | AUDIO | 8/entity | mirrors legal CONTROL lifetimes; no AUDIO musical admission |
| FIFO NOTE burst | IPC | 1024 commands | worst case two commands per 384 internal + 128 external actions |

The future capacity is not a horizon product limit.  It is persistent storage
whose 512 slots are reserved transactionally across track configurations.  The
32-entry A/B arrays are scratch, not musical polyphony.  The 384 horizon count is
a product bound, not a scratch guess; it is derived from the admitted source and
temporal producers and remains below the existing 4096-command FIFO proof
(`required = 3548`).

## FX audit

OFF, Probability and Groove have 1x fanout and no persistent future event.
Chord revoices/deduplicates a group without increasing its count.  Harmonizer
has up to four immediate voices.  Echo has one original plus at most two repeats
and reserves both ON and OFF repeats.  Gate passes one ON and owns one delayed
OFF per admitted source; repeated Gate stages replace, rather than multiply,
the prior OFF.  ARP retains up to eight held pitches but emits one selected pair
per deadline.  Euclid emits one pair per held pitch on a pulse.  At the shortest
division and maximum supported tempo a 64-frame horizon contains at most one
temporal deadline; downstream admission limits the resulting terminal fanout to
64 outputs globally.  More than one Echo, more than one Harmonizer, composed
fanout above four, a 32-event intermediate overflow, and GROUP-child fanout are
rejected before activation.

Parameter tweaks preserve chain generation.  Chord/Harmonizer revoice closes
causal outputs and replays HELD at the cutover sample.  ARP/Euclid keep phase and
deadline for non-type parameter changes.  Type changes close affected outputs,
purge obsolete futures, reset downstream state, increment chain generation and
replay from HELD.  Echo events already materialized survive ordinary tweaks.
Transport recovery drops expired delayed ONs; OFFs remain idempotent.  Panic and
track reset clear commands, futures, HELD state and generations after closing
CONTROL ownership.

## Verified failure-family paths and invariants

The audit covered pipeline rejection, window preflight/commit, FIFO horizon
commit, scheduler apply, future/held/live/command saturation and AUDIO NOTE
mapping.  The corrected invariants are:

- no admitted chain exceeds 32 intermediate events, fanout four or its reserved
  share of 512 futures;
- source and temporal work have separate bounds and one summed staging bound;
- the CONTROL ledger has at most eight unique active ids per entity and selects
  every musical victim once;
- first retrigger is START, live retrigger is OFF then ON with the same id;
- unknown/duplicate STOP is idempotent at both CONTROL and AUDIO;
- AUDIO does not steal or perform musical admission;
- STOP buckets precede START, which precedes RETRIGGER, at the same sample;
- stale generations cannot release a newer lifetime and expired delayed ONs are
  not replayed after an xrun;
- future and staging overflow remain invariant failures, but their deterministic
  product cases are rejected at configuration or covered by the common proof.

The host regression enumerates all 9^4 model chains, checks admitted fanout,
batch and future bounds, covers Gate/Echo, Arp/Gate, Euclid/Gate and Chord/Echo,
checks the explicit preflight rejection of Harmonizer/Echo and other
multiplicative Echo/Harmonizer chains, and pins the
first-retrigger/unknown-STOP semantics and shared capacity constants.  Hardware
validation should still repeat rapid retrigger, type/revoice changes, panic,
transport/xrun recovery and near-full future/FIFO scenarios because renderer and
dual-core timing cannot be simulated by the host contract test.

If NoteFx were rebuilt today, this architecture would be kept but simplified in
exactly this way: compact temporal state, one persistent future queue, one
CONTROL lifetime ledger, one derived capacity contract, and no AUDIO admission.
The fanout/future/staging/lifetime family is architecturally closed for the
declared product bounds; remaining risk is hardware timing validation rather
than an unowned capacity or identity decision.
