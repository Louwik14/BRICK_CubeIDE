$ErrorActionPreference = 'Stop'

function Get-SliceBounds([uint64]$Frames, [uint32]$Count, [uint32]$Index) {
    if ($Index -ge $Count) { $Index = $Count - 1 }
    $begin = [uint64]([math]::Floor(($Frames * $Index) / $Count))
    $end = if (($Index + 1) -eq $Count) {
        $Frames
    } else {
        [uint64]([math]::Floor(($Frames * ($Index + 1)) / $Count))
    }
    if ($end -le $begin) { $end = $begin + 1 }
    return @($begin, $end)
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-AllSliceBounds([uint64]$Frames, [uint32]$Count) {
    $serialized = @()
    for ($index = 0; $index -lt $Count; $index++) {
        $bounds = Get-SliceBounds $Frames $Count $index
        $serialized += "$($bounds[0]):$($bounds[1])"
    }
    return ($serialized -join ',')
}

function Get-PlaybackBounds([uint64]$Frames, [double]$Start, [double]$Length) {
    $begin = [uint64]([math]::Floor($Frames * $Start))
    if ($begin -ge $Frames) { $begin = $Frames - 1 }
    $requested = [uint64]([math]::Floor($Frames * $Length))
    if ($requested -eq 0) { $requested = 1 }
    $effective = [math]::Min($requested, $Frames - $begin)
    $end = $begin + $effective
    return @($begin, $end)
}

$counts = @(1, 2, 4, 8, 16, 32, 64)
foreach ($frames in @(257, 4096, 96000, 4608013)) {
    foreach ($count in $counts) {
        $previousEnd = [uint64]0
        for ($index = 0; $index -lt $count; $index++) {
            $bounds = Get-SliceBounds $frames $count $index
            Assert-True ($bounds[0] -eq $previousEnd) "non-contiguous slice"
            Assert-True (($bounds[0] -lt $bounds[1]) -and
                         ($bounds[1] -le $frames)) "invalid slice bounds"
            $previousEnd = $bounds[1]
        }
        Assert-True ($previousEnd -eq $frames) "last slice does not reach END"

        $separators = @()
        for ($index = 1; $index -lt $count; $index++) {
            $frame = [uint64]([math]::Floor(($frames * $index) / $count))
            $separators += [uint32]([math]::Floor(($frame * 124) / $frames))
        }
        Assert-True ($separators.Count -eq ($count - 1)) "wrong grid line count"
    }
}

$full = Get-PlaybackBounds 96000 0.0 1.0
Assert-True (($full[0] -eq 0) -and ($full[1] -eq 96000)) `
    'full playback range is invalid'
$middle = Get-PlaybackBounds 96000 0.5 0.125
Assert-True (($middle[0] -eq 48000) -and ($middle[1] -eq 60000)) `
    'middle playback range is invalid'
$clampedRange = Get-PlaybackBounds 96000 0.9 0.5
Assert-True (($clampedRange[0] -eq 86400) -and ($clampedRange[1] -eq 96000)) `
    'LENGTH did not clamp at sample end with START priority'

# START/LENGTH, loop, pitch, P-lock and fast modulation values are deliberately
# absent from Get-SliceBounds. Their strongest possible changes must leave the
# complete serialized grid bit-identical, including at 64 slices.
foreach ($count in $counts) {
    $before = Get-AllSliceBounds 4608013 $count
    foreach ($rangeMutation in @(
        @(0.10, 0.90, 0.20, -24.0),
        @(0.35, 0.01, 0.99, 24.0),
        @(0.98, 1.00, 0.00, 0.0)
    )) {
        $after = Get-AllSliceBounds 4608013 $count
        Assert-True ($before -ceq $after) `
            "slice bounds changed under range/loop/pitch mutation at $count slices"
    }
}

