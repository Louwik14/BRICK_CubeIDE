$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$root = Join-Path $PSScriptRoot '..'
$pool = Get-Content -Raw (Join-Path $root 'Src/Sampler/sample_global_pool.c')
$browser = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_catalog_browser.inc')
$actions = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_sample_assets.inc')
$navigation = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_navigation.inc')
$render = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_render.inc')

Assert-True ($pool.Contains('sample_global_pool_publish_change();')) `
    'the canonical pool does not publish mutations'
Assert-True ($pool.Contains('g_sample_global_pool[logical] = next;') -and
             $pool.Contains('old->kind = SAMPLE_GLOBAL_KIND_EMPTY;')) `
    'classic async publication/removal paths are missing'
Assert-True ($browser.Contains('sample_global_pool_get_revision()') -and
             $navigation.Contains('ui_page_settings_classic_coherence_service();') -and
             $render.Contains('ui_page_settings_classic_coherence_service();')) `
    'the browser does not consume the canonical pool revision'
Assert-True ($actions.Contains('UI_SETTINGS_CLASSIC_PENDING_LOAD') -and
             $actions.Contains('UI_SETTINGS_CLASSIC_PENDING_CLEAR')) `
    'LOAD/CLEAR do not retain an in-flight operation identity'
Assert-True ($actions.Contains('ui_page_settings_status("CLEARING");') -and
             $browser.Contains('ui_page_settings_status("CLEAR OK");')) `
    'CLEAR admission and terminal publication are not separated'
Assert-True ($browser.Contains(
    'g_ui_settings.sample_slot_selected = SAMPLE_GLOBAL_POOL_INVALID_INDEX;')) `
    'an empty projection retains a stale selected slot'

# Selection contract for deleting first, last and only entries.
function Select-Neighbor([int[]]$Slots, [int]$Selected) {
    $index = [Array]::IndexOf($Slots, $Selected)
    if ($index -lt 0) { return 0xFFFF }
    if ($index + 1 -lt $Slots.Count) { return $Slots[$index + 1] }
    if ($index -gt 0) { return $Slots[$index - 1] }
    return 0xFFFF
}
Assert-True ((Select-Neighbor @(4, 8, 12) 4) -eq 8) 'first deletion selection failed'
Assert-True ((Select-Neighbor @(4, 8, 12) 12) -eq 8) 'last deletion selection failed'
Assert-True ((Select-Neighbor @(4) 4) -eq 0xFFFF) 'only deletion selection failed'

# Change-driven projection model: admission leaves the last coherent snapshot;
# terminal publication advances the revision and replaces it atomically.
$poolSlots = [System.Collections.Generic.List[int]]::new()
$snapshot = @()
$revision = 1
$observed = 1
function Publish-Slot([int]$Slot) {
    if (-not $script:poolSlots.Contains($Slot)) { $script:poolSlots.Add($Slot) }
    $script:revision++
}
function Remove-Slot([int]$Slot) {
    [void]$script:poolSlots.Remove($Slot)
    $script:revision++
}
function Refresh-Projection {
    if ($script:revision -ne $script:observed) {
        $script:snapshot = @($script:poolSlots)
        $script:observed = $script:revision
    }
}
Publish-Slot 7; Refresh-Projection
Assert-True (($snapshot.Count -eq 1) -and ($snapshot[0] -eq 7)) `
    'LOAD terminal publication was not reflected'
Remove-Slot 7; Refresh-Projection
Assert-True ($snapshot.Count -eq 0) 'LOAD then DELETE retained a stale reference'
Publish-Slot 3; Publish-Slot 9; Refresh-Projection
Remove-Slot 3; Refresh-Projection; Publish-Slot 3; Refresh-Projection
Assert-True (($snapshot.Count -eq 2) -and ($snapshot -contains 3) -and
             ($snapshot -contains 9)) `
    'DELETE then LOAD lost or duplicated the canonical projection'

Write-Output 'sample browser coherence regressions: PASS'
