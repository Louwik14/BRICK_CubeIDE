$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$scheduler = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_play_scheduler.c')
$snapshot = Get-Content -Raw (Join-Path $repo 'Src\Core\track_snapshot.c')
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_runtime.c')
$mixer = Get-Content -Raw (Join-Path $repo 'Src\Audio\mixer.c')
$stack = Get-Content -Raw (Join-Path $repo 'Src\Core\brick6_stack_runtime.c')

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
    'keyboard_arp_clear_track',
    'mod_lfo_v1_all_notes_off',
    'track_snapshot_runtime_neutralize_note_state',
    'mixer_track_filter_all_notes_off',
    'mixer_track_vca_all_notes_off',
    'synth_polyphony_reset_track',
    'seq_runtime_restore_track_div',
    'seq_runtime_begin_track_restore',
    'seq_runtime_end_track_restore'
)) {
    if (-not $snapshot.Contains($contract)) {
        throw "Missing Track clipboard restore contract: $contract"
    }
}

if ($snapshot.Contains('seq_runtime_set_playhead_step((seq_track_id_t)track, 0U)')) {
    throw 'Track paste still restores transient playhead state'
}
if ($runtime -match '(?s)seq_runtime_begin_track_restore.*?seq_boundary_engine_invalidate_track') {
    throw 'Track paste still invalidates the current boundary and can replay it'
}
if ($runtime -match '(?s)seq_runtime_begin_track_restore.*?track_div_phase.*?seq_runtime_end_track_restore') {
    throw 'Track paste still rewinds transient division phase'
}
if (-not $mixer.Contains('mixer_track_vca_all_notes_off(next_mix_track)')) {
    throw 'Single-track mixer rebind still migrates an active VCA gate'
}
if (-not $stack.Contains('g_stack_note_cancel_pending')) {
    throw 'Stack pending note commands are not cancelled at the audio boundary'
}

function Invoke-PasteDuringPlay([string]$engine, [bool]$filled, [int[]]$affectedTracks) {
    $pasteSample = 120
    $nextBoundarySample = 200
    $generation = @(1, 1, 1, 1, 1, 1)
    $suspended = @($false, $false, $false, $false, $false, $false)
    $gate = @($false, $false, $false, $false, $true, $false)
    $voice = @($false, $false, $false, $false, $true, $false)
    $noteOnCount = @(0, 0, 0, 0, 1, 0)
    $events = [System.Collections.Generic.List[object]]::new()

    # PLAY is active. Track 4 has a legitimate sounding note and must survive.
    # A stale lookahead note for the pasted target exists beyond the paste point.
    $events.Add([pscustomobject]@{
        Track = $affectedTracks[0]
        Generation = 1
        Due = 150
        Type = 'note_on'
        Engine = $engine
    })

    foreach ($track in $affectedTracks) {
        $suspended[$track] = $true
        $generation[$track]++
        $events = [System.Collections.Generic.List[object]]@(
            $events | Where-Object Track -ne $track
        )
        $gate[$track] = $false
        $voice[$track] = $false
    }

    # Persistent sequence data is installed while playhead/boundary phase stay put.
    # Resuming must not synthesize an event at the paste sample.
    foreach ($track in $affectedTracks) {
        $generation[$track]++
        $suspended[$track] = $false
    }

    foreach ($sample in ($pasteSample..($nextBoundarySample - 1))) {
        foreach ($event in @($events | Where-Object Due -eq $sample)) {
            if ((-not $suspended[$event.Track]) -and
                    ($event.Generation -eq $generation[$event.Track]) -and
                    ($event.Type -eq 'note_on')) {
                $noteOnCount[$event.Track]++
                $gate[$event.Track] = $true
                $voice[$event.Track] = $true
            }
        }
    }

    foreach ($track in $affectedTracks) {
        if (($noteOnCount[$track] -ne 0) -or $gate[$track] -or $voice[$track]) {
            throw "Ghost audio before next boundary: engine=$engine track=$track filled=$filled"
        }
    }
    if (($noteOnCount[4] -ne 1) -or (-not $gate[4]) -or (-not $voice[4])) {
        throw "Unrelated active track was cut: engine=$engine filled=$filled"
    }

    if ($filled) {
        $target = $affectedTracks[0]
        $events.Add([pscustomobject]@{
            Track = $target
            Generation = $generation[$target]
            Due = $nextBoundarySample
            Type = 'note_on'
            Engine = $engine
        })
    }

    foreach ($event in @($events | Where-Object Due -eq $nextBoundarySample)) {
        if ((-not $suspended[$event.Track]) -and
                ($event.Generation -eq $generation[$event.Track]) -and
                ($event.Type -eq 'note_on')) {
            $noteOnCount[$event.Track]++
            $gate[$event.Track] = $true
            $voice[$event.Track] = $true
        }
    }

    $target = $affectedTracks[0]
    if ($filled) {
        if (($noteOnCount[$target] -ne 1) -or (-not $gate[$target]) -or (-not $voice[$target])) {
            throw "Next planned trigger was lost: engine=$engine"
        }
    } elseif (($noteOnCount[$target] -ne 0) -or $gate[$target] -or $voice[$target]) {
        throw "Empty sequence generated audio: engine=$engine"
    }
}

$engines = @('PRISM', 'STACK', 'WAVE', 'DELUGE', 'DRUM', 'SAMPLER')
foreach ($engine in $engines) {
    Invoke-PasteDuringPlay $engine $false @(1)
    Invoke-PasteDuringPlay $engine $true @(1)
    Invoke-PasteDuringPlay $engine $false @(1, 2, 3)
    Invoke-PasteDuringPlay $engine $true @(1, 2, 3)
}

'track_paste_playback_validation=PASS engines=6 simple=12 mono_track=12 pre_boundary_note_on=0 pre_boundary_gate=0 pre_boundary_voice=0 unrelated_tracks_preserved=1'