# Simulate a control-rate LFO sweeping START/LENGTH: no sample-global bound may
# change or accumulate rounding drift.
$stable64 = Get-AllSliceBounds 4608013 64
for ($tick = 0; $tick -lt 4096; $tick++) {
    $unusedStart = (($tick * 37) % 1000) / 1000.0
    $unusedLength = (($tick * 91) % 1001) / 1000.0
    Assert-True ($stable64 -ceq (Get-AllSliceBounds 4608013 64)) `
        '64-slice grid changed under rapid START/LENGTH modulation'
}

foreach ($transition in @(
    @(4608013, 64, 96000, 4),
    @(96000, 4, 4608013, 64),
    @(4608013, 64, 4608013, 64)
)) {
    $newFrames = $transition[2]
    $newCount = $transition[3]
    foreach ($index in @(0, [uint32]($newCount / 2), ($newCount - 1))) {
        $bounds = Get-SliceBounds $newFrames $newCount $index
        Assert-True (($bounds[0] -lt $bounds[1]) -and
                     ($bounds[1] -le $newFrames)) "invalid transition bounds"
    }
}

$clamped = Get-SliceBounds 96000 16 48
$last = Get-SliceBounds 96000 16 15
Assert-True (($clamped[0] -eq $last[0]) -and
             ($clamped[1] -eq $last[1])) "slice selection was not clamped"

# A -> B, B -> A and sample -> OFF -> sample rebuild exclusively from the new
# sample frame count. First/middle/last slices remain playable after clamping.
foreach ($frames in @(4608013, 96000, 0, 257)) {
    if ($frames -eq 0) { continue }
    foreach ($index in @(0, 32, 63)) {
        $bounds = Get-SliceBounds $frames 64 $index
        Assert-True (($bounds[0] -lt $bounds[1]) -and
                     ($bounds[1] -le $frames)) 'sample transition slice invalid'
    }
}

$runtimeSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Audio/Engines/Sampler/sampler_clip_voice.inc')
$controlSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Audio/Engines/Sampler/sampler_stream_control.inc')
$uiSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/UI/Renderer/ui_renderer_sampler_wavetable.inc')
$dispatchSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Audio/Engines/Sampler/sampler_audio_dispatch.inc')
$modSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Mod/mod_destination_catalog.c')
$toneControlSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Track/tone_program_control.c')
$paramSpecSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Param/param_spec_catalog.inc')
$persistenceHeader = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Inc/Storage/persistent_control_codec.h')
$streamManager = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Sampler/sample_stream_manager.c')
$cacheLifecycle = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Sampler/PageCache/sample_page_cache_lifecycle.inc')
$cacheConfig = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Inc/Sampler/sample_page_cache_config.h')
Assert-True ($runtimeSource.Contains(
    'source.total_frames, count, index, &region_begin, &region_end')) `
    'trigger slices are not resolved from the complete sample'
Assert-True (-not $runtimeSource.Contains(
    'const uint32_t frames = region_end - region_begin;')) `
    'trigger slices still depend on playback range'
Assert-True ($controlSource.Contains(
    '((uint64_t)sample_frames * slice_index)')) `
    'sample-global slice formula missing'
Assert-True ($uiSource.Contains(
    '((uint64_t)frame_count * (uint64_t)i)')) `
    'Waveview slice grid is not sample-global'
Assert-True (-not $uiSource.Contains('region_frames * (uint64_t)i')) `
    'Waveview slice grid still depends on playback range'
Assert-True ($runtimeSource.Contains('stop_on_underrun = 0U;')) `
    'cold first-page pending/lease contract missing'
Assert-True (([regex]::Matches($dispatchSource,
    '&& \(voice->slice_count == 0U\)')).Count -eq 2) `
    'START/LENGTH still publish live slice recalculations'
Assert-True ($modSource.Contains('case PARAM_SAMPLER_LENGTH:') -and
             $modSource.Contains('MOD_DEST_APPLY_SAMPLER_LENGTH')) `
    'LENGTH modulation destination missing'
Assert-True ($toneControlSource.Contains(
    'PARAM_SAMPLER_LENGTH,p->state.stream.length')) `
    'LENGTH tone state/P-lock storage mapping missing'
Assert-True ($paramSpecSource.Contains(
    'PARAM_SAMPLER_LENGTH, "Length"')) `
    'LENGTH parameter is still exposed as END'
Assert-True (-not $paramSpecSource.Contains('PARAM_SAMPLER_END')) `
    'legacy END parameter still exists'
Assert-True ($persistenceHeader.Contains('#define PERSIST_CODEC_VERSION 21U')) `
    'persistence format was not bumped for LENGTH semantics'
Assert-True ($streamManager.Contains('sample_stream_manager_slice_service();') -and
             $streamManager.Contains('sample_page_cache_slice_retain_page') -and
             $streamManager.Contains('state->prepared = ready;') -and
             $streamManager.Contains('sample_stream_manager_slice_has_pending_work()')) `
    'cooperative slice preload/readiness contract missing'
Assert-True ($cacheLifecycle.Contains(
    'SAMPLE_PAGE_SLICE_REF_COUNT(page) != 0U') -and
             $cacheLifecycle.Contains(
    'SAMPLE_PAGE_SLICE_REF_COUNT(page) == 0U')) `
    'slice retains do not protect reserved pages from cancellation'
Assert-True ($cacheConfig.Contains(
    '#define SAMPLE_PAGE_SLICE_CARRIER_COUNT        (512U)') -and
             $cacheConfig.Contains(
    '#define SAMPLE_PAGE_SLICE_DESCRIPTOR_COUNT     (512U)')) `
    'fixed 512-page stereo slice reserve missing'

$holdSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Audio/Engines/Sampler/sampler_multi_voice.inc')
Assert-True ($holdSource.Contains('brick6_sampler_mode_is_hold_value(')) `
    'HOLD policy guard missing'
Assert-True ($holdSource.Contains('g_sampler_voice[track_id].mode) == 0U')) `
    'HOLD release guard missing'
Assert-True ($holdSource.Contains('g_sampler_voice[track_id].release_pending = 1U')) `
    'HOLD release action missing'

Write-Output 'sampler slice regressions: PASS'
