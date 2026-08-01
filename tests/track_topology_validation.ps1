$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command arm-none-eabi-gcc -ErrorAction Stop).Source
$include = Join-Path $repo 'Inc'
$source = Join-Path $repo 'Src\Core\track_topology.c'

foreach ($variant in @('BRICK6_VARIANT_LOWCOST', 'BRICK6_VARIANT_PREMIUM')) {
    & $compiler '-std=c11' '-Wall' '-Werror' '-fsyntax-only' "-D$variant" "-I$include" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Topology compile failed for $variant"
    }
}

$header = Get-Content -Raw (Join-Path $include 'Core\track_topology.h')
$implementation = Get-Content -Raw $source
$trackState = Get-Content -Raw (Join-Path $repo 'Src\Core\track_state.c')
$trackRuntime = Get-Content -Raw (Join-Path $repo 'Src\Core\track_runtime.c')
$scheduler = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_play_scheduler.c')
$uiCore = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core.c')
$hallFlow = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_hall_mode_flow.c')
$cfgPage = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_template_cfg.c')
$uiHeader = Get-Content -Raw (Join-Path $include 'UI\ui_core.h')
$catalog = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_track_catalog.c')
$mute = Get-Content -Raw (Join-Path $repo 'Src\Core\track_mute.c')
$macroFx = Get-Content -Raw (Join-Path $repo 'Src\Audio\fx_master_macro.c')

foreach ($contract in @(
    '#define TRACK_TOPOLOGY_PLAY_TRACK_COUNT 8U',
    '#define TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT 4U',
    '#define TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT 6U',
    '#define TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT 12U',
    '#define TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT 14U',
    '#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 1U',
    '#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 3U',
    '#define TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX 8U',
    '#define TRACK_TOPOLOGY_LOOPER_TRACK_INDEX 9U',
    '#define TRACK_TOPOLOGY_FX_TRACK_INDEX 10U',
    '#define TRACK_TOPOLOGY_MASTER_TRACK_INDEX 11U',
    '#define TRACK_TOPOLOGY_INPUT_SECOND_TRACK_INDEX 12U',
    '#define TRACK_TOPOLOGY_INPUT_THIRD_TRACK_INDEX 13U',
    'TRACK_CAPABILITY_INPUT_RESERVATION'
)) {
    if (-not $header.Contains($contract)) {
        throw "Missing topology contract: $contract"
    }
}

if ($implementation.Contains('INPUT4')) {
    throw 'Topology still declares Input4'
}
if ($uiHeader.Contains('UI_TRACK_FAMILY_INPUT4') -or
    $uiHeader.Contains('UI_TRACK_TYPE_HYBRID') -or
    $uiHeader.Contains('UI_TRACK_FAMILY_MASTER') -or
    $uiHeader.Contains('UI_TRACK_TYPE_MASTER_FX') -or
    $trackRuntime.Contains('TRACK_RUNTIME_TYPE_HYBRID') -or
    (Test-Path (Join-Path $include 'Core\runtime_target.h'))) {
    throw 'Dead Input4/Hybrid/runtime_target topology code remains'
}
if ($catalog -match 'k_sampler_types\[\]\s*=\s*\{[^}]*UI_TRACK_TYPE_LOOPER' -or
    $catalog -match 'k_master_types' -or
    $catalog -match 'k_input_types[\s\S]*UI_TRACK_TYPE_HYBRID') {
    throw 'Fixed Special identities remain exposed as convertible catalog entries'
}
if (-not $implementation.Contains('TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT <= 3U')) {
    throw 'Topology lacks the three-input compile-time bound'
}
if ($implementation -notmatch 'PLAY_DESCRIPTOR\(7U\),\s*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX[\s\S]*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_LOOPER_TRACK_INDEX[\s\S]*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_FX_TRACK_INDEX[\s\S]*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_MASTER_TRACK_INDEX') {
    throw 'Special topology order is not Input1, Looper, FX, Master'
}
if (-not $trackState.Contains('track_state_topology_config(track)')) {
    throw 'Track state does not force topology-owned Special identities'
}
if (-not $trackState.Contains('track_topology_is_play(track) == 0U')) {
    throw 'Track state does not protect Special identity mutations'
}
if (-not $trackRuntime.Contains('TRACK_CAPABILITY_MIDI_FX')) {
    throw 'Runtime UI availability is not topology-gated for MIDI FX'
}
if (-not $header.Contains('TRACK_CAPABILITY_MIDI_FX = (1U << 4)')) {
    throw 'MIDI FX capability value changed'
}
if (-not $header.Contains('TRACK_CAPABILITY_AUTOMATION = (1U << 5)')) {
    throw 'Capability bits after MIDI FX shifted'
}
if (-not $scheduler.Contains('track_runtime_has_capability(track, TRACK_CAPABILITY_NOTES) == 0U')) {
    throw 'Sequencer scheduler does not reject ordinary notes on Special tracks'
}
if (-not $uiCore.Contains('track_topology_is_active(track) == 0U')) {
    throw 'Active-track selection does not honor the product topology'
}
if (-not $hallFlow.Contains('hall >= UI_ACTIVE_TRACK_COUNT')) {
    throw 'Hall selection still exposes storage-only tracks'
}
if (-not $hallFlow.Contains('PLAY TRACK ONLY')) {
    throw 'Special tracks do not reject instrument browsers explicitly'
}
if (-not $cfgPage.Contains('g_ui_template_cfg_special_family')) {
    throw 'Special CFG identity page is missing'
}
if (-not ($mute -match 'TRACK_TOPOLOGY_ROLE_MASTER:\s*return TRACK_MUTE_KIND_NONE' -and
          $mute.Contains('case TRACK_TOPOLOGY_ROLE_LOOPER: return TRACK_MUTE_KIND_LOOPER') -and
          $mute -match 'kind == TRACK_MUTE_KIND_AUDIO[\s\S]*kind == TRACK_MUTE_KIND_MIDI[\s\S]*kind == TRACK_MUTE_KIND_EXTERNAL')) {
    throw 'Master mute or Looper position-preserving mute contract is broken'
}
if (-not $macroFx.Contains('g_fxmm_dry_l[i] + ((left[i] - g_fxmm_dry_l[i]) * g_fxmm_mute_gain)')) {
    throw 'FX mute can cut the direct signal'
}

$audioPathFiles = Get-ChildItem (Join-Path $repo 'Src\Audio'), (Join-Path $repo 'Src\Core'), (Join-Path $repo 'Src\Seq') -Recurse -File -Include *.c,*.cpp
foreach ($file in $audioPathFiles) {
    if ((Get-Content -Raw $file.FullName) -match '\b(malloc|calloc|realloc|free)\s*\(') {
        throw "Dynamic allocation found in realtime-related source: $($file.FullName)"
    }
}

'track_topology_validation=PASS lowcost=Play1-8,Input1,Looper,FX,Master premium=Play1-8,Input1,Looper,FX,Master,Input2,Input3 fixed_special=yes notes_arp=play_only input4_hybrid=absent mute_roles=yes realtime_alloc=none'
