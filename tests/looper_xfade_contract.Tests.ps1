$ErrorActionPreference = 'Stop'

function Invoke-XFadeModel {
    param([float]$Live, [float]$Loop, [float]$XFade)
    if ($XFade -lt 0.0) { $XFade = 0.0 }
    if ($XFade -gt 1.0) { $XFade = 1.0 }
    return ($Live * (1.0 - $XFade)) + ($Loop * $XFade)
}

function Assert-Near {
    param([float]$Actual, [float]$Expected, [string]$Name)
    if ([Math]::Abs($Actual - $Expected) -gt 0.000001) {
        throw "Looper XFADE contract failed: $Name ($Actual != $Expected)"
    }
}

Assert-Near (Invoke-XFadeModel 0.75 0.25 0.0) 0.75 'XFADE 0 is live only'
Assert-Near (Invoke-XFadeModel 0.75 0.25 1.0) 0.25 'XFADE 127 is loop only'
Assert-Near (Invoke-XFadeModel 0.75 0.25 (64.0 / 127.0)) `
    ((0.75 * (63.0 / 127.0)) + (0.25 * (64.0 / 127.0))) 'XFADE 64 is complementary'
Assert-Near (Invoke-XFadeModel 0.60 0.25 1.0) 0.25 'LINE live cannot leak at 127'
Assert-Near (Invoke-XFadeModel 0.40 0.25 1.0) 0.25 'USB live cannot leak at 127'
Assert-Near (Invoke-XFadeModel 0.90 0.25 1.0) 0.25 'send return cannot leak at 127'
Assert-Near (Invoke-XFadeModel 0.35 0.25 1.0) 0.25 'SD preview cannot leak at 127'

$root = Split-Path -Parent $PSScriptRoot
$mixer = Get-Content -Raw (Join-Path $root 'Src\Audio\Mixer\mixer_process.inc')
$dispatch = Get-Content -Raw (Join-Path $root 'Src\Audio\Engines\audio_engine_dispatch.c')
$audioIo = Get-Content -Raw (Join-Path $root 'Src\Audio\audio_io.c')

$returns = $mixer.IndexOf('fx_reverb_global_process_block_add(')
$preview = $mixer.IndexOf('sd_preview_render_main(bus_main_l, bus_main_r, frames)')
$xfade = $mixer.IndexOf('if(looper_xfade_apply_active != 0U)')
$master = $mixer.IndexOf('fx_chain_process_global_slot(2U, bus_main_l, bus_main_r, frames)')
if (($returns -lt 0) -or ($preview -lt 0) -or ($xfade -lt 0) -or ($master -lt 0) -or
        -not ($returns -lt $preview -and $preview -lt $xfade -and $xfade -lt $master)) {
    throw 'Looper XFADE bus ordering is not returns/preview -> XFADE -> master'
}
if ($dispatch.Contains('sd_preview_render_main(')) {
    throw 'SD preview still has a post-XFADE master bypass'
}
if ($mixer.Contains('audio_rec_bus_l[i] += bus_main_l[i]') -or
        $mixer.Contains('bus_main_l[i] += audio_rec_bus_l[i]')) {
    throw 'REC_BUS must remain capture-only'
}
if (-not $audioIo.Contains('monitor_main_l[n] = bus_main_l[n] * out_gain;')) {
    throw 'Physical and USB output must derive from the authoritative main bus'
}

Write-Output 'Looper XFADE contract: 11/11 cases passed'
