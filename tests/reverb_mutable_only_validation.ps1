$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
function Read-Text([string]$relative) { Get-Content -Raw -LiteralPath (Join-Path $root $relative) }

$tone = Read-Text 'Src\UI\pages\ui_page_template_tone.c'
$toneRenderer = Read-Text 'Src\UI\ui_renderer_template.c'
$reverb = Read-Text 'Src\Audio\fx_reverb.cpp'
$revb = Read-Text 'Src\Audio\fx_reverb_revb.cpp'
$model = Read-Text 'Inc\Audio\fx_revb_model.h'
$mixer = Read-Text 'Src\Audio\mixer.c'
$pattern = Read-Text 'Src\Storage\pattern_live_ram.c'
$runtime = Read-Text 'Src\Core\track_runtime.c'

foreach ($text in @($tone, $toneRenderer, $reverb, $revb, $model, $mixer, $pattern, $runtime)) {
    if ($text -match 'PARAM_MIX_REVERB_MODEL|PARAM_MIX_REVERB_DIGITAL|ProcessDigital|set_model|digital_decay|digital_damp|digital_hpf|digital_lpf') {
        throw 'Removed reverb model or backend surface remains'
    }
}

if ($tone -notmatch 'return &g_ui_template_tone_family_master_reverb_mutable;') {
    throw 'Master TONE does not resolve directly to Mutable'
}
$mutableSurface = [regex]::Match($tone, '(?s)g_ui_template_tone_family_master_reverb_mutable\s*=\s*\{.*?\n\};').Value
 $visibleSurface = [regex]::Match($mutableSurface, '(?s)REVERB 1.*?REVERB 2.*?(?=\{ \.title = "-")').Value
if (-not $visibleSurface -or $visibleSurface -match 'PARAM_COUNT') {
    throw 'Mutable reverb UI contains an empty encoder slot'
}
foreach ($param in @(
    'PARAM_MIX_REVERB_WET', 'PARAM_MIX_REVERB_SIZE', 'PARAM_MIX_REVERB_DECAY',
    'PARAM_MIX_REVERB_PRED', 'PARAM_MIX_REVERB_DAMP', 'PARAM_MIX_REVERB_HPF',
    'PARAM_MIX_REVERB_LPF', 'PARAM_MIX_REVERB_SMEAR')) {
    if ($visibleSurface -notmatch [regex]::Escape($param)) { throw "Mutable UI parameter missing: $param" }
    if ($pattern -notmatch [regex]::Escape("case ${param}:")) { throw "Pattern global missing: $param" }
    if ($runtime -notmatch [regex]::Escape("case ${param}:")) { throw "Runtime global missing: $param" }
}

if ($revb -notmatch 'g_revb\.engine\.Process\(' -or $revb -notmatch 'fx_reverb_revb_global_reset') {
    throw 'Mutable backend process/reset contract is incomplete'
}
if ($reverb -notmatch 'g_reverb_global\.wet > 0\.0f' -or $reverb -notmatch 'fx_reverb_revb_global_process_send_mono_to_stereo_wet') {
    throw 'Mutable bypass/process contract is incomplete'
}

$delayCall = $mixer.IndexOf('fx_delay_stereo_global_process_block')
$reverbCall = $mixer.IndexOf('fx_reverb_global_process_block')
$compressorCall = $mixer.IndexOf('fx_master')
if ($delayCall -lt 0 -or $reverbCall -lt 0 -or ($delayCall -gt $reverbCall)) {
    throw 'Master effect order does not keep Delay before Reverb'
}
if ($tone -notmatch 'MASTER 2/3' -or $tone -notmatch 'MASTER 3/3') {
    throw 'Delay/Compressor Master page order is missing'
}

'reverb_mutable_only_validation=PASS surface=8 params selector=none backend=digital-free bypass=active order=master'
