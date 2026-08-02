$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$pipelineHeader = Get-Content -Raw (Join-Path $root 'Inc/NoteFx/note_fx_pipeline.h')
$engine = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$terminal = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_output_guard.c')
$midi = Get-Content -Raw (Join-Path $root 'Src/MIDI/midi.c')
$clock = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime_exec.c')
$state = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_state.c')
$persistence = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_live_ram.c')
$files = Get-ChildItem (Join-Path $root 'Src/NoteFx'),
                         (Join-Path $root 'Src/Seq'),
                         (Join-Path $root 'Inc/NoteFx'),
                         (Join-Path $root 'Inc/Seq') -File -Recurse |
         Where-Object { $_.Extension -in @('.c', '.h') } |
         ForEach-Object { Get-Content -Raw $_.FullName }
$allNoteFxSeq = $files -join "`n"

if ($allNoteFxSeq -match 'g_seq_play_active_event_token|note_fx_pipeline_before_model_change|note_fx_pipeline_on_base_param_change') {
    throw 'historical NoteFx/scheduler helper remains in active code'
}
if ($scheduler -match 'first\s+ARP|premier\s+ARP|g_seq_play_active_event_token' -or
    $engine -match 'g_note_fx_runtime_arp_slot|first\s+ARP') {
    throw 'first-ARP or pitch-only ownership remains'
}
if ($engine -match 'seq_play_scheduler_dispatch_terminal_event' -or
    $scheduler -notmatch 'seq_play_scheduler_dispatch_terminal_event' -or
    $pipeline -notmatch 'note_fx_pipeline_terminal') {
    throw 'FX terminal path is not shared and staged'
}
if ($pipelineHeader -match 'NOTE_EVENT_RESULT_BOTH|NOTE_EVENT_RESULT.*BOTH' -or
    $terminal -notmatch 'midi_dest_mask' -or
    $midi -notmatch 'midi_note_on_admit' -or
    $midi -notmatch 'midi_note_off_admit') {
    throw 'terminal admission still requires an atomic BOTH result'
}
if ($clock -notmatch 'SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK' -or
    $clock -notmatch 'external_pulses_coalesced') {
    throw 'external clock backlog is not bounded and instrumented'
}
if ($allNoteFxSeq -match '\b(malloc|calloc|realloc|free)\s*\(') {
    throw 'dynamic allocation remains in NoteFx/scheduler scope'
}
if ($persistence -match 'note_fx_engine|owned\[|next_sample|source_generation|phase') {
    throw 'runtime NoteFx state leaks into persistence'
}
if ($state -notmatch 'note_fx_state_normalize_track' -or
    $pipeline -notmatch 'NOTE_FX_COMMAND_CAPACITY 32U') {
    throw 'central normalization or bounded owner queue is missing'
}

Write-Output 'note_fx_step10_consolidation_validation=PASS old_helpers=absent first_arp=absent terminal=shared admissions=independent clock_backlog=bounded dynamic_alloc=absent runtime_persistence=absent'
