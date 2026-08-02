$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$storePath = Join-Path $root 'Inc\Param\param_store.h'
$catalogPath = Join-Path $root 'Src\Param\param_registry_catalog.c'
$registryPath = Join-Path $root 'Src\Param\param_registry.c'
$uiPath = Join-Path $root 'Src\UI\ui_param.c'
$runtimePath = Join-Path $root 'Src\Core\track_runtime.c'
$seqPath = Join-Path $root 'Src\Seq\seq_param_iface.c'
$patternPath = Join-Path $root 'Src\Storage\pattern_live_ram.c'

function Read-Text([string]$path) {
    return Get-Content -Raw -LiteralPath $path
}

function Get-ParamEnumValues([string]$text) {
    $body = [regex]::Match($text, '(?s)enum\s*\{(.*?)^\s*PARAM_COUNT\s*$', [System.Text.RegularExpressions.RegexOptions]::Multiline).Groups[1].Value
    if ([string]::IsNullOrEmpty($body)) { throw 'param enum not found' }

    $values = @{}
    $next = 0
    foreach ($match in [regex]::Matches($body, '(?m)^\s*(PARAM_[A-Z0-9_]+)(?:\s*=\s*([0-9]+))?\s*,?')) {
        $name = $match.Groups[1].Value
        if ($match.Groups[2].Success) { $next = [int]$match.Groups[2].Value }
        $values[$name] = $next
        $next++
    }
    return $values
}

$store = Read-Text $storePath
$catalog = Read-Text $catalogPath
$registry = Read-Text $registryPath
$ui = Read-Text $uiPath
$runtime = Read-Text $runtimePath
$seq = Read-Text $seqPath
$pattern = Read-Text $patternPath

$expected = @{
    PARAM_RESERVED_000 = 0
    PARAM_RESERVED_001 = 1
    PARAM_RESERVED_002 = 2
    PARAM_RESERVED_003 = 3
    PARAM_RESERVED_004 = 4
    PARAM_RESERVED_005 = 5
    PARAM_RESERVED_006 = 6
    PARAM_RESERVED_007 = 7
    PARAM_RESERVED_008 = 8
    PARAM_RESERVED_009 = 9
    PARAM_RESERVED_010 = 10
    PARAM_RESERVED_011 = 11
    PARAM_RESERVED_012 = 12
    PARAM_RESERVED_013 = 13
    PARAM_RESERVED_015 = 15
    PARAM_RESERVED_018 = 18
    PARAM_RESERVED_019 = 19
    PARAM_RESERVED_020 = 20
    PARAM_RESERVED_030 = 30
    PARAM_RESERVED_031 = 31
    PARAM_RESERVED_032 = 32
    PARAM_RESERVED_033 = 33
    PARAM_RESERVED_034 = 34
    PARAM_RESERVED_035 = 35
    PARAM_RESERVED_036 = 36
    PARAM_RESERVED_037 = 37
    PARAM_MIX_MUTE = 14
    PARAM_CFG_POLY_VOICES = 16
    PARAM_CFG_POLY_SPREAD = 17
    PARAM_DRUM_MD_MODEL = 21
    PARAM_DRUM_MD_P1 = 22
    PARAM_DRUM_MD_P2 = 23
    PARAM_DRUM_MD_P3 = 24
    PARAM_DRUM_MD_P4 = 25
    PARAM_DRUM_MD_P5 = 26
    PARAM_DRUM_MD_P6 = 27
    PARAM_DRUM_MD_P7 = 28
    PARAM_DRUM_MD_P8 = 29
    PARAM_MIX_REVERB_DAMP = 174
    PARAM_RESERVED_175 = 175
    PARAM_MIX_REVERB_SPECTRAL_POSITION = 162
    PARAM_MIX_REVERB_SPECTRAL_WIDTH = 163
    PARAM_MIX_DELAY_SPECTRAL_POSITION = 171
    PARAM_MIX_DELAY_SPECTRAL_WIDTH = 172
}
$current = Get-ParamEnumValues $store
foreach ($entry in $expected.GetEnumerator()) {
    if (-not $current.ContainsKey($entry.Key) -or $current[$entry.Key] -ne $entry.Value) {
        throw "$($entry.Key) is not $($entry.Value)"
    }
}

