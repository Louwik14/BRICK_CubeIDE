$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw (Join-Path $repo $relativePath)
}

function Require-Text([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        throw $message
    }
}

function Forbid-Text([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) {
        throw $message
    }
}

$pattern = Read-RepoFile 'Src\Storage\pattern_live_ram.c'
$snapshot = Read-RepoFile 'Src\Core\track_snapshot.c'
$snapshotHeader = Read-RepoFile 'Inc\Core\track_snapshot.h'

Require-Text $pattern '#include "Core/brick6_sampler_runtime.h"' 'Pattern restore has no Multi runtime authority'
Require-Text $pattern 'UI_TRACK_FAMILY_SAMPLER' 'Pattern budget has no sampler family branch'
Require-Text $pattern 'UI_TRACK_TYPE_MULTI' 'Pattern budget has no Multi type branch'
Require-Text $pattern 'SAMPLER_MULTI_MAX_VOICES_PER_TRACK' 'Pattern Multi budget is not capped at 8'
Require-Text $pattern 'param_registry_apply_track_value(id, track, value)' 'Pattern restore does not use the canonical setter'
Require-Text $pattern 'param_registry_get_track_value(PARAM_CFG_POLY_VOICES, track, &value)' 'Pattern canonicalization does not read VOICES'
Require-Text $pattern 'param_registry_get_track_value(PARAM_CFG_POLY_SPREAD, track, &value)' 'Pattern canonicalization does not read SPREAD'
Forbid-Text $pattern 'if (id == PARAM_CFG_POLY_VOICES)`n            {`n                continue;' 'Pattern still skips VOICES during restore'
Forbid-Text $pattern 'synth_polyphony_set_voice_count(track, ctx->voice_count[track])' 'Pattern restores Multi through Synth authority'

Require-Text $snapshotHeader 'uint8_t poly_voice_count;' 'Snapshot voice field is not generic'
Require-Text $snapshotHeader 'float poly_spread;' 'Snapshot spread field is not generic'
Require-Text $snapshot 'brick6_sampler_runtime_get_multi_voice_count(track)' 'Snapshot capture does not read Multi VOICES'
Require-Text $snapshot 'brick6_sampler_runtime_get_multi_spread(track)' 'Snapshot capture does not read Multi SPREAD'
Require-Text $snapshot 'param_registry_apply_track_value(PARAM_CFG_POLY_VOICES' 'Snapshot restore does not use the canonical VOICES setter'
Require-Text $snapshot 'param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD' 'Snapshot restore does not use the canonical SPREAD setter'
Forbid-Text $snapshot 'synth_polyphony_set_voice_count(target_track, applied_voice_count)' 'Snapshot restore still directly writes Synth VOICES'
Forbid-Text $snapshot 'synth_polyphony_set_spread(target_track, snapshot->poly_spread)' 'Snapshot restore still directly writes Synth SPREAD'

'pattern_multi_polyphony_step3_validation=PASS pattern_sound_track_valid=canonical snapshot_fields=generic multi_setter=canonical synth_budget=isolated runtime_handles=absent'
