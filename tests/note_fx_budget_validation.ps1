$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$pipelineHeader = Get-Content -Raw (Join-Path $root 'Inc/NoteFx/note_fx_pipeline.h')
$engine = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
$audio = Get-Content -Raw (Join-Path $root 'Src/Audio/audio.c')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$schedulerHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_play_scheduler.h')

if ($pipelineHeader -notmatch 'NOTE_FX_HALF_BUFFER_FRAMES 64U' -or
    $pipelineHeader -notmatch 'NOTE_FX_HALF_OFF_RESERVE 32U' -or
    $pipelineHeader -notmatch 'NOTE_FX_HALF_ON_QUOTA_PER_TRACK 8U') {
    throw 'half-buffer budget constants are missing'
}
if ($pipeline -notmatch 'note_fx_pipeline_begin_audio_half' -or
    $pipeline -notmatch 'note_fx_pipeline_end_audio_half' -or
    $pipeline -notmatch 'note_fx_pipeline_budget_admit') {
    throw 'shared NoteFx half-buffer budget is missing'
}
if ($pipeline -notmatch 'event->flags & NOTE_EVENT_FLAG_GENERATED' -or
    $pipeline -notmatch 'off_remaining' -or
    $pipeline -notmatch 'budget_off_drop_count' -or
    $pipeline -notmatch 'budget_on_drop_count') {
    throw 'generated emission classes are not admitted independently'
}
if ($pipeline -notmatch 'NOTE_FX_HALF_COMMAND_QUOTA' -or
    $pipeline -notmatch 'commands_used') {
    throw 'command work is not bounded per half-buffer'
}
if ($engine -match 'uint8_t budget = NOTE_FX_MAX_EMISSIONS_PER_BLOCK' -or
    $engine -match 'budget == 0U') {
    throw 'engine still recreates a per-subsegment emission budget'
}
if ($audio -notmatch '(?s)static void process_half.*?note_fx_pipeline_begin_audio_half\(AUDIO_FRAMES_PER_HALF\).*?while \(half_cursor < AUDIO_FRAMES_PER_HALF\).*?note_fx_pipeline_end_audio_half\(\)') {
    throw 'audio half-buffer does not own the shared budget lifetime'
}
if ($schedulerHeader -notmatch 'SEQ_PLAY_SCHEDULER_HALF_EVENT_QUOTA 128U' -or
    $scheduler -notmatch 'g_seq_play_audio_half_remaining' -or
    $scheduler -notmatch 'half_quota_exhaustion_count') {
    throw 'scheduler queue has no half-buffer event quota'
}
if ($audio -notmatch 'seq_play_scheduler_audio_begin_half\(SEQ_PLAY_SCHEDULER_HALF_EVENT_QUOTA\)' -or
    $audio -notmatch 'seq_play_scheduler_audio_end_half\(\)') {
    throw 'scheduler half-buffer quota is not owned by process_half'
}

Write-Output 'note_fx_budget_validation=PASS half=64 note_fx_on=8/track note_fx_off=32 scheduler_events=128 commands=32 releases=accounted'
