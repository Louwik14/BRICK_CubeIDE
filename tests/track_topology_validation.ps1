$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command arm-none-eabi-gcc -ErrorAction Stop).Source
$include = Join-Path $repo 'Inc'
$source = Join-Path $repo 'Src\Core\track_topology.c'

foreach ($variant in @('BRICK6_VARIANT_LOWCOST', 'BRICK6_VARIANT_PREMIUM')) {
    & $compiler '-std=c11' '-Wall' '-Werror' '-fsyntax-only' "-D$variant" "-I$include" $source
    if ($LASTEXITCODE -ne 0) { throw "Topology compile failed for $variant" }
}

$header = Get-Content -Raw (Join-Path $include 'Core\track_topology.h')
$implementation = Get-Content -Raw $source
$catalog = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_track_catalog.c')
$mute = Get-Content -Raw (Join-Path $repo 'Src\Core\track_mute.c')

foreach ($contract in @(
    '#define TRACK_TOPOLOGY_TRACK_COUNT 8U',
    '#define TRACK_TOPOLOGY_PLAY_TRACK_COUNT TRACK_TOPOLOGY_TRACK_COUNT',
    '#define TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT TRACK_TOPOLOGY_TRACK_COUNT',
    '#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 1U',
    '#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 3U',
    'TRACK_CAPABILITY_INPUT_RESERVATION'
)) {
    if (-not $header.Contains($contract)) { throw "Missing topology contract: $contract" }
}

if (([regex]::Matches($implementation, 'TRACK_TOPOLOGY_ROLE_PLAY')).Count -ne 9) {
    throw 'Topology does not expose exactly eight homogeneous Play descriptors'
}
if (-not ($implementation.Contains('track_topology_get_special_track_count(void) { return 0U; }') -and
          $implementation.Contains('track_topology_find_special') -and
          $implementation.Contains('return 0U;'))) {
    throw 'Topology still exposes a fixed Special identity'
}
if ($catalog -notmatch 'k_sampler_types\[\]\s*=\s*\{[^}]*UI_TRACK_TYPE_LOOPER') {
    throw 'Looper is not an assignable catalog type'
}
if ($mute.Contains('case TRACK_TOPOLOGY_ROLE_LOOPER: return TRACK_MUTE_KIND_LOOPER') -or
    $mute.Contains('case TRACK_TOPOLOGY_ROLE_INPUT: return TRACK_MUTE_KIND_INPUT')) {
    throw 'Mute still derives Looper/Input behavior from a fixed topology role'
}

$audioPathFiles = Get-ChildItem (Join-Path $repo 'Src\Audio'), (Join-Path $repo 'Src\Core'), (Join-Path $repo 'Src\Seq') -Recurse -File -Include *.c,*.cpp
foreach ($file in $audioPathFiles) {
    if ((Get-Content -Raw $file.FullName) -match '\b(malloc|calloc|realloc|free)\s*\(') {
        throw "Dynamic allocation found in realtime-related source: $($file.FullName)"
    }
}

'track_topology_validation=PASS slots=8 homogeneous=yes special=none inputs=owned looper=assignable realtime_alloc=none'
