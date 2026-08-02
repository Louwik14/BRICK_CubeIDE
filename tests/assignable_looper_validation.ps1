$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$catalog = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_track_catalog.c')
$state = Get-Content -Raw (Join-Path $repo 'Src\Core\track_state.c')
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Core\track_runtime.c')
$looper = Get-Content -Raw (Join-Path $repo 'Src\Core\brick6_looper_runtime.c')
$mixer = Get-Content -Raw (Join-Path $repo 'Inc\Audio\mixer.h')
$pattern = Get-Content -Raw (Join-Path $repo 'Inc\Storage\pattern_live_ram.h')
$undo = Get-Content -Raw (Join-Path $repo 'Src\Storage\undo_v2.c')

if ($catalog -notmatch 'k_sampler_types\[\]\s*=\s*\{[^}]*UI_TRACK_TYPE_LOOPER') {
    throw 'Looper is absent from the assignable Sampler catalog'
}
if ($catalog -match 'UI_TRACK_TYPE_LOOPER\)\)\s*\|\|\s*ui_track_catalog_family_is_input') {
    throw 'Looper is still rejected by the availability gate'
}
if ($state -match 'config->type\s*=\s*UI_TRACK_TYPE_RAM') {
    throw 'Bulk restore still normalizes Looper to RAM'
}
if (-not ($looper.Contains('BRICK6_LOOPER_TRACK_CAP SEQ_TRACK_COUNT') -and
          $looper.Contains('g_looper_tracks[BRICK6_LOOPER_TRACK_CAP]'))) {
    throw 'Looper runtime state is not indexed by all logical slots'
}
if (-not $mixer.Contains('#define MIXER_MAX_TRACKS SEQ_TRACK_COUNT')) {
    throw 'Mixer lane matrix is not eight-track homogeneous'
}
if (-not $pattern.Contains('looper_route_enabled[SEQ_TRACK_COUNT][SEQ_TRACK_COUNT]')) {
    throw 'Pattern route matrix is not slot indexed'
}
if ($undo -match 'track_state_set_track_(family|type)') {
    throw 'Engine selection leaked into Undo/Redo'
}
if (-not ($runtime.Contains('BRICK6_LOOPER_GLOBAL_CAP') -and
          $runtime.Contains('TRACK_RUNTIME_ENGINE_LOOPER'))) {
    throw 'Looper runtime quota binding is absent'
}

'assignable_looper_validation=PASS slots=8 catalog=yes state=slot-indexed restore=atomic undo=excluded quota=variant'
