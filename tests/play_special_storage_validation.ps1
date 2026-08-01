$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$patternHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\pattern_live_ram.h')
$pattern = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')
$snapshotHeader = Get-Content -Raw (Join-Path $repo 'Inc\Core\track_snapshot.h')
$snapshot = Get-Content -Raw (Join-Path $repo 'Src\Core\track_snapshot.c')
$seqClipboard = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_clipboard.c')
$uiClipboard = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core_clipboard.c')
$edit = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_edit.c')
$undo = Get-Content -Raw (Join-Path $repo 'Src\Storage\undo_v2.c')
$kitHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\kit_v1.h')
$kit = Get-Content -Raw (Join-Path $repo 'Src\Storage\kit_v1.c')
$patchHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\patch_v1.h')
$patch = Get-Content -Raw (Join-Path $repo 'Src\Storage\patch_v1.c')
$patchBank = Get-Content -Raw (Join-Path $repo 'Src\Storage\patch_sd_bank.c')
$modelHeader = Get-Content -Raw (Join-Path $repo 'Inc\Seq\seq_model.h')
$model = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_model.c')

foreach ($contract in @(
    'pattern_v1_play_step_t',
    'locks[SEQ_PLAY_STEP_MAX_LOCKS]',
    'pattern_v1_special_step_t',
    'locks[SEQ_SPECIAL_STEP_MAX_LOCKS]',
    'pattern_v1_play_track_seq_t play[TRACK_TOPOLOGY_PLAY_TRACK_COUNT]',
    'pattern_v1_special_track_seq_t special['
)) {
    if (-not $patternHeader.Contains($contract)) { throw "Missing heterogeneous Pattern contract: $contract" }
}

if (-not ($pattern.Contains('out_step->action = seq_model_get_special_action(track, step)') -and
          $pattern.Contains('seq_model_set_special_action(track, step, special_step->action)'))) {
    throw 'Pattern/Project save-load does not round-trip Special actions'
}
if (-not ($pattern.Contains('entry.set_id == (uint8_t)SEQ_PLOCK_SET_PLAY') -and
          $pattern.Contains('rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_PLAY'))) {
    throw 'Special Pattern payload can still retain PLAY note data'
}
if (-not ($pattern.Contains('pattern_live_identity_matches_track') -and
          $patternHeader.Contains('track_topology_identity_t identity[SEQ_TRACK_COUNT]'))) {
    throw 'Pattern records are not role-identified and validated'
}

if (-not ($snapshotHeader.Contains('track_snapshot_special_sequence_t') -and
          $snapshotHeader.Contains('track_topology_identity_t identity') -and
          $snapshot.Contains('track_topology_identity_is_compatible'))) {
    throw 'Track snapshots are not role-aware'
}
if (-not ($snapshot.Contains('seq_model_get_special_action') -and
          $snapshot.Contains('seq_model_set_special_action'))) {
    throw 'Track snapshot does not round-trip Special actions'
}
if (-not ($seqClipboard.Contains('source_identity') -and
          $seqClipboard.Contains('track_topology_identity_is_compatible') -and
          $seqClipboard.Contains('src->action'))) {
    throw 'Sequence clipboard does not reject incompatible roles or paste Special actions'
}
if (-not ($uiClipboard.Contains('track_snapshot_apply_ex') -and
          $snapshot.Contains('track_topology_identity_is_compatible'))) {
    throw 'Track clipboard bypasses snapshot role validation'
}
if (-not ($edit.Contains('seq_model_set_special_action(track, step, (uint8_t)SEQ_SPECIAL_ACTION_NONE)') -and
          $edit.Contains('seq_edit_begin_snapshot_undo') -and
          $edit.Contains('seq_edit_finish_snapshot_undo'))) {
    throw 'Special clear and snapshot Undo/Redo are incomplete'
}
if (-not ($undo.Contains('special_step->action != seq_model_get_special_action') -and
          $undo.Contains('pattern_live_apply_snapshot(snapshot, 0U)'))) {
    throw 'Undo/Redo does not validate and restore heterogeneous snapshots'
}
if (-not ($kitHeader.Contains('topology_role') -and $kit.Contains('track_topology_identity_is_compatible'))) {
    throw 'Kit records are not role-aware'
}
if (-not ($patchHeader.Contains('topology_role') -and
          $patch.Contains('track_topology_is_play(target_track) == 0U') -and
          $patch.Contains('track_topology_is_play(track) == 0U'))) {
    throw 'Patch apply does not reject Special targets'
}
foreach ($required in @(
    'case UI_TRACK_FAMILY_MIDI:',
    'case UI_TRACK_FAMILY_EXTERNAL:',
    'UI_TRACK_TYPE_MULTI)) ? 1U : 0U;',
    'patch->meta.family != patch->track.family',
    'PATCH_V1_RESULT_BAD_PATCH'
)) { if (-not $patch.Contains($required)) { throw "Patch Play-only metadata contract missing: $required" } }
if (-not ($patchBank.Contains('patch_sd_family_type_is_play_valid') -and
          $patchBank.Contains('patch_sd_family_type_is_play_valid(hdr->family, hdr->type)'))) {
    throw 'Patch bank still accepts legacy Special metadata as valid headers'
}
foreach ($forbidden in @(
    'PATCH_ASSIGN_FAMILY_INPUT',
    'PATCH_ASSIGN_TYPE_LOOPER',
    'PATCH_ASSIGN_TYPE_AUDIO'
)) {
    $patchUi = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_patch_assign.c')
    if ($patchUi.Contains($forbidden)) { throw "Ghost Patch filter remains: $forbidden" }
}
if ($modelHeader.Contains('seq_project_data_t') -or $model.Contains('g_seq_legacy_project') -or
    (Test-Path (Join-Path $repo 'Src\Seq\seq_persistence.c'))) {
    throw 'Homogeneous sequence persistence adapter or dead persistence code remains'
}

$patternBank = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_sd_bank.c')
$projectHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\project_v1.h')
$kitBankHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\kit_sd_bank.h')
$patchBankHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\patch_sd_bank.h')
if (-not ($patternBank.Contains('#define PATTERN_VERSION    3U') -and
          $projectHeader.Contains('#define PROJECT_V1_FILE_VERSION    3U') -and
          $kitBankHeader.Contains('#define KIT_SD_FILE_VERSION 3U') -and
          $patchBankHeader.Contains('#define PATCH_SD_FILE_VERSION 3U'))) {
    throw 'Current file versions changed despite the no-version-bump contract'
}

'play_special_storage_validation=PASS save_load=yes special_action=yes notes_arp=excluded clipboard_roles=strict clear=yes undo_redo=yes formats=v3'
