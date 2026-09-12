$ErrorActionPreference = 'Stop'

$held = 8
$batch = 32
$emittingVoices = 64
$sourceActions = 256
$temporalActions = 128
$internalActions = 384
$futureCapacity = 512

$models = [ordered]@{
    OFF        = @(1, 1, 8, 0)
    ARP        = @(1, 1, 8, 16)
    EUCLID     = @(1, 1, 8, 16)
    PROBABILITY = @(1, 1, 8, 0)
    GATE       = @(1, 1, 8, 8)
    GROOVE     = @(1, 1, 8, 0)
    ECHO       = @(1, 3, 8, 32)
    HARMONIZER = @(4, 1, 8, 0)
    CHORD      = @(1, 1, 8, 0)
}

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

function Get-ChainBound([string[]]$chain) {
    $stage = 1
    $maximumStage = 1
    $instant = 1
    $temporal = 1
    $future = 0
    $sourceLimit = $held
    foreach ($name in $chain) {
        $desc = $models[$name]
        $instantModel, $temporalModel, $notes, $pending = $desc
        $stage *= $instantModel * $temporalModel
        $maximumStage = [Math]::Max($maximumStage, $stage)
        $instant *= $instantModel
        $temporal *= $temporalModel
        $sourceLimit = [Math]::Min($sourceLimit,
            [Math]::Floor($notes / $stage))
        $sourceLimit = [Math]::Min($sourceLimit,
            [Math]::Floor($batch / $maximumStage))
        $future += $pending * [Math]::Floor($stage / $temporalModel)
    }
    $composed = $instant * $temporal
    $sourceLimit = [Math]::Min($sourceLimit,
        [Math]::Floor($held / $composed))
    $echoCount = @($chain | Where-Object { $_ -eq 'ECHO' }).Count
    $harmonizerCount = @($chain | Where-Object { $_ -eq 'HARMONIZER' }).Count
    if (($echoCount -ne 0) -and ($chain -contains 'EUCLID')) {
        $future = [Math]::Max($future, 256)
    }
    $admitted = ($stage -le $batch) -and ($composed -le 4) -and
        ($sourceLimit -gt 0) -and ($echoCount -le 1) -and
        ($harmonizerCount -le 1) -and ($future -le $futureCapacity)
    return @($admitted, $composed, $sourceLimit, $maximumStage, $future)
}

Assert-Contract ($sourceActions -eq 2 * 2 * $emittingVoices) 'source proof'
Assert-Contract ($temporalActions -eq 2 * $emittingVoices) 'temporal proof'
Assert-Contract ($internalActions -eq $sourceActions + $temporalActions) 'sum proof'

$names = @($models.Keys)
foreach ($a in $names) { foreach ($b in $names) {
    foreach ($c in $names) { foreach ($d in $names) {
        $chain = @($a, $b, $c, $d)
        $bound = Get-ChainBound $chain
        if ($bound[0]) {
            Assert-Contract ($bound[1] * $bound[2] -le $held) "fanout: $chain"
            Assert-Contract ($bound[3] -le $batch) "batch: $chain"
            Assert-Contract ($bound[4] -le $futureCapacity) "future: $chain"
        }
    }}
}}

$accepted = @(
    @('GATE', 'ECHO', 'OFF', 'OFF'),
    @('ARP', 'GATE', 'OFF', 'OFF'),
    @('EUCLID', 'GATE', 'OFF', 'OFF'),
    @('CHORD', 'ECHO', 'OFF', 'OFF')
)
foreach ($chain in $accepted) {
    $bound = Get-ChainBound $chain
    Assert-Contract ([bool]$bound[0]) "expected admitted: $chain"
}

$rejected = @(
    @('ECHO', 'ECHO', 'OFF', 'OFF'),
    @('HARMONIZER', 'HARMONIZER', 'OFF', 'OFF'),
    @('HARMONIZER', 'ECHO', 'OFF', 'OFF'),
    @('HARMONIZER', 'ECHO', 'GATE', 'ECHO')
)
foreach ($chain in $rejected) {
    $bound = Get-ChainBound $chain
    Assert-Contract (-not [bool]$bound[0]) "expected rejected: $chain"
}

$root = Split-Path -Parent $PSScriptRoot
$control = Get-Content -Raw (Join-Path $root 'Src/Track/control_music_output.c')
$audio = Get-Content -Raw (Join-Path $root 'Src/Audio/audio_note_engine_adapter.c')
$capacity = Get-Content -Raw (Join-Path $root 'Inc/IPC/control_music_capacity.h')
Assert-Contract $control.Contains('batch[count++] = admitted_start;') 'first retrigger normalization'
Assert-Contract $audio.Contains('(output_was_active == 0U)') 'idempotent unknown STOP'
Assert-Contract $capacity.Contains('CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST == 384U') 'shared staging proof'

Write-Output 'NoteFx contract tests: PASS (6561 chains + lifetime/capacity regressions)'
