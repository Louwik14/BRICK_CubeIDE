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

$holdSource = Get-Content -Raw (Join-Path $PSScriptRoot `
    '../Src/Audio/Engines/Sampler/sampler_multi_voice.inc')
Assert-True ($holdSource.Contains('brick6_sampler_mode_is_hold_value(')) `
    'HOLD policy guard missing'
Assert-True ($holdSource.Contains('g_sampler_voice[track_id].mode) == 0U')) `
    'non-HOLD release guard missing'
Assert-True ($holdSource.Contains('g_sampler_voice[track_id].release_pending = 1U')) `
    'non-HOLD release action missing'

Write-Output 'sampler slice regressions: PASS'
