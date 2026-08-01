$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command arm-none-eabi-gcc -ErrorAction Stop).Source
$include = Join-Path $repo 'Inc'
$topology = Join-Path $repo 'Src\Core\track_topology.c'
$identity = Join-Path $repo 'Src\Storage\storage_track_identity.c'

foreach ($variant in @('BRICK6_VARIANT_LOWCOST', 'BRICK6_VARIANT_PREMIUM')) {
    & $compiler '-std=c11' '-Wall' '-Werror' '-fsyntax-only' "-D$variant" "-I$include" $topology $identity
    if ($LASTEXITCODE -ne 0) { throw "Identity remap compile failed for $variant" }
}

function Resolve-Identity([string]$role, [int]$ordinal, [bool]$premium) {
    if ($role -eq 'Play' -and $ordinal -ge 0 -and $ordinal -lt 8) { return $ordinal }
    if ($role -eq 'Input') {
        $inputs = if ($premium) { @(8, 12, 13) } else { @(8) }
        if ($ordinal -ge 0 -and $ordinal -lt $inputs.Count) { return $inputs[$ordinal] }
        return -1
    }
    if ($ordinal -ne 0) { return -1 }
    switch ($role) {
        'Looper' { return 9 }
        'FX' { return 10 }
        'Master' { return 11 }
        default { return -1 }
    }
}

function Assert-Old-Order-Remap([bool]$premium) {
    $stored = @()
    0..7 | ForEach-Object { $stored += [pscustomobject]@{ Role = 'Play'; Ordinal = $_ } }
    if ($premium) {
        $stored += [pscustomobject]@{ Role = 'Master'; Ordinal = 0 }
        $stored += [pscustomobject]@{ Role = 'Looper'; Ordinal = 0 }
        $stored += [pscustomobject]@{ Role = 'Input'; Ordinal = 0 }
        $stored += [pscustomobject]@{ Role = 'Input'; Ordinal = 1 }
        $stored += [pscustomobject]@{ Role = 'Input'; Ordinal = 2 }
        $stored += [pscustomobject]@{ Role = 'FX'; Ordinal = 0 }
        $expected = @(0,1,2,3,4,5,6,7,11,9,8,12,13,10)
    } else {
        $stored += [pscustomobject]@{ Role = 'Master'; Ordinal = 0 }
        $stored += [pscustomobject]@{ Role = 'Looper'; Ordinal = 0 }
        $stored += [pscustomobject]@{ Role = 'Input'; Ordinal = 0 }
        $stored += [pscustomobject]@{ Role = 'FX'; Ordinal = 0 }
        $expected = @(0,1,2,3,4,5,6,7,11,9,8,10)
    }
    $seen = @{}
    $distinct = 100..(99 + $stored.Count)
    $applied = New-Object int[] $stored.Count
    for ($source = 0; $source -lt $stored.Count; ++$source) {
        $destination = Resolve-Identity $stored[$source].Role $stored[$source].Ordinal $premium
        if ($destination -lt 0 -or $seen.ContainsKey($destination)) { throw 'Fixture identity is invalid or duplicated' }
        if ($destination -ne $expected[$source]) { throw 'Old-order remap differs from target topology' }
        $seen[$destination] = $true
        $applied[$destination] = $distinct[$source]
    }
    if ($seen.Count -ne $stored.Count) { throw 'Fixture remap is not bijective' }
    for ($source = 0; $source -lt $stored.Count; ++$source) {
        if ($applied[$expected[$source]] -ne $distinct[$source]) { throw 'Distinct role payload moved to the wrong identity' }
    }
}

Assert-Old-Order-Remap $false
Assert-Old-Order-Remap $true

$pattern = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')
$project = Get-Content -Raw (Join-Path $repo 'Src\Storage\project_v1.c')
$kit = Get-Content -Raw (Join-Path $repo 'Src\Storage\kit_v1.c')
$helper = Get-Content -Raw $identity

foreach ($required in @('current_seen[current] != 0U', 'current_seen[current] == 0U', 'track_topology_resolve_identity', 'STORAGE_TRACK_IDENTITY_UNMAPPED')) {
    if (-not $helper.Contains($required)) { throw "Identity remap guard missing: $required" }
}
foreach ($required in @('pattern_live_normalize_by_identity', 'sound.track_values[destination]', 'mix.track_values[destination]', 'seq.special[destination - TRACK_TOPOLOGY_PLAY_TRACK_COUNT]', 'looper_route_enabled[remap[source_looper]][remap[source_track]]', 'globals.track_div[destination]', 'note_fx[destination]')) {
    if (-not $pattern.Contains($required)) { throw "Pattern identity remap missing: $required" }
}
foreach ($required in @('project_v1_normalize_track_payloads', 'g_project_normalized_multi[remap[source]]', 'entry->track = remap[entry->track]', 'g_project_macro_state = g_project_normalized_macro')) {
    if (-not $project.Contains($required)) { throw "Project identity remap missing: $required" }
}
foreach ($required in @('kit_v1_normalize_by_identity', 'normalized->tracks[destination] = stored->tracks[source]', 'normalized->meta.summary[destination]')) {
    if (-not $kit.Contains($required)) { throw "Kit identity remap missing: $required" }
}

$patternBank = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_sd_bank.c')
$projectHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\project_v1.h')
$kitBank = Get-Content -Raw (Join-Path $repo 'Inc\Storage\kit_sd_bank.h')
$patchBank = Get-Content -Raw (Join-Path $repo 'Inc\Storage\patch_sd_bank.h')
if (-not ($patternBank.Contains('#define PATTERN_VERSION    5U') -and
          $projectHeader.Contains('#define PROJECT_V1_FILE_VERSION    5U') -and
          $kitBank.Contains('#define KIT_SD_FILE_VERSION 3U') -and
          $patchBank.Contains('#define PATCH_SD_FILE_VERSION 3U'))) {
    throw 'Persistent format version changed'
}

'storage_track_identity_remap_validation=PASS old_order_lowcost=yes old_order_premium=yes bijection=yes pattern=all_track_blocks project=multi+macro kit=payload+summary formats=v5,v5,v3,v3'
