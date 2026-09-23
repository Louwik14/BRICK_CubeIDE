$ErrorActionPreference = 'Stop'

$held = 8
$batch = 32
$futureCapacity = 320
$models = @('OFF','ARP','EUCLID','PROBABILITY','GATE','VOICER','SCALER')

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

function Test-ProductChain([string[]]$chain) {
    $families = @{}
    foreach ($name in $chain) {
        if ($name -eq 'OFF') { continue }
        $family = $name
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
        $candidates = if ($name -eq 'VOICER') { 4 * $admitted } else { $admitted }
        $maximumCandidates = [Math]::Max($maximumCandidates, $candidates)
        $admitted = [Math]::Min($held, $candidates)
    }
    $futurePerTrack = 5 * $held
    return @(($maximumCandidates -le $batch) -and ($admitted -le $held),
        $admitted, $held, $maximumCandidates, $futurePerTrack)
}

foreach ($a in $models) { foreach ($b in $models) {
    foreach ($c in $models) {
        $bound = Get-ChainBound @($a,$b,$c)
        if ($bound[0]) {
            Assert-Contract ($bound[1] -le $held) "admission: $a $b $c"
            Assert-Contract ($bound[3] -le $batch) "batch: $a $b $c"
            Assert-Contract (8 * $bound[4] -le $futureCapacity) "future: $a $b $c"
        }
    }
}}

foreach ($scenario in @(
    @('VOICER','ARP','OFF'),
    @('ARP','VOICER','OFF'),
    @('GATE','ARP','OFF'),
    @('EUCLID','VOICER','GATE'),
    @('VOICER','EUCLID','GATE'),
    @('GATE','VOICER','SCALER')
)) {
    $bound = Get-ChainBound $scenario
    Assert-Contract $bound[0] "required chain rejected: $($scenario -join ' -> ')"
}
Assert-Contract (-not (Test-ProductChain @('ARP','ARP','OFF'))) 'ARP family duplicated'
Assert-Contract (Test-ProductChain @('VOICER','GATE','SCALER')) 'three distinct FX rejected by product rule'

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
    }}
}
Assert-Contract ($future.Count -eq (8 * 8)) 'causal future compaction'
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
Assert-Contract $pipeline.Contains('note_fx_pipeline_purge_future_dependency') 'causal dependency compaction'
Assert-Contract $pipeline.Contains('note_fx_engine_forget_causal_sources_from_slot') 'local causal revoice'
Assert-Contract (-not $pipeline.Contains('note_fx_engine_reset_from_slot')) 'global slot reset removed'
Assert-Contract $pipeline.Contains('note_fx_pipeline_purge_future_track(track)') 'explicit track reset purge'
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

$gccCandidates = @(
    'C:\msys64\ucrt64\bin\gcc.exe',
    'C:\msys64\mingw64\bin\gcc.exe'
)
$gcc = $gccCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
Assert-Contract ($null -ne $gcc) 'native C compiler unavailable'
$runtimeExe = Join-Path ([System.IO.Path]::GetTempPath()) 'brick_note_fx_runtime.exe'
$savedPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $gcc);$savedPath"
    & $gcc -std=c11 -Wall -Wextra -Werror "-I$((Join-Path $root 'Inc'))" `
        (Join-Path $root 'tools/test_note_fx_runtime.c') `
        (Join-Path $root 'Src/NoteFx/note_fx_engine.c') `
        (Join-Path $root 'Src/NoteFx/note_fx_euclid.c') `
        -o $runtimeExe
    Assert-Contract ($LASTEXITCODE -eq 0) 'native NoteFx runtime build failed'
    & $runtimeExe
    Assert-Contract ($LASTEXITCODE -eq 0) 'native NoteFx runtime assertions failed'
} finally {
    $env:PATH = $savedPath
    Remove-Item -LiteralPath $runtimeExe -Force -ErrorAction SilentlyContinue
}

Write-Output 'NoteFx runtime contract tests: PASS (native C + 10000 chains + lifetimes + temporal accumulation)'
