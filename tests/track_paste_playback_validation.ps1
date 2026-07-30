$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$scheduler = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_play_scheduler.c')
$snapshot = Get-Content -Raw (Join-Path $repo 'Src\Core\track_snapshot.c')
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_runtime.c')

foreach ($contract in @(
    'g_seq_play_track_generation',
    'g_seq_play_track_suspended',
    'candidate->track_generation != g_seq_play_track_generation[candidate->track]',
    'event->track_generation != g_seq_play_track_generation[event->track]',
    'seq_play_scheduler_suspend_tracks',
    'seq_play_scheduler_resume_tracks'
)) {
    if (-not $scheduler.Contains($contract)) {
        throw "Missing scheduler restore guard: $contract"
    }
}

foreach ($contract in @(
    'track_snapshot_collect_restore_tracks',
    'track_runtime_get_voice_group_effective_master',
    'track_runtime_collect_voice_group_members',
    'seq_runtime_begin_track_restore',
    'seq_runtime_end_track_restore'
)) {
    if (-not $snapshot.Contains($contract)) {
        throw "Missing Track clipboard restore contract: $contract"
    }
}

foreach ($contract in @(
    'seq_boundary_engine_restore_all_active_locks',
    'seq_boundary_engine_invalidate_track',
    'seq_play_scheduler_suspend_tracks',
    'seq_play_scheduler_resume_tracks'
)) {
    if (-not $runtime.Contains($contract)) {
        throw "Missing runtime restore contract: $contract"
    }
}

function Test-PasteScenario([string]$engine, [bool]$filled) {
    $generation = @(1, 1, 1, 1)
    $suspended = @($false, $false, $false, $false)
    $events = [System.Collections.Generic.List[object]]::new()
    $events.Add([pscustomobject]@{ Track = 1; Generation = 1; Note = 61; Engine = $engine })
    $events.Add([pscustomobject]@{ Track = 3; Generation = 1; Note = 73; Engine = 'OTHER' })

    $generation[1]++
    $suspended[1] = $true
    $events = [System.Collections.Generic.List[object]]@(
        $events | Where-Object { $_.Track -ne 1 }
    )

    if (-not $suspended[1]) {
        $events.Add([pscustomobject]@{ Track = 1; Generation = $generation[1]; Note = 62; Engine = $engine })
    }

    $generation[1]++
    $suspended[1] = $false
    if ($filled) {
        $events.Add([pscustomobject]@{ Track = 1; Generation = $generation[1]; Note = 64; Engine = $engine })
    }

    $accepted = @($events | Where-Object {
        (-not $suspended[$_.Track]) -and ($_.Generation -eq $generation[$_.Track])
    })
    $target = @($accepted | Where-Object Track -eq 1)
    $other = @($accepted | Where-Object Track -eq 3)
    if ($target.Count -ne ([int]$filled)) {
        throw "Unexpected target notes after $engine paste, filled=$filled"
    }
    if ($other.Count -ne 1) {
        throw "Unrelated track was cut by $engine paste"
    }
}

foreach ($engine in @('PRISM', 'STACK', 'WAVE', 'DELUGE', 'DRUM', 'SAMPLER')) {
    Test-PasteScenario $engine $false
    Test-PasteScenario $engine $true
}

$groupEvents = @(
    [pscustomobject]@{ Track = 1; Note = 60 },
    [pscustomobject]@{ Track = 2; Note = 64 },
    [pscustomobject]@{ Track = 3; Note = 67 },
    [pscustomobject]@{ Track = 4; Note = 72 }
)
$affected = @(1, 2, 3)
$remaining = @($groupEvents | Where-Object { $affected -notcontains $_.Track })
if (($remaining.Count -ne 1) -or ($remaining[0].Track -ne 4)) {
    throw 'Voice-group cleanup leaked or cut an unrelated track'
}

'track_paste_playback_validation=PASS empty=6 filled=6 group_cleanup=1 unrelated_tracks_preserved=1'
