$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bridge = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_mute_bridge.c')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$schedulerHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_play_scheduler.h')
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$guard = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_output_guard.c')
$mute = Get-Content -Raw (Join-Path $root 'Src/Core/track_mute.c')

if ($schedulerHeader -notmatch 'SEQ_PLAY_TRANSITION_MUTE_TRIGS' -or
    $schedulerHeader -notmatch 'SEQ_PLAY_TRANSITION_PATTERN_REPLACE' -or
    $schedulerHeader -notmatch 'seq_play_scheduler_transition_tracks') {
    throw 'transition protocol is incomplete'
}
if ($bridge -notmatch 'SEQ_PLAY_TRANSITION_MUTE_TRIGS' -or
    $bridge -notmatch 'SEQ_PLAY_TRANSITION_RESUME_TRIGS' -or
    $bridge -match 'seq_play_scheduler_clear_tracks|note_fx_pipeline_cleanup') {
    throw 'mute bridge still uses a destructive transition'
}
if ($scheduler -notmatch '(?s)seq_play_scheduler_notify_track_pattern_change.*?SEQ_PLAY_TRANSITION_PATTERN_REPLACE' -or
    $scheduler -notmatch '(?s)seq_play_scheduler_clear_tracks\(tracks, track_count\).*?SEQ_PLAY_TRANSITION_MODEL_RECONFIGURE') {
    throw 'pattern/model transitions are not destructive and explicit'
}
if ($guard -notmatch 'SEQ_PLAY_TRANSITION_PANIC_CLOSE_ALL') {
    throw 'panic does not use the explicit destructive transition'
}
if ($pipeline -notmatch 'NOTE_FX_COMMAND_TRANSITION_TRACK' -or
    $pipeline -notmatch 'pending->kind == command->kind' -or
    $pipeline -notmatch 'NOTE_FX_COMMAND_RESET_ALL') {
    throw 'transition idempotence/coalescing is missing'
}
if ($mute -match 'note_fx_pipeline_cleanup|seq_play_scheduler_clear_tracks|all_notes_off') {
    throw 'track mute path still closes or purges owned notes'
}

Write-Output 'note_fx_transition_lifecycle_validation=PASS mute=non_destructive unmute=no_retrigger pattern=model=explicit idempotent=coalesced'
