$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $repo 'Inc\Core\track_topology.h')
$input = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_hall_input_service.c')
$flow = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_hall_mode_flow.c')
$led = Get-Content -Raw (Join-Path $repo 'Drivers\Drv_app\Src\led_rgb.c')

if (-not $header.Contains('#define TRACK_TOPOLOGY_TRACK_COUNT 8U')) {
    throw 'Track selection domain is not eight slots'
}
if ($input -notmatch 'hall < UI_ACTIVE_TRACK_COUNT') {
    throw 'Hall track selection is not bounded to STEP 1-8'
}
if ($flow -notmatch 'if \(hall == 15U\)[\s\S]{0,320}ui_page_template_tone_open_global_master') {
    throw 'SHIFT + STEP 16 does not open global Master'
}
if ($led -match 'special|macro_fx') {
    throw 'Special/FX LED behavior remains'
}

'led_track_select_validation=PASS tracks=step1-8 contextual=step9-16 master=shift+step16 special_led=none'
