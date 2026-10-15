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
$patchPreview = Get-Content -Raw (Join-Path $root 'Src/Storage/patch_preview.c')
$pool = Get-Content -Raw (Join-Path $root 'Src/Sampler/wavetable_pool.c')
$async = Get-Content -Raw (Join-Path $root `
    'Src/Sampler/Wavetable/wavetable_storage_async.inc')
$workspace = Get-Content -Raw (Join-Path $root `
    'Inc/Storage/persistence_workspace.h')
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
Assert-True ($control.Contains('wavetable_pool_find_by_path(canonical_path, &persistent_slot)') -and
             $patchPreview.Contains(
                'project_control_find_asset(PERSIST_ASSET_WAVETABLE,')) `
    'Project/Patch path compatibility no longer resolves through the global catalogue'
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
             $render.Contains('"%s *"')) `
    'SD catalogue does not mark globally imported wavetables'

Assert-True ((-not $workspace.Contains('PERSISTENCE_WORKSPACE_WAVETABLE_PREPARE')) -and
             $pool.Contains('STORAGE_SCRATCH_SDRAM static float') -and
             $pool.Contains('g_wavetable_prepare_base[') -and
             $pool.Contains('g_wavetable_prepare_mipmap[')) `
    'Unsafe persistence-workspace aliasing was introduced for Wavetable preparation'
Assert-True ((-not $async.Contains('persistence_workspace_acquire_wavetable_prepare')) -and
             $async.Contains('job->state = WAVETABLE_LOAD_DONE')) `
    'Project Load could be blocked by a nested Wavetable workspace reservation'

Write-Output 'wavetable Flash browser/workspace regressions: PASS'
