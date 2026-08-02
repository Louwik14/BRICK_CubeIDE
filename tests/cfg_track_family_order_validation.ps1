$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$uiHeader = Get-Content -Raw (Join-Path $repo 'Inc\UI\ui_core.h')
$catalogHeader = Get-Content -Raw (Join-Path $repo 'Inc\UI\ui_track_catalog.h')
$catalog = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_track_catalog.c')
$uiParam = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_param.c')
$renderer = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_renderer_template.c')
$registry = Get-Content -Raw (Join-Path $repo 'Src\Param\param_registry_catalog.c')

$expectedEnum = @(
    'UI_TRACK_FAMILY_OFF',
    'UI_TRACK_FAMILY_INPUT1',
    'UI_TRACK_FAMILY_INPUT2',
    'UI_TRACK_FAMILY_INPUT3',
    'UI_TRACK_FAMILY_SYNTH',
    'UI_TRACK_FAMILY_DRUM',
    'UI_TRACK_FAMILY_MIDI',
    'UI_TRACK_FAMILY_SAMPLER',
    'UI_TRACK_FAMILY_EXTERNAL',
    'UI_TRACK_FAMILY_COUNT'
)
$enumBlock = [regex]::Match($uiHeader, '(?s)typedef enum\s*\{(.*?)\}\s*ui_track_family_t;').Groups[1].Value
if ([string]::IsNullOrEmpty($enumBlock)) { throw 'Family enum block is missing' }
$actualEnum = @([regex]::Matches($enumBlock, 'UI_TRACK_FAMILY_[A-Z0-9_]+') | ForEach-Object { $_.Value })
if (($actualEnum -join ',') -ne ($expectedEnum -join ',')) {
    throw "Persisted family enum order changed: $($actualEnum -join ',')"
}
if (-not $enumBlock.Contains('UI_TRACK_FAMILY_OFF = 0')) {
    throw 'Persisted family enum base value changed'
}
if ($enumBlock -match 'UI_TRACK_FAMILY_(INPUT1|INPUT2|INPUT3|SYNTH|DRUM|MIDI|SAMPLER|EXTERNAL)\s*=') {
    throw 'Persisted family enum contains a changed explicit value'
}

$order = [regex]::Match($catalog, '(?s)g_cfg_play_family_order\[\]\s*=\s*\{(.*?)\};').Groups[1].Value
if ($order -notmatch 'UI_TRACK_FAMILY_OFF[\s\S]*UI_TRACK_FAMILY_SYNTH[\s\S]*UI_TRACK_FAMILY_DRUM[\s\S]*UI_TRACK_FAMILY_MIDI[\s\S]*UI_TRACK_FAMILY_EXTERNAL[\s\S]*UI_TRACK_FAMILY_SAMPLER') {
    throw 'CFG Play family order is not Off, Synth, Drum, MIDI, External, Sampler'
}
if ($order.Contains('UI_TRACK_FAMILY_INPUT1') -or $order.Contains('UI_TRACK_FAMILY_INPUT2') -or $order.Contains('UI_TRACK_FAMILY_INPUT3')) {
    throw 'Special Input families leaked into the Play CFG order'
}
foreach ($helper in @(
    'ui_track_catalog_cfg_family_order_count',
    'ui_track_catalog_cfg_family_order_at',
    'ui_track_catalog_cfg_family_order_index',
    'ui_track_catalog_cfg_family_step'
)) {
    if (-not $catalogHeader.Contains($helper) -or -not $catalog.Contains($helper)) { throw "Missing CFG order helper: $helper" }
}
if ($catalog -notmatch 'direction > 0[\s\S]*position \+ 1U >= order_count[\s\S]*else[\s\S]*position == 0U') {
    throw 'CFG family navigation does not implement both directions'
}
if ($catalog -notmatch 'position \+ 1U >= order_count[\s\S]*return current') {
    throw 'CFG family navigation does not clamp at the forward limit'
}
if ($catalog -notmatch 'position == 0U[\s\S]*return current') {
    throw 'CFG family navigation does not clamp at the reverse limit'
}
if ($catalog -match 'order_count - 1U\) : 0U|position == 0U\) \? \(uint8_t\)\(order_count - 1U\)') {
    throw 'CFG family navigation still wraps between order limits'
}
if ($catalog -notmatch 'ui_track_catalog_family_is_available\(track, candidate, track_configs\)') {
    throw 'CFG family navigation does not skip unavailable families'
}
if (-not $catalog.Contains('if (track_topology_is_play(track) == 0U)')) {
    throw 'CFG family catalog does not preserve Special track isolation'
}
$uiCore = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core.c')
if ($uiCore -notmatch 'if \(track_topology_is_play\(track\) == 0U\)[\s\S]*return false;') {
    throw 'Special tracks are not protected from family mutation'
}
$cfgStepper = [regex]::Match($uiParam, '(?s)static float ui_param_step_cfg_track\(.*?\n\}\n\nstatic float ui_param_step_cfg_track_type').Groups[0].Value
if ($cfgStepper.Contains('candidate = (int16_t)(candidate + direction)') -or
    $cfgStepper -notmatch 'ui_track_catalog_cfg_family_step') {
    throw 'CFG encoder still steps raw family enum ordinals'
}
if ($renderer -notmatch 'ui_get_track_family_display_name\(\(ui_track_family_t\)') {
    throw 'CFG family labels are not resolved from canonical enum values'
}
if ($registry -notmatch 'g_track_family_labels\[\].*Off.*Input1.*Synth.*Drum.*MIDI.*Sampler.*External') {
    throw 'Canonical enum labels are missing or reordered'
}

'cfg_track_family_order_validation=PASS enum=0,1,2,3,4,5,6,7,8 order=Off-Synth-Drum-MIDI-External-Sampler inputs=excluded clamp=both unavailable=skipped special=fixed labels=enum persistence=unchanged'
