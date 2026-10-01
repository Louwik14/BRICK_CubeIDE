# Pattern Recall / PreparedAudio exhaustive diagnostics

`BRICK_PATTERN_RECALL_DIAG` is a CMake option and the only switch for this
temporary instrumentation.  It defaults to `ON` for the diagnostic image.
Every object, helper, call, phase trace and sweep is enclosed by
`#if BRICK_PATTERN_RECALL_DIAG`; an `OFF` image contains no diagnostic symbol,
storage, branch, loop or call.

## Commit call graph and non-mutating seams

The Pattern path is:

`persistent_pattern_control_prepare()` -> CONTROL/PreparedAudio/PreparedSeq
prepare -> `prepared_audio_control_begin_install()` ->
`persistent_pattern_control_install_internal()` ->
`persistent_pattern_finalize_audio_from_control()` -> static diagnostic sweep
-> `seq_engine_control_commit_prepared()` ->
`prepared_audio_control_publish()` -> dated FIFO `AUDIO_STATE_COMMIT` ->
`audio_command_apply_prepared_state_commit()` -> runtime diagnostic preflight
-> trim/close/OFF/install/apply/rebind -> publication fence.

The complete PreparedAudio slot can be checked without mutation immediately
after finalization.  The real AUDIO contexts, held-output ledger, allocator,
resource registries and endpoint eligibility can be checked without mutation
at IRQ entry, after deriving `changed_program_mask` and before PANIC, trim,
close, OFF, install, parameter application or rebind.

## RAM layout and coverage

`g_pattern_recall_diag` is a fixed 3,216-byte block in uncached shared SRAM2.
It contains the transaction identities, effective sample time, current and
maximum phase, both failure counters, 128 records of 24 bytes, and a six-byte
polyphony preflight snapshot for each of the 16 logical entities.  Overflow is
counted without overwriting earlier evidence.

The block is NOLOAD and can survive a debugger flash/reset. Version 2 carries
contract tag `0x812F`; AUDIO verifies magic/version/size/tag and starts each
runtime transaction with an empty runtime record set. Thus records from an
older firmware cannot be mistaken for failures emitted by the current
preflight when generation counters restart after reset.

The CONTROL sweep covers slot identity; all 16 topology/PROGRAM descriptors;
Tone and FM contracts; Filter, VCA, Mixer, FX and polyphony canonical domains;
LFO/ENV/routes and eight physical modulation/temp owners; sampler, Multi and
wavetable kind/presence/none representation; MIDI endpoint exclusion; mute;
the complete 51-global CONTROL-to-command projection; tempo, step, metronome,
input ownership and every temp-clear bit.  It also cross-checks structural
identity against the decoded Pattern, installed CONTROL topology and the
prepared transaction identity used by SEQ.

The IRQ preflight covers metadata/generation/transition, changed PROGRAMs,
current renderer and held count, target voice count and required trim, total
synth allocator budget, descriptor installability, MIDI/external endpoint
eligibility, AUDIO command-domain values, physical modulation owners, input
owners, temp clears, sampler/Multi/RAM/stream READY state, wavetable slot and
registration generation, and rebind capacity.  `none` resources remain valid.
Any runtime record causes one DMB and the existing command fatal before the
first AUDIO mutation.

Renderer setters whose success depends on code executed inside their mutation
(for example a renderer-internal state transition after allocation) cannot be
executed speculatively.  Their structural, capacity, ownership and resource
preconditions are checked; their existing fatal remains the final guard.

## Cost and OFF proof

Release ON versus OFF, measured from the linked ELF:

| Region | ON - OFF |
|---|---:|
| FLASH | +9,816 bytes |
| DTCM | +64 bytes |
| D1 SRAM | -32 bytes (link/LTO layout variation) |
| D2 DMA/non-cacheable | 0 bytes |
| D2 cacheable | 0 bytes |
| D2 SRAM2 diagnostic window | +3,232 bytes |
| D3 | 0 bytes |
| SDRAM (all arenas) | 0 bytes |
| ITCM | -16 bytes (link/LTO layout variation) |

The OFF ELF has no `g_pattern_recall_diag`, no `pattern_recall_diag_*` symbol
and no diagnostic section/buffer.  The AUDIO IRQ source after preprocessing is
the production hot path: all diagnostic statements and the held-count helper
are absent.

## Logical activity and AUDIO endpoints

`PreparedAudio.entity[].active` describes topology membership, not physical
AUDIO ownership. An active top-level slot may carry PROGRAM OFF. PROGRAM MIDI
is active for sequencing/routing but has no physical AUDIO endpoint; PROGRAM
OFF has neither renderer nor AUDIO parameter endpoint. External and GROUP
master remain physical AUDIO endpoints even when GROUP uses engine NONE.

The runtime preflight therefore enforces the one-way topology invariant
`inactive => PROGRAM OFF`; it does not require `active => PROGRAM non-OFF`.
The real apply uses the same OFF/MIDI/physical endpoint classification: MIDI
configuration is installed for every active logical slot, then Tone/Common/
FX/Mute/Mod/Resource projection runs only for physical AUDIO endpoints.

## GDB capture

```gdb
shell cls
set pagination off
printf "Pattern Recall diag header\n"
p/x g_pattern_recall_diag.magic
p g_pattern_recall_diag.version
p g_pattern_recall_diag.size
p g_pattern_recall_diag.candidate_generation
p g_pattern_recall_diag.prepared_seq_generation
p g_pattern_recall_diag.prepared_audio_generation
p g_pattern_recall_diag.transition
p g_pattern_recall_diag.effective_sample_time
p g_pattern_recall_diag.phase
p g_pattern_recall_diag.max_phase
p g_pattern_recall_diag.static_failure_count
p g_pattern_recall_diag.runtime_failure_count
p g_pattern_recall_diag.record_count
p g_pattern_recall_diag.dropped_record_count
p/x g_pattern_recall_diag.changed_program_mask
p g_pattern_recall_diag.polyphony
set $i = 0
while $i < g_pattern_recall_diag.record_count
  p g_pattern_recall_diag.records[$i]
  set $i = $i + 1
end
p g_audio_command_fatal_record
p g_brick_fatal_record
```
