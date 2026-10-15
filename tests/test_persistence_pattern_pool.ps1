$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function New-CompactLane([int[]]$Counts, [int]$PlayCount = 0) {
    if ($Counts.Count -gt 64 -or $PlayCount -lt 0 -or $PlayCount -gt 4) {
        throw 'invalid lane geometry'
    }
    $lane = [ordered]@{ steps = @(); locks = @(); lock_count = 0 }
    for ($step = 0; $step -lt 64; ++$step) {
        $count = if ($step -lt $Counts.Count) { $Counts[$step] } else { 0 }
        if ($count -lt 0 -or $count -gt 32 -or
            ($lane.lock_count + $count) -gt 512) { throw 'P-lock capacity exceeded' }
        $lane.steps += [ordered]@{
            lock_first = $lane.lock_count
            lock_count = $count
            play_count = $PlayCount
        }
        for ($index = 0; $index -lt $count; ++$index) {
            $lane.locks += [ordered]@{
                parameter = (($step + 1) * 1000 + $index)
                flags = (($step + $index) -band 255)
                value = (($step * 32) + $index)
            }
        }
        $lane.lock_count += $count
    }
    return $lane
}

function Assert-Lane($Lane) {
    Assert-True ($Lane.steps.Count -eq 64) 'lane does not contain 64 steps'
    Assert-True ($Lane.lock_count -le 512) 'lane exceeds 512 P-locks'
    Assert-True ($Lane.locks.Count -eq $Lane.lock_count) 'pool count mismatch'
    $cursor = 0
    foreach ($step in $Lane.steps) {
        Assert-True ($step.lock_count -le 32) 'step exceeds 32 P-locks'
        Assert-True ($step.play_count -le 4) 'step exceeds four PLAY notes'
        Assert-True ($step.lock_first -eq $cursor) 'pool ranges overlap or contain gaps'
        Assert-True (($step.lock_first + $step.lock_count) -le $Lane.lock_count) `
            'step P-lock range is out of bounds'
        $cursor += $step.lock_count
    }
    Assert-True ($cursor -eq $Lane.lock_count) 'pool terminal count mismatch'
}

function Copy-ThroughWire($Lane) {
    $counts = @($Lane.steps | ForEach-Object { [int]$_.lock_count })
    $copy = New-CompactLane $counts ([int]$Lane.steps[0].play_count)
    for ($index = 0; $index -lt $Lane.lock_count; ++$index) {
        $copy.locks[$index].parameter = $Lane.locks[$index].parameter
        $copy.locks[$index].flags = $Lane.locks[$index].flags
        $copy.locks[$index].value = $Lane.locks[$index].value
    }
    return $copy
}

$root = Join-Path $PSScriptRoot '..'
$model = Get-Content -Raw (Join-Path $root 'Inc/Storage/persistent_control_model.h')
$codecHeader = Get-Content -Raw (Join-Path $root 'Inc/Storage/persistent_control_codec.h')
$codec = Get-Content -Raw (Join-Path $root 'Src/Storage/persistent_control_codec.c')
$control = Get-Content -Raw (Join-Path $root 'Src/Storage/persistent_pattern_control.c')
$workspace = Get-Content -Raw (Join-Path $root 'Inc/Storage/persistence_workspace.h')

Assert-True ($model.Contains('#define PERSIST_CONTROL_PLAY_ITEM_COUNT            4U')) `
    'persistent PLAY capacity is not four'
Assert-True ($model.Contains('persist_control_step_lock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK]')) `
    'persistent lane P-lock pool is missing'
Assert-True ($model.Contains('sizeof(persist_control_step_lock_t) == 12U')) `
    'aligned 12-byte P-lock ABI proof is missing'
Assert-True (-not $model.Contains('persist_control_step_lock_t locks[PERSIST_CONTROL_STEP_LOCK_COUNT]')) `
    'dense per-step P-lock storage remains'
Assert-True ($codecHeader.Contains('#define PERSIST_CODEC_VERSION 21U')) `
    'persistence codec version was not advanced'
Assert-True ($codec.Contains('st->lock_first = lock_cursor') -and
    $codec.Contains('lock_cursor == sequence->lock_count')) `
    'codec does not rebuild and validate canonical pool ranges'
Assert-True ($control.Contains('step->lock_first=sequence->lock_count') -and
    $control.Contains('&x->sequence.locks[st->lock_first+i]')) `
    'CONTROL capture/apply does not use the lane pool'
Assert-True ($workspace.Contains('PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY (152U)') -and
    $workspace.Contains('sizeof(persistence_pattern_io_workspace_t) == 261312U') -and
    $workspace.Contains('sizeof(persistence_project_restore_workspace_t) == 268544U')) `
    'workspace capacity proofs are missing'

$irregular = @()
for ($pair = 0; $pair -lt 15; ++$pair) {
    $irregular += (32 - $pair)
    $irregular += (1 + $pair)
}
$irregular += 17

foreach ($case in @(
    (New-CompactLane @()),
    (New-CompactLane @(1)),
    (New-CompactLane @(32)),
    (New-CompactLane $irregular 4)
)) {
    Assert-Lane $case
    $roundTrip = Copy-ThroughWire $case
    Assert-Lane $roundTrip
    Assert-True (($case | ConvertTo-Json -Depth 6 -Compress) -eq
        ($roundTrip | ConvertTo-Json -Depth 6 -Compress)) `
        'capture/save/load round-trip changed the pool or step links'
}
Assert-True ((($irregular | Measure-Object -Sum).Sum) -eq 512) `
    'irregular test does not cover the full 512-P-lock lane pool'

$overflowRejected = $false
try { $null = New-CompactLane @(33) } catch { $overflowRejected = $true }
Assert-True $overflowRejected '33 P-locks on one step were accepted'
$overflowRejected = $false
try { $null = New-CompactLane (@(32) * 17) } catch { $overflowRejected = $true }
Assert-True $overflowRejected 'more than 512 P-locks on one lane were accepted'

$corrupt = New-CompactLane @(1, 1)
$corrupt.steps[1].lock_first = 0
$corruptRejected = $false
try { Assert-Lane $corrupt } catch { $corruptRejected = $true }
Assert-True $corruptRejected 'overlapping P-lock ranges were accepted'

Write-Output 'persistence compact Pattern pool regressions: PASS'
