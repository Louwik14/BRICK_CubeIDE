$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
function Read-Text([string]$relative) { Get-Content -Raw -LiteralPath (Join-Path $root $relative) }

$helper = Read-Text 'Src\Audio\spectral_window.c'
$header = Read-Text 'Inc\Audio\spectral_window.h'
$mixer = Read-Text 'Src\Audio\mixer.c'
$renderer = Read-Text 'Src\UI\ui_renderer_template.c'
$tone = Read-Text 'Src\UI\pages\ui_page_template_tone.c'
$store = Read-Text 'Inc\Param\param_store.h'
$seq = Read-Text 'Src\Seq\seq_param_iface.c'
$mod = Read-Text 'Src\Mod\mod_destination_catalog.c'
$pattern = Read-Text 'Src\Storage\pattern_live_ram.c'

if ($helper -notmatch 'logf\(' -or $helper -notmatch 'expf\(') { throw 'Spectral mapping is not logarithmic' }
if ($helper -notmatch 'min_width') { throw 'Minimum spectral width is not represented' }
if ($helper -notmatch 'low_cut_hz' -or $helper -notmatch 'high_cut_hz') { throw 'Spectral result contract is incomplete' }
if ($renderer -notmatch 'spectral_window_calculate' -or $renderer -notmatch 'spectral_window_log_position') { throw 'UI does not use the shared spectral authority' }
if ($mixer -notmatch 'spectral_window_calculate') { throw 'DSP path does not use the shared spectral authority' }
if ($tone -match 'PARAM_MIX_(REVERB|DELAY)_(HPF|LPF)') { throw 'Old HPF/LPF user controls remain in TONE' }
if ($store -match 'PARAM_MIX_(REVERB|DELAY)_(HPF|LPF)') { throw 'Old HPF/LPF parameter IDs remain in the current format' }
if ($seq -match 'PARAM_MIX_(REVERB|DELAY)_(HPF|LPF)') { throw 'Old HPF/LPF p-lock targets remain' }
if ($mod -match 'PARAM_MIX_(REVERB|DELAY)_(HPF|LPF)') { throw 'Old HPF/LPF modulation targets remain' }
foreach ($param in @('PARAM_MIX_REVERB_SPECTRAL_POSITION', 'PARAM_MIX_REVERB_SPECTRAL_WIDTH',
                     'PARAM_MIX_DELAY_SPECTRAL_POSITION', 'PARAM_MIX_DELAY_SPECTRAL_WIDTH')) {
    if ($pattern -notmatch [regex]::Escape("case ${param}:")) { throw "Persistence classification missing: $param" }
}

function Map-Window([double]$position, [double]$width) {
    $span = 0.05 + (0.95 * [Math]::Max(0.0, [Math]::Min(1.0, $width)))
    $left = [Math]::Max(0.0, [Math]::Min(1.0, $position)) - (0.5 * $span)
    $right = [Math]::Max(0.0, [Math]::Min(1.0, $position)) + (0.5 * $span)
    if ($left -lt 0.0) { $right -= $left; $left = 0.0 }
    if ($right -gt 1.0) { $left -= ($right - 1.0); $right = 1.0 }
    $left = [Math]::Max(0.0, [Math]::Min(1.0 - 0.05, $left))
    $right = [Math]::Max(0.05, [Math]::Min(1.0, $right))
    return @($left, $right)
}

foreach ($position in @(0.0, 0.5, 1.0)) {
    foreach ($width in @(0.0, 0.5, 1.0)) {
        $mapped = Map-Window $position $width
        if ($mapped[0] -ge $mapped[1]) { throw "Window crossed at position=$position width=$width" }
        if (($mapped[0] -lt 0.0) -or ($mapped[1] -gt 1.0)) { throw 'Window escaped normalized bounds' }
    }
}

$a = Map-Window 0.25 0.65
$b = Map-Window 0.25 0.65
if (($a[0] -ne $b[0]) -or ($a[1] -ne $b[1])) { throw 'Mapping is not deterministic' }

'spectral_window_validation=PASS logarithmic=shared min_width=0.05 limits=20..20000 order_independent=yes'
