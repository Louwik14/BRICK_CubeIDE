$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$header = Get-Content -Raw (Join-Path $root 'Inc/NoteFx/note_fx_pipeline.h')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$mute = Get-Content -Raw (Join-Path $root 'Src/Core/track_mute.c')
$bridge = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_mute_bridge.c')

if ($header -notmatch 'NOTE_FX_TRANSITION_MUTE_TRIGS' -or
    $header -notmatch 'NOTE_FX_TRANSITION_DESTINATION_REBIND') {
    throw 'transition policy surface is incomplete'
}
if ($pipeline -notmatch 'NOTE_FX_COMMAND_CAPACITY 32U' -or
    $pipeline -notmatch 'note_fx_pipeline_enqueue' -or
    $pipeline -notmatch 'note_fx_pipeline_dequeue') {
    throw 'fixed NoteFx command queue is missing'
}
if ($pipeline -notmatch '(?s)note_fx_pipeline_process.*?note_fx_pipeline_apply_pending_commands\(\);') {
    throw 'commands are not applied at the audio owner boundary'
}
if ($pipeline -notmatch 'note_fx_pipeline_submit_audio') {
    throw 'audio-owner event seam is missing'
}
if ($bridge -notmatch 'SEQ_PLAY_TRANSITION_MUTE_TRIGS' -or
    $bridge -notmatch 'SEQ_PLAY_TRANSITION_RESUME_TRIGS' -or
    $bridge -match 'seq_play_scheduler_clear_tracks|note_fx_pipeline_cleanup') {
    throw 'MUTE_TRIGS transition is not non-destructive'
}
if ($scheduler -notmatch '\(candidate->type != \(uint8_t\)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF\)') {
    throw 'owned Note Off is not exempted from muted-track filtering'
}
if ($mute -match 'keyboard_engine_all_notes_off_for_track') {
    throw 'central mute still invokes destructive all-notes cleanup'
}

Write-Output 'note_fx_transition_validation=PASS owner_queue=32 policies=explicit mute=non_destructive owned_off=allowed'
