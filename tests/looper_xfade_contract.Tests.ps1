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

function Assert-Contains {
    param([string]$Text, [string]$Needle, [string]$Name)
    if (-not $Text.Contains($Needle)) {
        throw "Looper XFADE contract failed: $Name"
    }
}

function Invoke-MainBusModel {
    param(
        [hashtable]$LiveSources,
        [float]$Loop,
        [float]$XFade,
        [float]$PostXFadeMonitor = 0.0
    )
    [float]$live = 0.0
    foreach ($source in $LiveSources.Values) { $live += [float]$source }
    return (Invoke-XFadeModel $live $Loop $XFade) + $PostXFadeMonitor
}

Assert-Near (Invoke-XFadeModel 0.75 0.25 0.0) 0.75 'XFADE 0 is live only'
Assert-Near (Invoke-XFadeModel 0.75 0.25 1.0) 0.25 'XFADE 127 is loop only'
Assert-Near (Invoke-XFadeModel 0.75 0.25 (64.0 / 127.0)) `
    ((0.75 * (63.0 / 127.0)) + (0.25 * (64.0 / 127.0))) 'XFADE 64 is complementary'
Assert-Near (Invoke-XFadeModel 0.60 0.25 1.0) 0.25 'LINE live cannot leak at 127'
Assert-Near (Invoke-XFadeModel 0.40 0.25 1.0) 0.25 'USB live cannot leak at 127'
Assert-Near (Invoke-XFadeModel 0.90 0.25 1.0) 0.25 'send return cannot leak at 127'
Assert-Near (Invoke-XFadeModel 0.35 0.25 1.0) 0.25 'SD preview cannot leak at 127'

# The UI percent policy is 0..127 -> canonical 0..1.  The command transports
# the canonical float bit-for-bit and AUDIO decodes that same value.
[float]$controlDisplay = 127.0
[float]$controlCanonical = $controlDisplay / 127.0
$transportedBits = [BitConverter]::ToUInt32([BitConverter]::GetBytes($controlCanonical), 0)
[float]$audioValue = [BitConverter]::ToSingle([BitConverter]::GetBytes($transportedBits), 0)
Assert-Near $controlCanonical 1.0 'CONTROL canonical value at display 127'
if ($transportedBits -ne 0x3F800000) {
    throw 'Looper XFADE contract failed: transported float at 127 is not 0x3F800000'
}
Assert-Near $audioValue 1.0 'AUDIO value at display 127'
Assert-Near (1.0 - $audioValue) 0.0 'live_gain at display 127'
Assert-Near $audioValue 1.0 'loop_gain at display 127'

# A real Looper live bus contains every supported monitored origin before the
# authoritative crossfade.  REC_BUS direct inputs are deliberately absent:
# they feed capture only.  The metronome is the sole post-XFADE monitor source.
$liveSources = @{
    Internal = 0.11
    ExternalLine = 0.13
    ExternalUsb = 0.17
    Group = 0.19
    SendReturns = 0.23
    SdPreview = 0.29
}
[float]$liveTotal = ($liveSources.Values | Measure-Object -Sum).Sum
[float]$loopSource = 0.31
Assert-Near (Invoke-MainBusModel $liveSources $loopSource 0.0) $liveTotal `
    'XFADE 0 keeps the complete live bus and rejects loop playback'
Assert-Near (Invoke-MainBusModel $liveSources $loopSource (64.0 / 127.0)) `
    (($liveTotal * (63.0 / 127.0)) + ($loopSource * (64.0 / 127.0))) `
    'XFADE 64 crossfades a real complete live bus with Looper playback'
Assert-Near (Invoke-MainBusModel $liveSources $loopSource 1.0) $loopSource `
    'XFADE 127 rejects every monitored live origin'
Assert-Near (Invoke-MainBusModel $liveSources $loopSource 1.0 0.07) 0.38 `
    'post-XFADE metronome remains independent from live/loop XFADE'

$root = Split-Path -Parent $PSScriptRoot
$mixer = Get-Content -Raw (Join-Path $root 'Src\Audio\Mixer\mixer_process.inc')
$dispatch = Get-Content -Raw (Join-Path $root 'Src\Audio\Engines\audio_engine_dispatch.c')
$audioIo = Get-Content -Raw (Join-Path $root 'Src\Audio\audio_io.c')
$valuePolicy = Get-Content -Raw (Join-Path $root 'Src\Param\param_value_policy.c')
$publication = Get-Content -Raw (Join-Path $root 'Src\App\live_parameter_audio_publication.c')
$audioRuntime = Get-Content -Raw (Join-Path $root 'Src\Audio\live_parameter_audio_runtime.c')
$backend = Get-Content -Raw (Join-Path $root 'Src\Param\param_registry_backends.c')
$looperRuntime = Get-Content -Raw (Join-Path $root 'Src\Audio\brick6_looper_runtime.c')
$codec = Get-Content -Raw (Join-Path $root 'Board\LowCost\Drivers\tlv320aic3204.c')

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
Assert-Contains $valuePolicy 'value * (max_value - min_value) / 127.0f' `
    'UI 0..127 normalization is not canonical 0..1'
Assert-Contains $publication 'live_parameter_event_encode_float(value)' `
    'CONTROL does not transport the canonical float'
Assert-Contains $audioRuntime 'live_parameter_event_decode_float((int32_t)value_bits)' `
    'AUDIO does not decode the transported float'
Assert-Contains $backend 'brick6_looper_runtime_set_main_xfade(track, clamped);' `
    'AUDIO parameter backend does not install Looper XFADE'
Assert-Contains $looperRuntime 'g_looper_tracks[track_id].main_xfade = looper_clampf(xfade, 0.0f, 1.0f);' `
    'Looper runtime does not retain canonical XFADE'
Assert-Contains $mixer 'const float live_gain = 1.0f - xfade;' `
    'mixer live gain is not complementary'
Assert-Contains $mixer 'const float loop_gain = xfade;' `
    'mixer loop gain is not canonical XFADE'
Assert-Contains $mixer 'memcpy(bus_main_l, looper_bus_main_l, sizeof(float) * frames);' `
    'full endpoint is not constructed exclusively from Looper playback'
Assert-Contains $audioIo 'metronome_runtime_render_main_monitor(monitor_main_l, monitor_main_r, frames);' `
    'post-XFADE monitor contribution inventory changed'
Assert-Contains $codec 'TLV_P1_HPL_ROUTE, 0x08U' `
    'Low-Cost HPL is not routed from its DAC-only source'
Assert-Contains $codec 'TLV_P1_HPR_ROUTE, 0x08U' `
    'Low-Cost HPR is not routed from its DAC-only source'

Write-Output 'Looper XFADE contract: CONTROL/AUDIO path, 0/mid/127 buses and outputs passed'
