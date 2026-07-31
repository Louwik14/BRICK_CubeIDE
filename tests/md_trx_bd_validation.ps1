$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$source = [System.IO.File]::ReadAllText((Join-Path $repo "Src/Audio/drum_synth.cpp"))

foreach ($required in @(
    "md_trx_bd_prepare",
    "md_trx_bd_note_on",
    "md_trx_bd_render",
    "MD_MODEL_TRX_BD",
    "md_retrigger_fade_begin",
    "md_phase_sine_next",
    "md_rng_next_bipolar"
)) {
    if (-not $source.Contains($required)) { throw "Missing TRX-BD element: $required" }
}

$renderStart = $source.IndexOf("static void md_trx_bd_render")
$renderEnd = $source.IndexOf("static void drum_instance_reset_params", $renderStart)
if ($renderStart -lt 0 -or $renderEnd -le $renderStart) { throw "TRX-BD renderer bounds not found" }
$render = $source.Substring($renderStart, $renderEnd - $renderStart)
foreach ($forbidden in @("pow(", "powf(", "exp(", "expf(", "sinf(", "malloc(", "free(")) {
    if ($render.Contains($forbidden)) { throw "Forbidden TRX-BD hot-path call: $forbidden" }
}
if ($render.Contains("AnalogBassDrum") -or $render.Contains("plaits::")) {
    throw "Plaits dependency found in TRX-BD renderer"
}

Write-Output "MD TRX-BD validation: PASS"
