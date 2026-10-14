$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$root = Join-Path $PSScriptRoot '..'
$runtime = Get-Content -Raw (Join-Path $root `
    'Src/Audio/Engines/Sampler/sampler_clip_voice.inc')
$dispatch = Get-Content -Raw (Join-Path $root `
    'Src/Audio/Engines/Sampler/sampler_audio_dispatch.inc')
$executor = Get-Content -Raw (Join-Path $root `
    'Src/Audio/audio_command_executor.c')

Assert-True ($runtime.Contains(
    'g_sampler_clip_runtime[track_id].sample_id = UINT16_MAX;')) `
    'empty Stream source aliases valid sample 0'
Assert-True ($dispatch.Contains('if (clip->source == next) return;') -and
             $dispatch.Contains(
                 'brick6_sampler_runtime_clip_stop_playback(track_id);')) `
    'source transition does not retire the reader/lease lifetime'
Assert-True (-not $executor.Contains(
    'current_sample == (uint16_t)command->value')) `
    'selector equality still skips held-output repair'
Write-Output 'sampler source switch regressions: PASS'
