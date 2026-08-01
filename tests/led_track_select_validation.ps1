$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$led = Get-Content -Raw (Join-Path $repo 'Drivers\Drv_app\Src\led_rgb.c')
$topology = Get-Content -Raw (Join-Path $repo 'Src\Core\track_topology.c')
$header = Get-Content -Raw (Join-Path $repo 'Inc\Core\track_topology.h')

if (-not $led.Contains('#include "Core/track_topology.h"')) {
    throw 'Track-select LED scene does not include topology authority'
}

foreach ($contract in @(
    '#define LED_FIXED_VIOLET_R        LED_FIXED_HALF_BRIGHTNESS',
    '#define LED_FIXED_VIOLET_G        0U',
    '#define LED_FIXED_VIOLET_B        LED_FIXED_HALF_BRIGHTNESS',
    'if (track_topology_is_special(hall) != 0U)',
    'else if (ui_get_track_family(hall) != UI_TRACK_FAMILY_OFF)',
    'if (hall < UI_ACTIVE_TRACK_COUNT)'
)) {
    if (-not $led.Contains($contract)) {
        throw "Missing track-select LED contract: $contract"
    }
}

$scene = [regex]::Match($led, 'static void led_apply_track_select_hall_scene\(uint8_t hall\)[\s\S]*?\n}\r?\n\r?\nstatic uint8_t led_apply_mute_hall_scene')
if (-not $scene.Success) {
    throw 'Track-select LED scene could not be isolated'
}
$sceneText = $scene.Value
$specialStart = $sceneText.IndexOf('if (track_topology_is_special(hall) != 0U)')
$familyStart = $sceneText.IndexOf('ui_get_track_family(hall)')
$activeStart = $sceneText.IndexOf('if (hall == ui_get_active_track())')
if (($specialStart -lt 0) -or ($familyStart -lt 0) -or ($activeStart -lt 0) -or
    ($specialStart -ge $familyStart) -or ($activeStart -le $specialStart)) {
    throw 'Special classification or active-track precedence is not topology-first'
}

$specialBlock = [regex]::Match($sceneText, 'if \(track_topology_is_special\(hall\) != 0U\)[\s\S]*?\n        }\r?\n        else if')
if (-not $specialBlock.Success -or
    -not $specialBlock.Value.Contains('LED_FIXED_VIOLET_R') -or
    -not $specialBlock.Value.Contains('LED_FIXED_VIOLET_G') -or
    -not $specialBlock.Value.Contains('LED_FIXED_VIOLET_B')) {
    throw 'Special tracks do not receive the fixed violet color'
}

$playBlock = $sceneText.Substring($familyStart)
if (-not $playBlock.Contains('LED_FIXED_DARK_BLUE_R') -or
    -not $playBlock.Contains('LED_FIXED_WHITE_R')) {
    throw 'Play track color path or active Play rendering changed'
}
if ($specialBlock.Value.Contains('LED_FIXED_WHITE_R') -or
    $specialBlock.Value.Contains('LED_FIXED_DARK_BLUE_R')) {
    throw 'Special active rendering can be overwritten by Play colors'
}

foreach ($contract in @(
    '#define TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX 8U',
    '#define TRACK_TOPOLOGY_LOOPER_TRACK_INDEX 9U',
    '#define TRACK_TOPOLOGY_FX_TRACK_INDEX 10U',
    '#define TRACK_TOPOLOGY_MASTER_TRACK_INDEX 11U',
    '#define TRACK_TOPOLOGY_INPUT_SECOND_TRACK_INDEX 12U',
    '#define TRACK_TOPOLOGY_INPUT_THIRD_TRACK_INDEX 13U'
)) {
    if (-not $header.Contains($contract)) {
        throw "Missing topology index contract: $contract"
    }
}
if ($topology -notmatch 'SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX[\s\S]*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_LOOPER_TRACK_INDEX[\s\S]*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_FX_TRACK_INDEX[\s\S]*SPECIAL_DESCRIPTOR\(TRACK_TOPOLOGY_MASTER_TRACK_INDEX') {
    throw 'Low-Cost/Premium Special order is not Input1, Looper, FX, Master'
}
if (-not $led.Contains('for (uint8_t hall = 0U; hall < HALL_KEY_COUNT; hall++)') -or
    -not $led.Contains('led_apply_track_select_hall_scene(hall)')) {
    throw 'Track-select scene is not rendered for the hall set'
}

'led_track_select_validation=PASS topology=role-based special=violet(128,0,128) active_special=violet play=unchanged absent_slots=off'
