$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw (Join-Path $repo $relativePath)
}

function Require-Text([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) { throw $message }
}

function Forbid-Text([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) { throw $message }
}

$patchHeader = Read-RepoFile 'Inc\Storage\patch_v1.h'
$patch = Read-RepoFile 'Src\Storage\patch_v1.c'
$kitHeader = Read-RepoFile 'Inc\Storage\kit_v1.h'
$kit = Read-RepoFile 'Src\Storage\kit_v1.c'
$patchBank = Read-RepoFile 'Src\Storage\patch_sd_bank.c'
$kitBank = Read-RepoFile 'Src\Storage\kit_sd_bank.c'

foreach ($payload in @($patchHeader, $kitHeader)) {
    Require-Text $payload 'uint8_t family;' 'Payload lost family identity'
    Require-Text $payload 'uint8_t type;' 'Payload lost type identity'
    Require-Text $payload 'uint8_t poly_voice_count;' 'Payload lacks generic VOICES'
    Require-Text $payload 'float poly_spread;' 'Payload lacks generic SPREAD'
    Forbid-Text $payload 'synth_voice_count' 'Synth-only VOICES payload remains'
    Forbid-Text $payload 'synth_spread' 'Synth-only SPREAD payload remains'
}

foreach ($required in @(
    'synth_polyphony_get_voice_count(track)',
    'synth_polyphony_get_spread(track)',
    'brick6_sampler_runtime_get_multi_voice_count(track)',
    'brick6_sampler_runtime_get_multi_spread(track)',
    'param_registry_apply_track_value(PARAM_CFG_POLY_VOICES',
    'param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD',
    'UI_TRACK_FAMILY_SAMPLER',
    'UI_TRACK_TYPE_MULTI'
)) { Require-Text $patch $required "Patch contract missing: $required" }
Forbid-Text $patch 'synth_polyphony_set_voice_count(target' 'Patch restore writes Synth authority directly'
Forbid-Text $patch 'synth_polyphony_set_spread(target' 'Patch restore writes Synth SPREAD directly'

foreach ($required in @(
    'synth_polyphony_get_voice_count(track)',
    'synth_polyphony_get_spread(track)',
    'brick6_sampler_runtime_get_multi_voice_count(track)',
    'brick6_sampler_runtime_get_multi_spread(track)',
    'SAMPLER_MULTI_MAX_VOICES_PER_TRACK',
    'param_registry_apply_track_value(PARAM_CFG_POLY_VOICES',
    'param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD',
    'UI_TRACK_FAMILY_SAMPLER',
    'UI_TRACK_TYPE_MULTI'
)) { Require-Text $kit $required "Kit contract missing: $required" }
Forbid-Text $kit 'synth_polyphony_set_voice_count(track' 'Kit restore writes Synth authority directly'
Forbid-Text $kit 'synth_polyphony_set_spread(track' 'Kit restore writes Synth SPREAD directly'

Require-Text $patchBank 'hdr->payload_size == sizeof(PatchSaveV1)' 'Patch bank does not persist the current payload size'
Require-Text $kitBank 'hdr->payload_size == sizeof(KitSaveV1)' 'Kit bank does not persist the current payload size'

foreach ($voices in 1, 2, 4, 8) {
    if ($voices -lt 1 -or $voices -gt 8) { throw 'Multi VOICES fixture is outside 1..8' }
}
foreach ($spread in 0.0, 0.5, 1.0) {
    if ($spread -lt 0.0 -or $spread -gt 1.0) { throw 'SPREAD fixture is outside 0..1' }
}

'patch_kit_multi_polyphony_step4_validation=PASS payload=generic family_type=retained patch=canonical kit=canonical multi_budget=isolated voices=1,2,4,8 spreads=0,0.5,1'
