$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$undo = Get-Content -Raw (Join-Path $repo 'Src\Storage\undo_v2.c')
$undoHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\undo_v2.h')
$edit = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_edit.c')
$clipboard = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core_clipboard.c')
$uiParam = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_param.c')
$liveRec = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_live_rec_session.c')
$applyWrappers = Get-Content -Raw (Join-Path $repo 'Src\Param\param_registry_apply_wrappers.c')
$patternLive = Get-Content -Raw (Join-Path $repo 'Src\Storage\pattern_live_ram.c')
$runtimeBridge = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core_runtime_bridge.c')
$functional = Get-Content -Raw (Join-Path $repo 'tests\undo_v2_functional_test.c')
$production = (Get-ChildItem @((Join-Path $repo 'Src'), (Join-Path $repo 'Inc')) -Recurse -File |
    Where-Object { $_.Extension -in @('.c', '.h') } |
    ForEach-Object { Get-Content -Raw $_.FullName }) -join "`n"

foreach ($required in @(
    'undo_v2_begin_sequence_transaction',
    'undo_v2_commit_sequence_transaction',
    'undo_v2_undo',
    'undo_v2_redo',
    'undo_v2_invalidate_history',
    'UNDO_V2_MAX_TRANSACTIONS 8U',
    'seq_step_snapshot_can_apply_list',
    'undo_v2_exchange_transaction'
)) {
    if (-not ($undo.Contains($required) -or $undoHeader.Contains($required) -or $functional.Contains($required))) {
        throw "Missing Undo/Redo functional contract symbol: $required"
    }
}

if ($patternLive -notmatch 'pattern_live_apply_snapshot[\s\S]*undo_v2_invalidate_history\(\)[\s\r\n]*;[\s\r\n]*return 1U;') {
    throw 'Successful Pattern/Project snapshot application does not centrally invalidate Undo history'
}
if ($runtimeBridge -match 'undo_v2_undo\(\)[\s\S]{0,240}pattern_live_last_voice_limited') {
    throw 'Structural Undo still reports legacy Pattern voice-limit feedback'
}

if ($uiParam.Contains('undo_v2_') -or
    $liveRec.Contains('undo_v2_') -or
    $applyWrappers.Contains('undo_v2_') -or
    $clipboard.Contains('undo_v2_')) {
    throw 'Non-structural Undo producer remains in UI, live recording, parameter wrappers or Track/Page clipboard'
}

if ($edit.Contains('undo_v2_begin_transaction') -or
    $edit.Contains('undo_v2_record_plock_change') -or
    $edit.Contains('undo_v2_record_param_change') -or
    $edit.Contains('seq_model_set_step_roll(track, step') -and $edit.Contains('undo_v2_begin_snapshot_transaction')) {
    throw 'Non-structural seq_edit producer remains undoable'
}

if (($edit -notmatch 'seq_clipboard_collect_paste_targets[\s\S]*seq_edit_begin_snapshot_undo\(track, paste_targets, paste_target_count\)') -or
    ($edit -match 'seq_edit_begin_snapshot_undo\(track,[\s\r\n]*dest_steps,[\s\r\n]*dest_count\)')) {
    throw 'Paste Undo does not capture the exact clipboard-derived destination steps'
}

foreach ($required in @(
    'seq_edit_begin_snapshot_undo',
    'seq_edit_finish_snapshot_undo',
    'seq_edit_paste_steps',
    'seq_clipboard_collect_paste_targets',
    'seq_edit_clear_steps',
    'seq_edit_clear_steps_without_undo',
    'undo_v2_begin_sequence_transaction',
    'undo_v2_commit_sequence_transaction'
)) {
    if (-not ($edit.Contains($required) -or $undo.Contains($required) -or $liveRec.Contains($required))) {
        throw "Missing Step 2 producer contract: $required"
    }
}

foreach ($forbidden in @(
    'PatternSaveV1',
    'pattern_live_apply_snapshot',
    'undo_v2_record_param_change',
    'undo_v2_record_plock_change',
    'undo_v2_begin_snapshot_transaction',
    'undo_v2_capture_snapshot_before',
    'undo_v2_capture_snapshot_after',
    'undo_v2_source_t',
    'UNDO_V2_SOURCE_',
    'gesture_key',
    'pending_begin_tick',
    'begin_tick',
    'end_tick'
)) {
    if ($undo.Contains($forbidden)) {
        throw "Legacy global Undo payload remains: $forbidden"
    }
}

if (($undo -notmatch 'undo_v2_copy_snapshot_list') -or
    ($undo -match 'transaction->snapshot\s*=\s*g_undo_v2_pending_snapshot') -or
    ($undo -match 'memset\(transaction,\s*0,\s*sizeof\(\*transaction\)\)') -or
    ($undo -match 'undo_v2_clear_pending\(void\)[\s\S]{0,180}memset\(&g_undo_v2_pending_snapshot')) {
    throw 'Single-step Undo still copies or clears the complete fixed-capacity transaction'
}

foreach ($forbidden in @(
    'undo_v2_record_param_change',
    'undo_v2_record_plock_change',
    'undo_v2_record_step_change',
    'undo_v2_begin_snapshot_transaction',
    'undo_v2_param_is_undoable',
    'undo_v2_get_last_status',
    'undo_v2_is_apply_in_progress',
    'undo_v2_is_transaction_open',
    'undo_v2_is_undo_available',
    'undo_v2_is_redo_available'
)) {
    if ($production.Contains($forbidden)) {
        throw "Legacy Undo API remains in production: $forbidden"
    }
}

foreach ($required in @(
    'undo_v2_begin_sequence_transaction(seq_track_id_t track',
    'undo_v2_commit_sequence_transaction',
    'undo_v2_set_capture_suspended'
)) {
    if (-not $undoHeader.Contains($required)) {
        throw "Simplified structural Undo API is incomplete: $required"
    }
}

if ($functional -notmatch 'test_play_step_round_trip' -or
    $functional -notmatch 'test_special_round_trip' -or
    $functional -notmatch 'test_step_snapshot_codec' -or
    $functional -notmatch 'test_copy_paste_scope' -or
    $functional -notmatch 'test_depth_and_branching' -or
    $functional -notmatch 'test_noop_and_atomic_failure') {
    throw 'Functional Undo/Redo test matrix is incomplete'
}

if ($functional -notmatch 'SEQ_PLAY_STEP_MAX_LOCKS' -or
    $functional -notmatch 'SEQ_STEP_ROLL_1_32' -or
    $functional -notmatch 'SEQ_SPECIAL_ACTION_TRIGGER') {
    throw 'Play/Special step payload coverage is incomplete'
}

'undo_v2_step5_static_validation=PASS structural_api=simplified legacy_symbols=removed variants=lowcost,premium'
