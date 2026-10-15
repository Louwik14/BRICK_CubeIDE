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

Assert-True ($workspace.Contains('PERSISTENCE_WORKSPACE_WAVETABLE_PREPARE') -and
             $workspace.Contains('sizeof(persistence_wavetable_prepare_workspace_t) == 98304U')) `
    'The shared workspace lacks an explicit 96 KiB Wavetable reservation'
Assert-True ((-not $pool.Contains('STORAGE_SCRATCH_SDRAM static float')) -and
             $pool.Contains('persistence_workspace_acquire_wavetable_prepare()')) `
    'Static Wavetable preparation buffers remain allocated'
Assert-True ($async.Contains('wavetable_prepare_workspace_acquire()') -and
             $async.Contains('wavetable_prepare_workspace_release()') -and
             $async.Contains('job->state == WAVETABLE_LOAD_DONE')) `
    'The cooperative Wavetable job does not retain and release workspace ownership'

Write-Output 'wavetable Flash browser/workspace regressions: PASS'