$renamed = @($expected.Keys)
$headStore = (& git -C $root show 'HEAD:Inc/Param/param_store.h') -join "`n"
$before = Get-ParamEnumValues $headStore
foreach ($entry in $current.GetEnumerator()) {
    if ($renamed -contains $entry.Key) { continue }
    if (-not $before.ContainsKey($entry.Key) -or $before[$entry.Key] -ne $entry.Value) {
        throw "ordinal changed for $($entry.Key)"
    }
}

foreach ($name in $expected.Keys) {
    if ($catalog -notmatch [regex]::Escape("$name, ")) { throw "descriptor missing for $name" }
}
foreach ($name in ($expected.Keys | Where-Object { $_ -like 'PARAM_RESERVED_*' })) {
    $descriptor = ($catalog -split "`r?`n") | Where-Object { $_ -match [regex]::Escape("$name,") } | Select-Object -First 1
    if ($descriptor -match ',\s*apply_') { throw "reserved descriptor has an apply callback: $name" }
}
if ((($current.Values | Measure-Object -Maximum).Maximum + 1) -ne 323) { throw 'PARAM_COUNT changed' }
if ($store -notmatch '#define PARAM_PERSIST_COUNT PARAM_MIDI_FX_S1_PARAM1') {
    throw 'PARAM_PERSIST_COUNT boundary changed'
}
if ($store -notmatch 'PARAM_MIDI_FX_S1_PARAM1\s*,') {
    throw 'PARAM_PERSIST_COUNT source ordinal missing'
}
if ($registry -notmatch 'param_id_is_reserved\(id\)') { throw 'reserved write guard missing' }
if ($ui -notmatch '\.params = \{ PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT \}') {
    throw 'UI fallback bank is not empty'
}
if ($runtime -notmatch 'default:\s*return rule;') { throw 'runtime NONE fallback missing' }
if ($seq -notmatch 'default:\s*return 0U;') { throw 'p-lock NONE fallback missing' }
if ($pattern -notmatch 'pattern_live_classify_param') { throw 'Pattern persistence classification missing' }
if ($pattern -match 'pattern_live_is_global_param_useful|pattern_live_is_reverb_global_tombstone') {
    throw 'Historical Pattern persistence helper remains'
}

$excluded = '\\(build|\.git|Inspiration|artifacts|mutable_instruments|Drivers|App|tinyusb)\\'
$files = Get-ChildItem -LiteralPath $root -File -Recurse |
    Where-Object { $_.FullName -notmatch $excluded -and $_.FullName -notmatch '\\docs\\audits\\' }
$forbidden = @(
    ('PARAM_' + 'GRAN_'),
    ('apply_' + 'gran_'),
    ('FX_' + 'GRANULAR'),
    ('fx_' + 'granular'),
    ('"' + 'Gran ')
)
foreach ($file in $files) {
    $text = Get-Content -Raw -LiteralPath $file.FullName
    foreach ($token in $forbidden) {
        if ($text.Contains($token)) { throw "forbidden granular surface in $($file.FullName): $token" }
    }
}

$legacyPrefix = 'PARAM_' + 'MIX_TRACK'
foreach ($file in $files) {
    $text = Get-Content -Raw -LiteralPath $file.FullName
    if ($text.Contains($legacyPrefix + '0_') -or
        $text.Contains($legacyPrefix + '1_') -or
        $text.Contains($legacyPrefix + '2_') -or
        $text.Contains($legacyPrefix + '3_')) {
        throw "historical MIX lane symbol remains in $($file.FullName)"
    }
}

Write-Output 'param_reserved_slots_validation=PASS ids=0..37 canonical=active-or-reserved inert=1 ui=hidden p_lock=off modulation=off persistence=unused'
