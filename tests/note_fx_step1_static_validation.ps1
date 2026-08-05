$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$pipelineHeader = Get-Content -Raw (Join-Path $root 'Inc/NoteFx/note_fx_pipeline.h')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime_exec.c')
$state = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_state.c')
$cmake = Get-Content -Raw (Join-Path $root 'tests/CMakeLists.txt')

if ($pipelineHeader -notmatch 'NOTE_FX_SAMPLE_TIME_AUDIO_OWNER UINT64_MAX' -or
    $pipeline -notmatch 'sample_time == NOTE_FX_SAMPLE_TIME_AUDIO_OWNER' -or
    $pipeline -notmatch 'seq_runtime_exec_get_audio_timeline_sample') {
    throw 'audio-owner sample seam is missing'
}
if ($scheduler -notmatch 'seq_play_scheduler_admit_internal_note' -or
    $scheduler -notmatch 'fixed mono occurrence lease' -or
    $scheduler -notmatch 'internal_admitted' -or
    $scheduler -notmatch 'midi_dest_mask' -or
    $scheduler -notmatch '\(internal_admitted == 0U\).*\(midi_dest_mask == 0U\)') {
    throw 'explicit internal adapter or independent terminal admission is missing'
}
if ($scheduler -match 'seq_play_scheduler_dispatch_terminal_note(_to_channel)?\s*\(') {
    throw 'legacy pitch-only terminal wrapper remains'
}
if ($runtime -notmatch 'volatile uint32_t g_seq_runtime_exec_external_step_pulses_pending' -or
    $runtime -notmatch 'external_step_pulses_overflowed' -or
    $runtime -notmatch 'SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK') {
    throw 'external pulse overflow/coalescing contract is missing'
}
if ($pipeline -notmatch 'g_note_fx_commands' -or
    $pipeline -notmatch 'note_fx_pipeline_apply_pending_commands' -or
    $pipeline -notmatch 'note_fx_pipeline_process') {
    throw 'owner command queue/interleaving contract is missing'
}
if ($state -notmatch 'restore is also a model transition' -or
    $state -notmatch 'note_fx_state_restore_track_exact' -or
    $state -notmatch 'note_fx_state_default_for_model\(model, param\)') {
    throw 'restore model-default transaction is missing'
}
if ($cmake -notmatch 'note_fx_state_restore_test.c' -or
    $cmake -notmatch 'note_fx_step1_static_validation.ps1') {
    throw 'step 1 tests are not registered from the checkout'
}

Write-Output 'note_fx_step1_static_validation=PASS adapter=explicit terminal=independent owner=queued restore=model-defaults clock=uint32-overflow'
