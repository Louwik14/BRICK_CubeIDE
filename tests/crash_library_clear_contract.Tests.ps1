$ErrorActionPreference = 'Stop'

function Invoke-ClearModel {
    param([bool]$Unlock, [bool]$EraseA, [bool]$EraseB, [bool]$Verify)
    $state = [ordered]@{ Magic = $true; Capsules = $true; Status = 0 }
    if (-not $Unlock) { $state.Status = 0xC101; return $state }
    if (-not $EraseA) { $state.Status = 0xC102; return $state }
    if (-not $EraseB) { $state.Status = 0xC103; return $state }
    if (-not $Verify) { $state.Status = 0xC104; return $state }
    $state.Magic = $false
    $state.Capsules = $false
    $state.Status = 0xC100
    return $state
}

function Assert-ClearCase {
    param($State, [bool]$Magic, [bool]$Capsules, [int]$Status, [string]$Name)
    if (($State.Magic -ne $Magic) -or ($State.Capsules -ne $Capsules) -or
        ($State.Status -ne $Status)) { throw "Crash clear contract failed: $Name" }
}

Assert-ClearCase (Invoke-ClearModel $true $true $true $true) $false $false 0xC100 'success'
Assert-ClearCase (Invoke-ClearModel $false $true $true $true) $true $true 0xC101 'unlock'
Assert-ClearCase (Invoke-ClearModel $true $false $true $true) $true $true 0xC102 'erase A'
Assert-ClearCase (Invoke-ClearModel $true $true $false $true) $true $true 0xC103 'erase B'
Assert-ClearCase (Invoke-ClearModel $true $true $true $false) $true $true 0xC104 'verify'

$source = Get-Content -Raw (Join-Path $PSScriptRoot '..\Src\Platform\crash_library.c')
$unlock = $source.IndexOf('if (HAL_FLASH_Unlock() != HAL_OK)')
$eraseA = $source.IndexOf('erase_sector(FLASH_SECTOR_4, &sector_error)')
$eraseB = $source.IndexOf('erase_sector(FLASH_SECTOR_5, &sector_error)')
$verify = $source.IndexOf('flash_region_is_erased(CRASH_LIBRARY_BASE')
$consume = $source.IndexOf('memset(&g_crash_gdb_view, 0, sizeof(g_crash_gdb_view))')
if (($unlock -lt 0) -or -not ($unlock -lt $eraseA -and $eraseA -lt $eraseB -and
    $eraseB -lt $verify -and $verify -lt $consume)) {
    throw 'Production clear ordering contract is not satisfied'
}

Write-Output 'Crash Library CLEAR contract: 5/5 cases passed'
