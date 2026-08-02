$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command arm-none-eabi-gcc -ErrorAction Stop).Source
$include = Join-Path $repo 'Inc'
$source = Join-Path $repo 'Src\Core\track_input_ownership.c'

foreach ($variant in @('BRICK6_VARIANT_LOWCOST', 'BRICK6_VARIANT_PREMIUM')) {
    & $compiler '-std=c11' '-Wall' '-Werror' '-fsyntax-only' "-D$variant" "-I$include" $source
    if ($LASTEXITCODE -ne 0) {
        throw "External input ownership compile failed for $variant"
    }
}

$header = Get-Content -Raw (Join-Path $include 'Core\track_input_ownership.h')
$ownership = Get-Content -Raw $source
$runtime = Get-Content -Raw (Join-Path $repo 'Src\Core\track_runtime.c')
$modCatalog = Get-Content -Raw (Join-Path $repo 'Src\Mod\mod_destination_catalog.c')
$snapshot = Get-Content -Raw (Join-Path $repo 'Src\Core\track_snapshot.c')
$patternHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\pattern_live_ram.h')
$patternLive = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')
$uiCore = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core.c')

foreach ($contract in @(
    'track_input_ownership_apply_bulk',
    'track_input_ownership_validate_bulk',
    'track_input_ownership_can_claim',
    'TRACK_INPUT_OWNER_NONE'
)) {
    if (-not $header.Contains($contract)) {
        throw "Missing ownership API: $contract"
    }
}

if (-not $ownership.Contains('owners[input] != TRACK_INPUT_OWNER_NONE')) {
    throw 'Duplicate External ownership is not rejected'
}
if (-not $ownership.Contains('input >= TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT')) {
    throw 'External input selection is not variant-bounded'
}
if (-not $runtime.Contains('TRACK_RUNTIME_FAMILY_EXTERNAL')) {
    throw 'External has no distinct runtime family'
}
if (-not ($runtime -match 'track_input_ownership_track_owns_input\(\s*track,\s*track_input_ownership_get_external_input\(track\)\)')) {
    throw 'Runtime audio routing does not consult the ownership authority'
}
if ($ownership.Contains('get_fixed_input_track') -or
    $ownership.Contains('get_audible_owner') -or
    $header.Contains('get_fixed_input_track') -or
    $header.Contains('get_audible_owner')) {
    throw 'A fixed Input fallback remains in the ownership authority'
}
if ($runtime.Contains('TRACK_RUNTIME_FIXED_INPUT_MIX_TRACK_COUNT') -or
    $runtime.Contains('track_runtime_mark_reserved_input_mix_tracks') -or
    $runtime -match 'ctx->mix_track_id\s*=\s*external_input') {
    throw 'Physical inputs still reserve or dictate mixer lanes'
}
if (-not $ownership.Contains('track < UI_TRACK_COUNT')) {
    throw 'External ownership is not available to all eight logical slots'
}
if (([regex]::Matches($runtime, 'track_runtime_is_audio_routable\(track\) == 0U')).Count -lt 3) {
    throw 'Non-owner Input params are not blocked from audio resources'
}
if (-not ($modCatalog.Contains('TRACK_RUNTIME_FAMILY_EXTERNAL') -and
          $modCatalog.Contains('track_runtime_is_audio_routable(track) == 0U'))) {
    throw 'External modulation does not enforce the audible owner route'
}
if (-not $snapshot.Contains('track_input_ownership_validate_bulk')) {
    throw 'Track paste does not preflight External ownership'
}
if (-not ($snapshot -match 'ctx->midi_channel,\s*ctx->midi_source,\s*ctx->external_input')) {
    throw 'Track paste bulk ownership arguments are out of contract order'
}
if (-not $patternHeader.Contains('external_input[SEQ_TRACK_COUNT]')) {
    throw 'Pattern snapshot does not persist External input selection'
}
if (-not $patternLive.Contains('track_input_ownership_validate_bulk')) {
    throw 'Pattern/Project restore does not preflight External ownership'
}
if (-not ($patternLive -match 'track_cfg->midi_channel,\s*track_cfg->midi_source,\s*track_cfg->external_input')) {
    throw 'Pattern/Project bulk ownership arguments are out of contract order'
}
if (-not $uiCore.Contains('"USED P%u"')) {
    throw 'Reserved Input Special lacks USED Pn feedback'
}

'external_input_ownership_validation=PASS midi_external=distinct owner=unique paste_load=atomic no_fallback=yes'
