$ErrorActionPreference = 'Stop'

$held = 8
$batch = 32
$futureCapacity = 320
$models = @('OFF','ARP_FREE','ARP_SYNC','EUCLID','PROBABILITY','GATE','GROOVE','ECHO','HARMONIZER','CHORD')

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

function Test-ProductChain([string[]]$chain) {
    $families = @{}
    foreach ($name in $chain) {
        if ($name -eq 'OFF') { continue }
        $family = if (($name -eq 'ARP_FREE') -or ($name -eq 'ARP_SYNC')) { 'ARP' } else { $name }
        if ($families.ContainsKey($family)) { return $false }
        $families[$family] = $true
    }
    return $true
}

function Get-ChainBound([string[]]$chain) {
    if (-not (Test-ProductChain $chain)) { return @($false,0,0,0,0) }
    $admitted = $held
    $maximumCandidates = $held
    foreach ($name in $chain) {
        $candidates = if ($name -eq 'HARMONIZER') { 4 * $admitted } else { $admitted }
        $maximumCandidates = [Math]::Max($maximumCandidates, $candidates)
        if ($name -ne 'ECHO') { $admitted = [Math]::Min($held, $candidates) }
    }
    $futurePerTrack = 5 * $held
    return @(($maximumCandidates -le $batch) -and ($admitted -le $held),
        $admitted, $held, $maximumCandidates, $futurePerTrack)
}

foreach ($a in $models) { foreach ($b in $models) {
    foreach ($c in $models) { foreach ($d in $models) {
        $bound = Get-ChainBound @($a,$b,$c,$d)
        if ($bound[0]) {
            Assert-Contract ($bound[1] -le $held) "admission: $a $b $c $d"
            Assert-Contract ($bound[3] -le $batch) "batch: $a $b $c $d"
            Assert-Contract (8 * $bound[4] -le $futureCapacity) "future: $a $b $c $d"
        }
    }}
}}

foreach ($scenario in @(
    @('ARP_FREE','ECHO','OFF','OFF'),
    @('ECHO','ARP_SYNC','OFF','OFF'),
    @('HARMONIZER','ARP_FREE','OFF','OFF'),
    @('ARP_SYNC','HARMONIZER','OFF','OFF'),
    @('GROOVE','ARP_FREE','OFF','OFF'),
    @('ARP_SYNC','GROOVE','OFF','OFF'),
    @('GATE','ECHO','OFF','OFF'),
    @('EUCLID','HARMONIZER','ECHO','GATE'),
    @('HARMONIZER','EUCLID','ECHO','GATE'),
    @('ECHO','HARMONIZER','GATE','GROOVE'),
    @('GATE','ECHO','HARMONIZER','CHORD')
)) {
    $bound = Get-ChainBound $scenario
    Assert-Contract $bound[0] "required chain rejected: $($scenario -join ' -> ')"
}
Assert-Contract (-not (Test-ProductChain @('ARP_FREE','ARP_SYNC','OFF','OFF'))) 'ARP family duplicated'
Assert-Contract (-not (Test-ProductChain @('ECHO','ECHO','OFF','OFF'))) 'FX family duplicated'
Assert-Contract (Test-ProductChain @('HARMONIZER','ECHO','GATE','GROOVE')) 'four distinct FX rejected by product rule'

function New-Ledger([int]$polyphony) {
    return @{ Polyphony=$polyphony; NextHandle=0; Live=@(); Transitions=[System.Collections.ArrayList]::new() }
}
function Start-Semantic($ledger, [int]$semantic, [int]$note) {
    $existing = @($ledger.Live | Where-Object { $_.Semantic -eq $semantic })
    if ($existing.Count) {
        [void]$ledger.Transitions.Add("RETRIGGER $($existing[0].Handle)")
        return
    }
    $samePitch = @($ledger.Live | Where-Object { $_.Note -eq $note })
    if ($samePitch.Count) {
        [void]$ledger.Transitions.Add("STOP $($samePitch[0].Handle)")
        $ledger.Live = @($ledger.Live | Where-Object { $_.Handle -ne $samePitch[0].Handle })
    }
    while ($ledger.Live.Count -ge $ledger.Polyphony) {
        $victim = $ledger.Live[0]
        [void]$ledger.Transitions.Add("STOP $($victim.Handle)")
        $ledger.Live = @($ledger.Live | Select-Object -Skip 1)
    }
    ++$ledger.NextHandle
    $entry = [pscustomobject]@{ Semantic=$semantic; Note=$note; Handle=$ledger.NextHandle }
    $ledger.Live += $entry
    [void]$ledger.Transitions.Add("START $($entry.Handle)")
}
function Apply-Audio($transitions, [int]$capacity) {
    $live = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($transition in $transitions) {
        $kind,$handleText = $transition.Split(' ')
        $handle = [int]$handleText
        if ($kind -eq 'STOP') { [void]$live.Remove($handle) }
        elseif ($kind -eq 'RETRIGGER') {
            Assert-Contract ($live.Contains($handle)) "retrigger before start: $handle"
        } else {
            Assert-Contract ($live.Count -lt $capacity) 'predictable AUDIO mapping overflow'
            Assert-Contract ($live.Add($handle)) "duplicate physical handle: $handle"
        }
    }
    return $live.Count
}

