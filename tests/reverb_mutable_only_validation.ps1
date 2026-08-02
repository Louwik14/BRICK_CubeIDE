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
$store = Read-Text 'Inc\Param\param_store.h'
$catalog = Read-Text 'Src\Param\param_registry_catalog.c'
$bindings = Read-Text 'Inc\Param\param_registry_apply_bindings.h'
$apply = Read-Text 'Src\Param\param_registry_apply_wrappers.c'
$mixerHeader = Read-Text 'Inc\Audio\mixer.h'
$reverbHeader = Read-Text 'Inc\Audio\fx_reverb.h'
$revbHeader = Read-Text 'Inc\Audio\fx_reverb_revb.h'
$legacyControl = ([char]83) + ([char]77) + ([char]69) + ([char]65) + ([char]82)
$legacyParam = 'PARAM_MIX_REVERB_' + $legacyControl

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
if (-not $visibleSurface -or $visibleSurface -match [regex]::Escape($legacyParam) -or $visibleSurface -match 'PARAM_COUNT,\s*PARAM_COUNT') {
    throw 'Mutable reverb UI contains an obsolete or empty consecutive slot'
}
foreach ($param in @(
    'PARAM_MIX_REVERB_WET', 'PARAM_MIX_REVERB_SIZE', 'PARAM_MIX_REVERB_DECAY',
    'PARAM_MIX_REVERB_PRED', 'PARAM_MIX_REVERB_DAMP', 'PARAM_MIX_REVERB_SPECTRAL_POSITION',
    'PARAM_MIX_REVERB_SPECTRAL_WIDTH')) {
    if ($visibleSurface -notmatch [regex]::Escape($param)) { throw "Mutable UI parameter missing: $param" }
    if ($pattern -notmatch [regex]::Escape("case ${param}:")) { throw "Pattern global missing: $param" }
    if ($runtime -notmatch [regex]::Escape("case ${param}:")) { throw "Runtime global missing: $param" }
}

if ($revb -notmatch 'g_revb\.engine\.Process\(' -or $revb -notmatch 'fx_reverb_revb_global_reset') {
    throw 'Mutable backend process/reset contract is incomplete'
}
if ($model -match 'Interpolate\(ap1' -or $model -match 'Write\(ap1,\s*109') {
    throw 'Mutable AP1 control injection diverges from the stable Deluge implementation'
}
if ($model -notmatch '0\.01f\s*\+\s*\(0\.97f') {
    throw 'Mutable decay range is not aligned to Deluge 0.01..0.98'
}
foreach ($text in @($tone, $reverb, $revb, $model, $mixer, $pattern, $runtime, $store, $catalog, $bindings, $apply, $mixerHeader, $reverbHeader, $revbHeader)) {
    if ($text -match "(?i)$legacyControl") {
        throw 'Removed Mutable control remains in the active product path'
    }
}
if ($store -match [regex]::Escape($legacyParam) -or $catalog -match [regex]::Escape($legacyParam)) {
    throw 'Removed Mutable control remains in the active product path'
}
if ($tone -notmatch 'PARAM_MIX_REVERB_SPECTRAL_WIDTH, PARAM_MIX_REVERB_DAMP, PARAM_COUNT') {
    throw 'Mutable reverb UI still exposes an obsolete slot'
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

'reverb_mutable_only_validation=PASS surface=7 params selector=none backend=digital-free bypass=active order=master'
