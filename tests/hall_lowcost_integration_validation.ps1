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
    "BRICK6_VARIANT_LOWCOST[\s\S]*hall_engine_process_sample\(sample\.key, sample\.raw, sample\.sample_count\)" `
    "Low-cost Hall samples are not routed raw to hall_engine."
Require-Text "Src/App/Hall/hall_loop.c" `
    "hall_filter_asc_process\(sample\.key, sample\.raw, &filtered_raw\)" `
    "Premium ASC path is no longer present."
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
