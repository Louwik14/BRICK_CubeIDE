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
}
$current = Get-ParamEnumValues $store
foreach ($entry in $expected.GetEnumerator()) {
    if (-not $current.ContainsKey($entry.Key) -or $current[$entry.Key] -ne $entry.Value) {
        throw "$($entry.Key) is not $($entry.Value)"
    }
}

$headStore = (& git -C $root show 'HEAD:Inc/Param/param_store.h') -join "`n"
$before = Get-ParamEnumValues $headStore
foreach ($entry in $current.GetEnumerator()) {
    if ($entry.Key -like 'PARAM_RESERVED_*') { continue }
    if (-not $before.ContainsKey($entry.Key) -or $before[$entry.Key] -ne $entry.Value) {
        throw "ordinal changed for $($entry.Key)"
    }
}

foreach ($name in $expected.Keys) {
    if ($catalog -notmatch [regex]::Escape("$name, ")) { throw "descriptor missing for $name" }
    $descriptor = ($catalog -split "`r?`n") | Where-Object { $_ -match [regex]::Escape("$name,") } | Select-Object -First 1
    if ($descriptor -match ',\s*apply_') { throw "reserved descriptor has an apply callback: $name" }
}
if ($registry -notmatch 'param_id_is_reserved\(id\)') { throw 'reserved write guard missing' }
if ($ui -notmatch '\.params = \{ PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT \}') {
    throw 'UI fallback bank is not empty'
}
if ($runtime -notmatch 'default:\s*return rule;') { throw 'runtime NONE fallback missing' }
if ($seq -notmatch 'default:\s*return 0U;') { throw 'p-lock NONE fallback missing' }
if ($pattern -notmatch 'pattern_live_is_global_param_useful') { throw 'Pattern usefulness gate missing' }

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

Write-Output 'param_reserved_slots_validation=PASS ids=0..5 inert=1 ui=hidden p_lock=off modulation=off persistence=unused'
