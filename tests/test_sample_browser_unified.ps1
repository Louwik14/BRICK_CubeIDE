$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$root = Join-Path $PSScriptRoot '..'
$navigation = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_navigation.inc')
$public = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_public.inc')
$render = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_render.inc')
$assets = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/Settings/ui_settings_multi_assets.inc')
$config = Get-Content -Raw (Join-Path $root `
    'Inc/Sampler/sample_page_cache_config.h')
$pool = Get-Content -Raw (Join-Path $root `
    'Inc/Sampler/sample_global_pool.h')

Assert-True ($navigation.Contains('return 2U;') -and
             $navigation.Contains('return "SAMPLE BROWSER";')) `
    'CLASSIC and MULTI still have separate menu entries'
Assert-True ($public.Contains('ui_page_settings_sample_browser_toggle_mode(level);') -and
             $public.Contains('if (encoder == 2U)')) `
    'E3 does not toggle the unified browser mode'
Assert-True ($render.Contains(
    'ui_page_settings_draw_unified_sample_header(UI_SETTINGS_VIEW_SAMPLER);') -and
             $render.Contains(
    'ui_page_settings_draw_unified_sample_header(UI_SETTINGS_VIEW_MULTI_SAMPLE);')) `
    'CLASSIC and MULTI do not share the browser header'
Assert-True ($assets.Contains('"CLASSIC"') -and
             $assets.Contains('"MULTI"') -and
             $assets.Contains('sample_global_pool_get_used_entries()') -and
             $assets.Contains('sample_global_pool_get_entry_capacity()')) `
    'header does not expose CLASSIC | MULTI | loaded sample capacity'
Assert-True ($config.Contains(
    '#define SAMPLE_PAGE_CACHE_TARGET_PAGE_COUNT    (376U)') -and
             $config.Contains(
    '#if (SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES != 36U)') -and
             $config.Contains(
    '#define SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES') -and
             $pool.Contains(
    '(SAMPLE_PAGE_SLOT_POOL_COUNT / SAMPLE_PAGE_MIN_READY_PAGES)')) `
    'the browser capacity no longer derives from the protected pool'
Assert-True (-not $navigation.Contains('return "STREAM";')) `
    'STREAM remains exposed in the Sample Browser menu'

Write-Output 'unified sample browser regressions: PASS'
