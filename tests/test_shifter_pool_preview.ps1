$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$root = Join-Path $PSScriptRoot '..'
$types = Get-Content -Raw (Join-Path $root 'Inc/Track/track_types.h')
$streamLimits = Get-Content -Raw (Join-Path $root `
    'Inc/Sampler/sample_stream_limits.h')
$shifterHeader = Get-Content -Raw (Join-Path $root `
    'Inc/Audio/brick6_clip_shifter.h')
$shifter = Get-Content -Raw (Join-Path $root `
    'Src/Audio/brick6_clip_shifter.c')
$runtime = Get-Content -Raw (Join-Path $root `
    'Src/Audio/Engines/Sampler/brick6_sampler_runtime.c')
$slots = Get-Content -Raw (Join-Path $root `
    'Src/Audio/Engines/Sampler/sampler_clip_voice.inc')
$preview = Get-Content -Raw (Join-Path $root `
    'Src/Audio/Engines/Sampler/sampler_preview_voice.inc')
$patchPreview = Get-Content -Raw (Join-Path $root `
    'Src/Audio/patch_preview_audio.c')

Assert-True ($types.Contains('#define BRICK6_MAX_CLIP_TRACKS 8U')) `
    'musical Shifter pool is not eight slots'
Assert-True ($streamLimits.Contains(
    '#define SAMPLE_STREAM_TARGET_MAX_VOICES          (8U)')) `
    'the existing shared STREAM/Multi reader budget changed unexpectedly'
Assert-True ($runtime.Contains(
    'g_sampler_clip_shifter_history[BRICK6_MAX_CLIP_TRACKS]') -and
    $runtime.Contains('AUDIO_HISTORY_SDRAM float')) `
    'musical histories are not one cacheable SDRAM arena'
Assert-True (-not $runtime.Contains('g_sampler_clip_shifter_delay_d1') -and
    -not $runtime.Contains('g_sampler_clip_shifter_delay_recorder') -and
    -not $runtime.Contains('.sdram_recorder_ring')) `
    'legacy split Shifter history placement remains'
Assert-True ($slots.Contains('g_sampler_clip_shifter_history[i][0]') -and
    $slots.Contains('g_sampler_clip_shifter_history[i][1]')) `
    'slot ownership is not bound dynamically to the SDRAM arena'

Assert-True ($shifterHeader.Contains('uint16_t valid_frames;')) `
    'logical history validity is missing'
$resetStart = $shifter.IndexOf('void brick6_clip_shifter_reset')
$resetEnd = $shifter.IndexOf('void brick6_clip_shifter_set_window_frames')
Assert-True (($resetStart -ge 0) -and ($resetEnd -gt $resetStart)) `
    'Shifter reset function boundaries were not found'
$resetBody = $shifter.Substring($resetStart, $resetEnd - $resetStart)
Assert-True (-not $resetBody.Contains('memset') -and
    $resetBody.Contains('valid_frames = 0U')) `
    'Shifter reset still clears history or lacks logical invalidation'
Assert-True ($shifter.Contains(
    'offset_integral < shifter->valid_frames') -and
    $shifter.Contains(
    '(offset_integral + 1U) < shifter->valid_frames')) `
    'old history taps are not independently masked'

Assert-True (-not $preview.Contains('brick6_clip_shifter') -and
    -not $preview.Contains('shifter_delay') -and
    -not $preview.Contains('stretch_mode') -and
    $preview.Contains('source->rate = 1.0f;') -and
    $preview.Contains('brick6_sampler_render_sample_segment_cursor')) `
    'sample Preview is not the minimal original-rate reader path'
Assert-True ($patchPreview.Contains(
    'patch_preview_engine_is_sample') -and
    $patchPreview.Contains('if (sample_preview == 0U)')) `
    'sample Preview still traverses generic modulation/DSP processing'

Write-Output 'Shifter pool and sample Preview regressions: PASS'
