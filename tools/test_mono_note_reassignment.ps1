$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$polyphony = Get-Content 'Src/Track/synth_polyphony.c' -Raw
$adapter = Get-Content 'Src/Audio/audio_note_engine_adapter.c' -Raw
$multi = Get-Content 'Src/Audio/Engines/Sampler/sampler_multi_voice.inc' -Raw

Assert-Contract ($polyphony -match
    'g_synth_voice\[slot\]\.state == SYNTH_POLY_VOICE_HELD[\s\S]*?g_synth_voice\[slot\]\.age - oldest') `
    'the allocator cannot steal the oldest HELD physical voice'
Assert-Contract ($adapter -match 'displaced_output\[voice\] != 0U') `
    'displaced output identity is not retired'
Assert-Contract ($adapter -match 'held_reassignment') `
    'stolen track-level note lifetime is not transferred'
Assert-Contract ($adapter -notmatch 'note_on_reassign_mono') `
    'an engine-specific mono bypass still exists'
Assert-Contract ($multi -match
    'track_voice_count >= voice_limit[\s\S]*?BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL') `
    'Sampler Multi does not steal at its configured per-track voice limit'

function New-MonoState {
    return [pscustomobject]@{
        Output = [uint32]0
        Note = [byte]0
        Gate = $false
        LifetimeCount = 0
        Mirror = [System.Collections.Generic.HashSet[uint32]]::new()
    }
}

function Note-On($state, [uint32]$output, [byte]$note) {
    $old = $state.Output
    if ($state.Gate) { $state.LifetimeCount-- }
    $state.Output = $output
    $state.Note = $note
    $state.Gate = $true
    $state.LifetimeCount++
    if ($old -ne 0) { [void]$state.Mirror.Remove($old) }
    [void]$state.Mirror.Add($output)
}

function Note-Off($state, [uint32]$output) {
    if (-not $state.Mirror.Contains($output)) { return }
    [void]$state.Mirror.Remove($output)
    if ($state.Output -eq $output) {
        $state.Output = 0
        $state.Gate = $false
        $state.LifetimeCount--
    }
}

foreach ($engine in 'PRISM','STACK','WAVE','FM','ACID','TB303','MULTI') {
    $state = New-MonoState
    Note-On $state 0x20000006 60
    Note-On $state 0x20000007 64
    Assert-Contract ($state.Gate -and $state.Note -eq 64) `
        "${engine}: replacement NOTE_ON was not applied"
    Assert-Contract ($state.LifetimeCount -eq 1) `
        "${engine}: replacement leaked a track-level note lifetime"
    Assert-Contract ($state.Mirror.Count -eq 1 -and
        $state.Mirror.Contains([uint32]0x20000007)) `
        "${engine}: displaced output identity remained mapped"
    Note-Off $state 0x20000006
    Assert-Contract ($state.Gate -and $state.Note -eq 64) `
        "${engine}: stale NOTE_OFF killed the new owner"
    Note-Off $state 0x20000007
    Assert-Contract (-not $state.Gate -and $state.Mirror.Count -eq 0) `
        "${engine}: current NOTE_OFF did not release the voice"

    Note-On $state 0x2000000A 60
    Note-On $state 0x2000000B 64
    Note-Off $state 0x2000000B
    Assert-Contract (-not $state.Gate -and $state.Mirror.Count -eq 0) `
        "${engine}: newest-first release left the stolen owner active"
    Note-Off $state 0x2000000A
    Assert-Contract (-not $state.Gate -and $state.Mirror.Count -eq 0) `
        "${engine}: delayed stolen NOTE_OFF resurrected or changed the voice"

    foreach ($index in 8..71) {
        Note-On $state ([uint32](0x20000000 + $index)) ([byte](48 + ($index % 24)))
        Assert-Contract ($state.Mirror.Count -eq 1 -and
            $state.LifetimeCount -eq 1) `
            "${engine}: rapid replacement leaked an output identity"
    }
    Note-Off $state $state.Output
    Assert-Contract (-not $state.Gate -and $state.Mirror.Count -eq 0 -and
        $state.LifetimeCount -eq 0) `
        "${engine}: rapid replacement left a stale owner"
}

'mono note reassignment: PASS'
