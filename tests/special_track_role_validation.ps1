$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$mixPage = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_template_mix.c')
$tonePage = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_template_tone.c')
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Core\track_runtime.c')
$macroFx = Get-Content -Raw (Join-Path $repo 'Src\Audio\fx_master_macro.c')
$toneBackend = Get-Content -Raw (Join-Path $repo 'Src\Param\param_registry_tone_backends.c')
$trackState = Get-Content -Raw (Join-Path $repo 'Src\Core\track_state.c')
$audioTest = Get-Content -Raw (Join-Path $repo 'Src\Core\audio_test_runner.c')
$templateCore = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_template_page.c')
$navigation = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_navigation.c')
$clipboard = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core_clipboard.c')
$pattern = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')
$projectHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\project_v1.h')
$patternBank = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_sd_bank.c')
$kitBank = Get-Content -Raw (Join-Path $repo 'Inc\Storage\kit_sd_bank.h')
$patchBank = Get-Content -Raw (Join-Path $repo 'Inc\Storage\patch_sd_bank.h')

foreach ($globalParam in @(
    'PARAM_MIX_REVERB_MODEL',
    'PARAM_MIX_DELAY_TYPE',
    'PARAM_COMP_MODEL'
)) {
    if ($mixPage.Contains($globalParam)) {
        throw "MIX still exposes global control: $globalParam"
    }
    if (-not $tonePage.Contains($globalParam)) {
        throw "Master surface does not expose global control: $globalParam"
    }
}

foreach ($contract in @(
    'TRACK_TOPOLOGY_ROLE_MASTER',
    'g_ui_template_tone_family_master_reverb_mutable',
    'g_ui_template_tone_family_master_delay_classic',
    'g_ui_template_tone_family_master_comp_off'
)) {
    if (-not $tonePage.Contains($contract)) {
        throw "Missing Master UI ownership contract: $contract"
    }
}

if (-not $macroFx.Contains('track_topology_find_special(TRACK_TOPOLOGY_ROLE_FX')) {
    throw 'Macro FX DSP does not resolve the fixed FX Special Track'
}
if (-not $toneBackend.Contains('track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX)')) {
    throw 'Macro FX parameter backend is not restricted to the FX Special Track'
}
if (-not $runtime.Contains('topology.role == (uint8_t)TRACK_TOPOLOGY_ROLE_MASTER')) {
    throw 'Runtime UI ensembles do not distinguish Master from FX'
}
if (-not $trackState.Contains('case TRACK_TOPOLOGY_ROLE_LOOPER:')) {
    throw 'Fixed Looper adapter is missing'
}
if (-not $trackState.Contains('case TRACK_TOPOLOGY_ROLE_INPUT:')) {
    throw 'Fixed Input adapters are missing'
}
if ($trackState.Contains('UI_TRACK_FAMILY_MASTER') -or
    $trackState.Contains('UI_TRACK_TYPE_MASTER_FX') -or
    $runtime.Contains('TRACK_RUNTIME_FAMILY_MASTER') -or
    $runtime.Contains('TRACK_RUNTIME_TYPE_MASTER_FX')) {
    throw 'Shared Master/FX adapter remains'
}
if (-not ($runtime.Contains('TRACK_RUNTIME_FAMILY_SPECIAL_MASTER') -and
          $runtime.Contains('TRACK_RUNTIME_FAMILY_SPECIAL_FX') -and
          $runtime.Contains('TRACK_RUNTIME_TYPE_SPECIAL_MASTER') -and
          $runtime.Contains('TRACK_RUNTIME_TYPE_SPECIAL_FX'))) {
    throw 'Master and FX do not have distinct topology-derived runtime identities'
}
if ($audioTest.Contains('set_track_param(UI_TRACK_COUNT - 1U, PARAM_MASTER_FX')) {
    throw 'Audio validation still assumes FX is the final storage slot'
}

