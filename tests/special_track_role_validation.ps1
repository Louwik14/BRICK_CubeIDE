$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$mixPage = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_template_mix.c')
$tonePage = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_template_tone.c')
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Core\track_runtime.c')
$macroFx = Get-Content -Raw (Join-Path $repo 'Src\Audio\fx_master_macro.c')
$toneBackend = Get-Content -Raw (Join-Path $repo 'Src\Param\param_registry_tone_backends.c')
$trackState = Get-Content -Raw (Join-Path $repo 'Src\Core\track_state.c')
$audioTest = Get-Content -Raw (Join-Path $repo 'Src\Core\audio_test_runner.c')

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

'special_track_role_validation=PASS master=global_fx fx=macro4 shared_adapter=absent convertible_master=absent mix=track_only looper=fixed inputs=fixed'
