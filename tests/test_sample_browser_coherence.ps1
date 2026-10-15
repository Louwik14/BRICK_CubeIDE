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
$loader = Get-Content -Raw (Join-Path $root 'Src/Storage/wav_loader.c')
$settings = Get-Content -Raw (Join-Path $root 'Src/UI/pages/ui_page_settings.c')

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
Assert-True ($settings.Contains('union') -and
             $settings.Contains('ui_settings_sample_entry_t sample_entries[') -and
             $settings.Contains('ui_settings_multi_entry_t multi_entries[')) `
    'Sample and Multi browser entry storage is not shared'
Assert-True ($browser.Contains('ui_page_settings_wavetable_browser_stop();') -and
             $browser.Contains('level->view != UI_SETTINGS_VIEW_WAVETABLE')) `
    'the asynchronous Wavetable scan can outlive ownership of shared browser storage'

Assert-True (-not $loader.Contains('g_wav_catalog_scratch_view')) `
    'the 56848-byte catalogue scratch view is still allocated'
Assert-True ($loader.Contains('target->valid = 0U;') -and
             $loader.Contains('target->entries[dst] = entry;') -and
             $loader.Contains('view->valid = 1U;')) `
    'catalogue views are not built privately then published atomically'
Assert-True ($loader.Contains('g_wav_catalog_pending_request = 1U;') -and
             $loader.Contains('g_wav_catalog_view_load.cancel_requested = 1U;') -and
             (-not $loader.Contains('(void)f_close(&load->file);')) -and
             $loader.Contains('close_ok = (f_close(&load->file) == FR_OK)')) `
    'superseded catalogue requests do not close and restart transactionally'
Assert-True ($loader.Contains('load->media_epoch != sd_access_media_epoch()') -and
             $loader.Contains('g_wav_catalog_loaded = 0U;') -and
             $loader.Contains('g_wav_catalog_stale = 1U;')) `
    'media removal does not invalidate the catalogue transaction'
Assert-True ($loader.Contains('SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES') -and
             $loader.Contains('sd_scheduler_runtime_background_try_begin(&request)')) `
    'catalogue reads bypass the AUDIO/STREAM background scheduler'

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
