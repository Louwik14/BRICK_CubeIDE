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

$template = Read-RepoFile 'Src\UI\pages\ui_page_template_cfg.c'
$ui = Read-RepoFile 'Src\UI\ui_param.c'
$registry = Read-RepoFile 'Src\Param\param_registry.c'
$pattern = Read-RepoFile 'Src\Storage\pattern_live_ram.c'
$snapshot = Read-RepoFile 'Src\Core\track_snapshot.c'
$patch = Read-RepoFile 'Src\Storage\patch_v1.c'
$patchHeader = Read-RepoFile 'Inc\Storage\patch_v1.h'
$kit = Read-RepoFile 'Src\Storage\kit_v1.c'
$kitHeader = Read-RepoFile 'Inc\Storage\kit_v1.h'
$undo = Read-RepoFile 'Src\Storage\undo_v2.c'
$runtime = Read-RepoFile 'Src\Core\brick6_sampler_runtime.c'
$runtimeHeader = Read-RepoFile 'Inc\Core\brick6_sampler_runtime.h'
$scheduler = Read-RepoFile 'Src\Seq\seq_play_scheduler.c'
$architecture = Read-RepoFile 'docs\architecture\z1_audio_hard_rt_mix.md'

Require-Text $template 'PARAM_CFG_POLY_VOICES, PARAM_CFG_POLY_SPREAD' 'CFG Multi bank does not expose both polyphony parameters'
Require-Text $ui 'BRICK6_SAMPLER_MULTI_MAX_VOICES' 'CFG Multi VOICES bound is missing'
Require-Text $registry 'brick6_sampler_runtime_set_multi_voice_count(track' 'Multi VOICES setter routing is missing'
Require-Text $registry 'brick6_sampler_runtime_set_multi_spread(track' 'Multi SPREAD setter routing is missing'
Require-Text $pattern 'param_registry_apply_track_value(id, track, value)' 'Pattern restore is not canonical'
Require-Text $snapshot 'param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD' 'Snapshot restore is not canonical'
Require-Text $patchHeader 'uint8_t poly_voice_count;' 'Patch payload is not generic'
Require-Text $kitHeader 'float poly_spread;' 'Kit payload is not generic'
Require-Text $patch 'UI_TRACK_TYPE_MULTI' 'Patch Multi routing is missing'
Require-Text $kit 'SAMPLER_MULTI_MAX_VOICES_PER_TRACK' 'Kit Multi budget isolation is missing'
Forbid-Text $undo 'PARAM_CFG_POLY_VOICES' 'VOICES leaked into structural step Undo'
Forbid-Text $undo 'PARAM_CFG_POLY_SPREAD' 'SPREAD leaked into structural step Undo'
Require-Text $runtimeHeader 'SAMPLER_MULTI_MAX_VOICES_PER_TRACK (BRICK6_SAMPLER_MULTI_MAX_VOICES)' 'Multi cap is not frozen at eight'
Require-Text $runtime 'brick6_sampler_runtime_multi_reindex_spread(track_id)' 'Multi spread ranks are not reindexed'
Require-Text $scheduler 'brick6_sampler_runtime_note_off_multi_track_note_token' 'Scheduler does not use tokenized Note Off'
Require-Text $runtimeHeader 'brick6_sampler_runtime_note_off_multi_track_note_all' 'Forced all-occurrence Note Off is not explicit'
Forbid-Text $scheduler 'brick6_sampler_runtime_note_off_multi_track_note(track' 'Ambiguous scheduler Note Off remains'
Require-Text $architecture 'Chemin de configuration polyphonique Multi final' 'Final Multi configuration documentation is missing'

foreach ($voices in 1, 2, 4, 8) {
    if (($voices -lt 1) -or ($voices -gt 8)) { throw 'VOICES scenario outside 1..8' }
}
foreach ($spread in 0.0, 0.5, 1.0) {
    if (($spread -lt 0.0) -or ($spread -gt 1.0)) { throw 'SPREAD scenario outside 0..1' }
}

'multi_polyphony_final_validation=PASS cfg=voices+spread edit_bounds=1..8/0..1 persistence=pattern+snapshot+patch+kit undo=excluded synth_pool=isolated_multi_pool=8 noteoff=tokenized_forced_all'
