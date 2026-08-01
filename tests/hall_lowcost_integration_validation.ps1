$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot

function Require-Text {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $content = Get-Content -Raw (Join-Path $repo $Path)
    if ($content -notmatch $Pattern) {
        throw $Message
    }
}

Require-Text "Src/App/Hall/hall_loop.c" `
    "hall_engine_process_sample\(sample\.key, sample\.raw, sample\.sample_count\)" `
    "Hall samples are not routed raw to hall_engine."
if ((Get-Content -Raw (Join-Path $repo "Src/App/Hall/hall_loop.c")) -match "hall_filter_asc|filtered_raw") {
    throw "A multi-sample ASC filter remains in the Hall runtime path."
}
Require-Text "Src/App/Hall/hall_surface.c" `
    "hall_engine_is_pressed\(lane\)[\s\S]*hall_engine_get_value\(lane\)" `
    "Navigation does not use the calibrated Hall press state."
Require-Text "Src/App/Hall/hall_keyboard_bridge.c" `
    "hall_engine_consume_note_on\(key\)[\s\S]*hall_engine_consume_note_off\(key\)[\s\S]*hall_engine_get_velocity\(key\)" `
    "Hall note bridge does not consume press/release and velocity from the same sample cycle."
Require-Text "Src/App/Hall/hall_engine.c" `
    "hall_velocity\[key\] = hall_velocity_compute\(key, range\);[\s\S]*hall_velocity_valid\[key\] = 1U;[\s\S]*hall_note_on_pending\[key\] = 1U" `
    "Note On is not published when velocity becomes determinable."
Require-Text "Src/App/Hall/hall_engine.c" `
    "defined\(BRICK6_VARIANT_LOWCOST\)[\s\S]*HALL_THRESHOLD_PPM\s+300U[\s\S]*#else[\s\S]*HALL_THRESHOLD_PPM\s+400U" `
    "Low-Cost/Premium Hall press thresholds are not variant-specific."
Require-Text "Src/App/Hall/hall_engine.c" `
    "HALL_HYST_PPM\s+100U" `
    "Hall hysteresis changed unexpectedly."
Require-Text "Src/App/Hall/hall_engine.c" `
    "range \* lo_ppm[\s\S]*range \* hi_ppm" `
    "Hall thresholds no longer use calibrated range."
Require-Text "Src/App/Hall/hall_engine.c" `
    "HALL_KEY_SAMPLE_PERIOD_US\s+2800U" `
    "Low-cost Hall debug cadence is not 2.8 ms."
Require-Text "Src/UI/pages/ui_page_settings.c" `
    'return \(index == 0U\) \? "HALL KBD" : "HALL VEL";' `
    "Settings calibration entries are missing."
Require-Text "Src/UI/pages/ui_page_settings.c" `
    "ui_page_calibration_open\(UI_PAGE_SETTINGS\)[\s\S]*ui_page_user_calibration_open\(UI_PAGE_SETTINGS\)" `
    "Settings does not reuse both calibration pages."
Require-Text "Src/UI/pages/ui_page_template_keyboard.c" `
    '"PROFILE"[\s\S]*"MODE"[\s\S]*"CURVE"[\s\S]*"NO CAL"' `
    "KEYBOARD VELOCITY controls or NO CAL state are missing."
Require-Text "Src/App/Hall/hall_calibration.c" `
    "BRICK6_VARIANT_LOWCOST\)\s*#define HALL_CAL_STORAGE_VERSION\s+2U[\s\S]*#else\s*#define HALL_CAL_STORAGE_VERSION\s+1U" `
    "Low-cost v2 / Premium v1 Hall storage split is missing."

Write-Output "Hall low-cost integration validation: PASS"