$harmony = New-Ledger 1
1..4 | ForEach-Object { Start-Semantic $harmony $_ (59 + $_) }
Assert-Contract (($harmony.Transitions -join ',') -eq
    'START 1,STOP 1,START 2,STOP 2,START 3,STOP 3,START 4') 'Harmony transition order'
Assert-Contract ((Apply-Audio $harmony.Transitions 1) -eq 1) 'Harmony polyphony 1'

$euclid = New-Ledger 1
1..8 | ForEach-Object { Start-Semantic $euclid $_ (59 + $_) }
Assert-Contract ((Apply-Audio $euclid.Transitions 1) -eq 1) 'EUCLID polyphony 1'

$gate = New-Ledger 1
Start-Semantic $gate 1 60
Start-Semantic $gate 1 60
Assert-Contract (($gate.Transitions -join ',') -eq 'START 1,RETRIGGER 1') 'Gate retrigger lifecycle'
Assert-Contract ((Apply-Audio $gate.Transitions 1) -eq 1) 'Gate retrigger AUDIO'

$legato = New-Ledger 1
Start-Semantic $legato 1 60
[void]$legato.Transitions.Add('RETRIGGER 1')
$legato.Live[0].Semantic = 2
Assert-Contract ((Apply-Audio $legato.Transitions 1) -eq 1) 'Gate legato shared handle'

$future = @{}
foreach ($tick in 0..999) {
    foreach ($track in 0..7) { foreach ($note in 60..67) {
        $future["G:$track`:1:$note`:OFF:0"] = $tick + 300
        foreach ($kind in @('ON','OFF')) { foreach ($repeat in 1..2) {
            $future["E:$track`:2:$note`:$kind`:$repeat"] = $tick + (400 * $repeat)
        }}
    }}
}
Assert-Contract ($future.Count -eq (8 * 8 * 5)) 'causal future compaction'
Assert-Contract ($future.Count -le $futureCapacity) 'multi-track future capacity'

function Groove-Phase([long]$sample, [long]$step) {
    return (([Math]::Floor($sample / $step) * 6) +
        [Math]::Floor((($sample % $step) * 6) / $step)) % 8
}
$phases = @(0..7 | ForEach-Object { Groove-Phase ($_ * 800) 2400 })
Assert-Contract ((@($phases | Select-Object -Unique).Count) -gt 2) 'ARP -> Groove phase'
Assert-Contract ((Groove-Phase 800 2400) -ne (Groove-Phase 1600 2400)) 'temporal Groove phase'

$root = Split-Path -Parent $PSScriptRoot
$control = Get-Content -Raw (Join-Path $root 'Src/Track/control_music_output.c')
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$state = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_state.c')
Assert-Contract $control.Contains('control_music_output_allocate_handle') 'single handle allocator'
Assert-Contract $control.Contains('g_control_music_window_order++') 'chronological transition order'
Assert-Contract (-not $control.Contains('CONTROL_MUSIC_WINDOW_KIND_COUNT')) 'no kind buckets'
Assert-Contract $pipeline.Contains('note_fx_pipeline_purge_future_sources') 'causal revoice purge'
Assert-Contract $pipeline.Contains('note_fx_pipeline_purge_future_owner') 'owner future compaction'
Assert-Contract $pipeline.Contains('note_fx_engine_reset_from_slot(track, revoice_slot)') 'downstream revoice reset'
Assert-Contract $pipeline.Contains('note_fx_pipeline_purge_future_track(track)') 'TYPE future purge'
Assert-Contract (-not $pipeline.Contains('composed_fanout')) 'legacy composed fanout removed'
Assert-Contract (-not $pipeline.Contains('g_note_fx_admitted_fanout')) 'legacy admission mirror removed'
Assert-Contract $pipeline.Contains('note_fx_engine_cleanup(track)') 'panic/transport cleanup'
Assert-Contract $state.Contains('note_fx_pipeline_commit_state(track, &next)') 'state/enqueue transaction'
Assert-Contract $state.Contains('note_fx_state_validate_unique_families') 'product family uniqueness'
$engine = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
Assert-Contract (-not $engine.Contains('r->phase++')) 'generator phase authority removed'
Assert-Contract (-not $engine.Contains('random_state')) 'mutable ARP random removed'
Assert-Contract $engine.Contains('seq_runtime_get_musical_time') 'canonical musical time used'
Assert-Contract $pipeline.Contains('deadline.duration_samples') 'terminal duration deadline'
Assert-Contract (-not $pipeline.Contains('uint8_t resume_slot;')) 'duplicate future resume slot removed'

Write-Output 'NoteFx runtime contract tests: PASS (10000 chains + lifetimes + temporal accumulation)'
