$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$root = Join-Path $PSScriptRoot '..'
$actions = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_sample_assets.inc')
$navigation = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_navigation.inc')
$render = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_render.inc')
$control = Get-Content -Raw (Join-Path $root 'Src/Storage/project_control.c')
$product = Get-Content -Raw (Join-Path $root 'Src/Storage/project_product.c')
$patchPreview = Get-Content -Raw (Join-Path $root 'Src/Storage/patch_preview.c')
$pool = Get-Content -Raw (Join-Path $root 'Src/Sampler/wavetable_pool.c')
$async = Get-Content -Raw (Join-Path $root `
    'Src/Sampler/Wavetable/wavetable_storage_async.inc')
$workspace = Get-Content -Raw (Join-Path $root `
    'Inc/Storage/persistence_workspace.h')
$workspaceSource = Get-Content -Raw (Join-Path $root `
    'Src/Storage/persistence_workspace.c')
$poolHeader = Get-Content -Raw (Join-Path $root 'Inc/Sampler/wavetable_pool.h')
$wavetableActions = $actions.Substring(
    $actions.IndexOf('static void ui_page_settings_wavetable_copy_left'),
    $actions.IndexOf('static void ui_page_settings_wavetable_copy_right') -
        $actions.IndexOf('static void ui_page_settings_wavetable_copy_left'))

Assert-True ($control.Contains('wavetable_pool_find_by_path(path,&existing_backend)') -and
             $control.Contains('project_control_register_wavetable_runtime(')) `
    'Project control does not reuse the global Flash catalogue before import'
Assert-True (-not $control.Contains(
    'for (uint16_t slot = 0U; slot < WAVETABLE_POOL_MAX_SLOTS; ++slot)')) `
    'Global Flash records are still injected into every current project bank at boot'
Assert-True ($control.Contains('project_control_resolve_wavetable_at(') -and
             $control.Contains('wavetable_pool_find_by_path(canonical_path, &backend)') -and
             $patchPreview.Contains(
                'project_control_find_asset(PERSIST_ASSET_WAVETABLE,')) `
    'Project/Patch path compatibility no longer resolves through the global catalogue'
Assert-True ((-not $product.Contains('PROJECT_LOAD_INSTALL_WAIT_WAVETABLE')) -and
             (-not $product.Contains('wavetable_pool_import_async_take_result')) -and
             (-not $product.Contains('project_control_complete_wavetable_runtime')) -and
             (-not $product.Contains('wavetable_slots <= WAVETABLE_POOL_MAX_SLOTS'))) `
    'Project restore still owns an implicit Wavetable import path or physical-slot quota'
$restoreResolver = $control.Substring(
    $control.IndexOf('static project_control_asset_result_t apply_bank_asset'),
    $control.IndexOf('project_control_asset_result_t project_control_prepare_asset') -
        $control.IndexOf('static project_control_asset_result_t apply_bank_asset'))
Assert-True ((-not $restoreResolver.Contains('wavetable_pool_import_async_begin')) -and
             (-not $restoreResolver.Contains('wavetable_pool_source_crc32_file')) -and
             $restoreResolver.Contains('project_control_resolve_wavetable_at')) `
    'Restore-time Wavetable resolution can still read SD or begin an import'
Assert-True ($wavetableActions.Contains('wavetable_pool_find_by_path(entry->path, &existing_backend)') -and
             (-not $wavetableActions.Contains(
                'sample_global_pool_get_used_entries() >= sample_global_pool_get_entry_capacity()'))) `
    'Wavetable LOAD still depends on the Sample pool or checks capacity before deduplication'
Assert-True ($actions.Contains('project_control_remove_wavetable(deleted_logical)') -and
             (-not $actions.Contains('wavetable_pool_clear(backend_slot);'))) `
    'Removing a project-bank reference still tombstones the global Flash record'
Assert-True ($navigation.Contains('FLASH PROTECTED') -and
             $navigation.Contains('ui_page_settings_wavetable_browser_refresh();')) `
    'Flash deletion policy or two-column refresh is missing'
Assert-True ($render.Contains('wavetable_pool_find_by_path(entry->path,') -and
             $render.Contains('"%s *"') -and
             $render.Contains('"UNAVAILABLE"')) `
    'SD catalogue does not mark globally imported wavetables'

Assert-True ($workspace.Contains('PERSISTENCE_WORKSPACE_WAVETABLE_IMPORT') -and
             $workspace.Contains('persistence_wavetable_import_workspace_t') -and
             $workspace.Contains('== 98304U') -and
             $workspaceSource.Contains('persistence_workspace_acquire_wavetable_import')) `
    'The 96 KiB Wavetable scratch is not covered by the shared workspace owner'
Assert-True ((-not $pool.Contains('STORAGE_SCRATCH_SDRAM static float')) -and
             $pool.Contains('static float *g_wavetable_prepare_base') -and
             $async.Contains('persistence_workspace_acquire_wavetable_import()') -and
             $async.Contains('wavetable_import_workspace_release();')) `
    'Wavetable preparation still owns permanent scratch or lacks lifetime exclusivity'
Assert-True ($poolHeader.Contains('wavetable_pool_import_async_begin') -and
             (-not $poolHeader.Contains('wavetable_pool_load_file(')) -and
             (-not $poolHeader.Contains('wavetable_pool_load_wav('))) `
    'The pool API still exposes a non-Browser Wavetable import entry point'

Write-Output 'wavetable Flash browser/workspace regressions: PASS'
