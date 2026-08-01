$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$store = Get-Content -Raw -LiteralPath (Join-Path $root 'Inc\Param\param_store.h')
$pattern = Get-Content -Raw -LiteralPath (Join-Path $root 'Src\Storage\pattern_live_ram.c')
$runtime = Get-Content -Raw -LiteralPath (Join-Path $root 'Src\Core\track_runtime.c')

function Get-ParamEnumValues([string]$text) {
    $body = [regex]::Match($text, '(?s)enum\s*\{(.*?)^\s*PARAM_COUNT\s*$', [System.Text.RegularExpressions.RegexOptions]::Multiline).Groups[1].Value
    if ([string]::IsNullOrEmpty($body)) { throw 'param enum not found' }

    $values = [ordered]@{}
    $next = 0
    foreach ($match in [regex]::Matches($body, '(?m)^\s*(PARAM_[A-Z0-9_]+)(?:\s*=\s*([0-9]+))?\s*,?')) {
        $name = $match.Groups[1].Value
        if ($match.Groups[2].Success) { $next = [int]$match.Groups[2].Value }
        $values[$name] = $next
        $next++
    }
    return $values
}

$params = Get-ParamEnumValues $store
$persistCount = [int]$params['PARAM_MIDI_FX_S1_PARAM1']
$runtimeRule = [regex]::Match($runtime, '(?s)track_runtime_param_rule_t track_runtime_get_param_rule\(.*?\n}\n\nuint8_t track_runtime_tone_slot_to_param').Value
if ([string]::IsNullOrEmpty($runtimeRule)) { throw 'runtime parameter rule function not found' }
$classification = [regex]::Match($pattern, '(?s)static pattern_live_param_class_t pattern_live_classify_param\(.*?\n}\n\nstatic uint8_t pattern_live_locks_required').Value
if ([string]::IsNullOrEmpty($classification)) { throw 'classification function not found' }
if ($classification -notmatch 'PATTERN_LIVE_PARAM_GLOBAL|PATTERN_LIVE_PARAM_TRACK_AWARE|PATTERN_LIVE_PARAM_RESERVED|PATTERN_LIVE_PARAM_NOT_RELEVANT') {
    throw 'classification states are incomplete'
}
if ($classification -notmatch 'return PATTERN_LIVE_PARAM_NOT_RELEVANT;') { throw 'classification default missing' }

$reserved = @($params.Keys | Where-Object { $_ -like 'PARAM_RESERVED_*' -and $params[$_] -lt $persistCount })
$global = @(
    'PARAM_MIX_REVERB_MODEL',
    'PARAM_MIX_REVERB_DIGITAL_DECAY', 'PARAM_MIX_REVERB_DIGITAL_DAMP',
    'PARAM_MIX_REVERB_DIGITAL_HPF', 'PARAM_MIX_REVERB_DIGITAL_LPF',
    'PARAM_MIX_REVERB_HPF', 'PARAM_MIX_REVERB_LPF',
    'PARAM_MIX_REVERB_DAMP', 'PARAM_MIX_REVERB_SMEAR',
    'PARAM_CFG_START', 'PARAM_CFG_TEMPO', 'PARAM_CFG_SYNC',
    'PARAM_CFG_REC_LEN', 'PARAM_CFG_METRO',
    'PARAM_MIX_SEND0_FX', 'PARAM_MIX_SEND1_FX',
    'PARAM_MIX_DELAY_TYPE', 'PARAM_MIX_DELAY_TIME', 'PARAM_MIX_DELAY_PINGPONG',
    'PARAM_MIX_DELAY_MODE', 'PARAM_MIX_DELAY_TIME_R', 'PARAM_MIX_DELAY_WIDTH',
    'PARAM_MIX_DELAY_FEEDBACK', 'PARAM_MIX_DELAY_HPF', 'PARAM_MIX_DELAY_LPF',
    'PARAM_MIX_DELAY_FBW', 'PARAM_MIX_DELAY_MOD', 'PARAM_MIX_DELAY_MOD_RATE',
    'PARAM_MIX_DELAY_REV', 'PARAM_MIX_DELAY_VOL',
    'PARAM_COMP_MODEL', 'PARAM_BUS_COMP_THRESHOLD_DB', 'PARAM_BUS_COMP_RATIO',
    'PARAM_BUS_COMP_ATTACK_INDEX', 'PARAM_BUS_COMP_RELEASE_INDEX',
    'PARAM_BUS_COMP_MAKEUP_DB', 'PARAM_BUS_COMP_DRYWET', 'PARAM_BUS_COMP_HPF_HZ',
    'PARAM_COMP_DETECT', 'PARAM_COMP_KNEE_DB', 'PARAM_COMP_DELUGE_SAT',
    'PARAM_POST_GAIN', 'PARAM_OUTPUT_COMP'
)

foreach ($name in $reserved) {
    if ($classification -notmatch [regex]::Escape("case ${name}:")) { throw "reserved ID is not explicit in Pattern classification: $name" }
    if ($store -notmatch [regex]::Escape("case ${name}:")) { throw "reserved ID is not explicit in param_id_is_reserved: $name" }
}
foreach ($name in $global) {
    if ($classification -notmatch [regex]::Escape("case ${name}:")) { throw "global ID is not explicit in Pattern classification: $name" }
}

foreach ($name in @('PARAM_MIX_MUTE', 'PARAM_CFG_POLY_VOICES', 'PARAM_CFG_POLY_SPREAD',
                    'PARAM_DRUM_MD_MODEL', 'PARAM_DRUM_MD_P1', 'PARAM_DRUM_MD_P8')) {
    if ($runtimeRule -notmatch [regex]::Escape("case ${name}:")) { throw "track-aware ID is absent from runtime rule evidence: $name" }
}

if ($pattern -match 'pattern_live_is_global_param_useful|pattern_live_is_reverb_global_tombstone') {
    throw 'historical persistence helpers remain'
}
if ($pattern -match 'id\s*[<>]=?\s*PARAM_RESERVED_0(06|37)') {
    throw 'Pattern persistence still classifies by reserved range'
}
if ($pattern -notmatch 'classification\s*==\s*PATTERN_LIVE_PARAM_TRACK_AWARE') {
    throw 'track-aware capture/restore path is not classification-driven'
}
if ($pattern -notmatch 'classification\s*==\s*PATTERN_LIVE_PARAM_GLOBAL') {
    throw 'global capture/restore path is not classification-driven'
}

$enumPersistent = @($params.Keys | Where-Object { $params[$_] -lt $persistCount })
$known = @{}
foreach ($name in ($reserved + $global)) { $known[$name] = $true }
foreach ($match in [regex]::Matches($runtimeRule, '(?m)^\s*case\s+(PARAM_[A-Z0-9_]+):')) { $known[$match.Groups[1].Value] = $true }
$unclassified = @($enumPersistent | Where-Object { -not $known.ContainsKey($_) })
if ($classification -notmatch 'default:\s*break;') { throw 'classification has no explicit default for non-relevant IDs' }

Write-Output "pattern_persistence_classification_validation=PASS ids=0..$($persistCount - 1) global=$($global.Count) reserved=$($reserved.Count) default_non_relevant=$($unclassified.Count) track_aware=explicit"