foreach ($resolverContract in @(
    'ui_template_family_resolve_effective_for_track',
    'ui_template_family_resolve_effective_active_track',
    'ui_template_family_get_effective_scope_count',
    'ui_page_template_tone_resolve_for_track'
)) {
    if (-not $templateCore.Contains($resolverContract)) {
        throw "Missing shared effective template resolver contract: $resolverContract"
    }
}
if ($navigation -match 'UI_PAGE_TEMPLATE_TONE:[\s\S]{0,300}ui_template_family_resolve_active_track\(UI_TEMPLATE_FAMILY_TONE\)') {
    throw 'Navigation still bypasses the effective role-aware TONE resolver'
}
if (-not $clipboard.Contains('ui_template_family_get_effective_scope_count') -or
    -not $clipboard.Contains('ui_template_family_resolve_effective_for_track')) {
    throw 'Clipboard does not reuse the effective role-aware template resolver'
}

$fxMask = [regex]::Match($runtime, 'if \(topology\.role == \(uint8_t\)TRACK_TOPOLOGY_ROLE_FX\)[\s\S]*?return mask;')
if (-not $fxMask.Success -or $fxMask.Value.Contains('TRACK_RUNTIME_UI_ENSEMBLE_MOD')) {
    throw 'FX still exposes the obsolete MOD ensemble'
}
foreach ($role in @('MASTER', 'FX')) {
    $roleBlock = [regex]::Match($runtime, "if \(topology\.role == \(uint8_t\)TRACK_TOPOLOGY_ROLE_$role\)[\s\S]*?return mask;")
    if (-not $roleBlock.Success) { throw "Missing runtime ensemble branch for $role" }
}
if (-not $runtime.Contains('mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_TONE);')) {
    throw 'TONE is not available before the Master/FX role split'
}

$macroParams = [regex]::Matches($tonePage, 'PARAM_MACRO_FX[1-4]_(TYPE|LEVEL|A|B)') |
    ForEach-Object Value | Sort-Object -Unique
if ($macroParams.Count -ne 16) {
    throw "FX TONE does not expose exactly 16 unique MacroFX parameters: $($macroParams.Count)"
}
if (-not $clipboard.Contains('rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED') -or
    -not $clipboard.Contains('param_set(target, value)') -or
    -not $clipboard.Contains('param_registry_apply_track_value(target, track, value)')) {
    throw 'Clipboard does not separate global Master writes from track-aware FX writes'
}
if (-not $clipboard.Contains('undo_v2_begin_snapshot_transaction') -or
    -not $clipboard.Contains('undo_v2_capture_snapshot_before') -or
    -not $clipboard.Contains('undo_v2_capture_snapshot_after')) {
    throw 'Clipboard clear/paste is not covered by snapshot undo/redo'
}

foreach ($reverbParam in @(
    'PARAM_MIX_REVERB_WET',
    'PARAM_MIX_REVERB_SIZE',
    'PARAM_MIX_REVERB_DECAY',
    'PARAM_MIX_REVERB_PRED'
)) {
    if (-not $pattern.Contains("case ${reverbParam}:")) {
        throw "Master reverb parameter is not captured as a Pattern global: $reverbParam"
    }
    if (-not $runtime.Contains("case ${reverbParam}:")) {
        throw "Master reverb parameter is not classified global by runtime: $reverbParam"
    }
}
if (-not $pattern.Contains('out_pattern->globals.global_values[id] = param_get(id)') -or
    -not $pattern.Contains('param_set(id, ctx->pattern->globals.global_values[id])')) {
    throw 'Pattern global capture/restore does not preserve param_get/param_set DSP reapply'
}
if (-not ($patternBank.Contains('#define PATTERN_VERSION    5U') -and
          $projectHeader.Contains('#define PROJECT_V1_FILE_VERSION    5U') -and
          $kitBank.Contains('#define KIT_SD_FILE_VERSION 3U') -and
          $patchBank.Contains('#define PATCH_SD_FILE_VERSION 3U'))) {
    throw 'Persistence versions changed unexpectedly'
}

'special_track_role_validation=PASS matrix=master/fx:cfg+tone-only resolver=role-aware clipboard=master-global/fx-track macrofx=16 reverb-globals=4 formats=v5,v5,v3,v3'
