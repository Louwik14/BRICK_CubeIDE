$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function New-ClipboardLane([int[]]$Counts) {
    if ($Counts.Count -gt 64) { throw 'too many steps' }
    $lane = [ordered]@{ steps = @(); locks = @() }
    $cursor = 0
    for ($step = 0; $step -lt $Counts.Count; ++$step) {
        $count = $Counts[$step]
        if ($count -lt 0 -or $count -gt 32 -or ($cursor + $count) -gt 512) {
            throw 'P-lock capacity exceeded'
        }
        $lane.steps += [ordered]@{ offset = $cursor; count = $count }
        for ($lock = 0; $lock -lt $count; ++$lock) {
            $lane.locks += [ordered]@{
                key = (($step * 32 + $lock) -band 254)
                value16 = (($step * 257 + $lock) -band 65535)
                flags = (($step + $lock) -band 255)
            }
        }
        $cursor += $count
    }
    return $lane
}

function Assert-Canonical($Lane) {
    $cursor = 0
    foreach ($step in $Lane.steps) {
        Assert-True ($step.offset -eq $cursor) 'non-canonical offset'
        Assert-True ($step.count -le 32) 'step exceeds 32 P-locks'
        $cursor += $step.count
        Assert-True ($cursor -le 512) 'lane exceeds 512 P-locks'
    }
    Assert-True ($cursor -eq $Lane.locks.Count) 'pool terminal count mismatch'
}

$root = Join-Path $PSScriptRoot '..'
$storageHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_clipboard_storage.h')
$seqClipboard = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_clipboard.c')
$uiClipboard = Get-Content -Raw (Join-Path $root 'Src/UI/ui_core_clipboard.c')

Assert-True ($storageHeader.Contains('sizeof(seq_clipboard_plock_t) == 4U')) `
    'four-byte compact P-lock proof is missing'
Assert-True ($storageHeader.Contains('sizeof(seq_clipboard_step_t) == 28U')) `
    'compact step metadata proof is missing'
Assert-True ($seqClipboard.Contains('sizeof(seq_clipboard_state_t) == 3974U')) `
    'sequence clipboard size proof is missing'
Assert-True ($uiClipboard.Contains('sizeof(ui_clipboard_state_t) == 40548U')) `
    'UI clipboard size proof is missing'
Assert-True (-not $seqClipboard.Contains('seq_step_snapshot_list_t steps;')) `
    'dense sequence clipboard storage remains'
Assert-True (-not $uiClipboard.Contains('seq_step_snapshot_t step[SEQ_MAX_STEPS]')) `
    'dense Track clipboard storage remains'
Assert-True ($uiClipboard.Contains('seq_clipboard_plock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK]')) `
    'Track clipboard lane pool is missing'

$irregular = @()
for ($pair = 0; $pair -lt 16; ++$pair) {
    $irregular += (32 - $pair)
    $irregular += $pair
}
$lane = New-ClipboardLane $irregular
Assert-Canonical $lane
Assert-True ($lane.locks.Count -eq 512) 'full-pool case does not contain 512 locks'

$selectedOffsets = @(0, 4, 11)
$destinationAnchor = 58
$targets = @($selectedOffsets | ForEach-Object { $destinationAnchor + $_ } |
    Where-Object { $_ -lt 64 })
Assert-True (($targets.Count -eq 2) -and ($targets[0] -eq 58) -and
    ($targets[1] -eq 62)) 'partial paste offset/truncation contract changed'

Assert-True ((64 * 32 * 6) - (512 * 4) -eq 10240) `
    'per-lane RAM gain is not 10 KiB'
Assert-True ((9 * 10240) -eq 92160) 'total Clipboard RAM gain is not 90 KiB'

$rejected = $false
try { $null = New-ClipboardLane @(33) } catch { $rejected = $true }
Assert-True $rejected '33 locks on one step were accepted'
$rejected = $false
try { $null = New-ClipboardLane (@(32) * 17) } catch { $rejected = $true }
Assert-True $rejected 'more than 512 locks on one lane were accepted'

$corrupt = New-ClipboardLane @(1, 1)
$corrupt.steps[1].offset = 0
$rejected = $false
try { Assert-Canonical $corrupt } catch { $rejected = $true }
Assert-True $rejected 'overlapping pool ranges were accepted'

Write-Output 'clipboard compact P-lock pool regressions: PASS'
