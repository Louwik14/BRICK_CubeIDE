$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$undo = Get-Content -Raw (Join-Path $repo 'Src\Storage\undo_v2.c')
$undoHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\undo_v2.h')
$edit = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_edit.c')
$clipboard = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core_clipboard.c')
$uiParam = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_param.c')
$liveRec = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_live_rec_session.c')
$applyWrappers = Get-Content -Raw (Join-Path $repo 'Src\Param\param_registry_apply_wrappers.c')
$functional = Get-Content -Raw (Join-Path $repo 'tests\undo_v2_functional_test.c')

foreach ($required in @(
    'undo_v2_begin_snapshot_transaction',
    'undo_v2_capture_snapshot_before',
    'undo_v2_capture_snapshot_after',
    'undo_v2_undo',
    'undo_v2_redo',
    'seq_model_step_plock_collect',
    'pattern_v1_play_step_t',
    'pattern_v1_special_step_t'
)) {
    if (-not ($undo.Contains($required) -or $undoHeader.Contains($required) -or $functional.Contains($required))) {
        throw "Missing Undo/Redo functional contract symbol: $required"
    }
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

foreach ($required in @(
    'seq_edit_begin_snapshot_undo',
    'seq_edit_finish_snapshot_undo',
    'seq_edit_paste_steps',
    'seq_edit_clear_steps',
    'seq_edit_clear_steps_without_undo',
    'undo_v2_snapshot_has_effective_change',
    'return 0U;'
)) {
    if (-not ($edit.Contains($required) -or $undo.Contains($required) -or $liveRec.Contains($required))) {
        throw "Missing Step 2 producer contract: $required"
    }
}

if ($undo -notmatch 'uint8_t undo_v2_param_is_undoable\(param_id_t param_id\)[\s\S]{0,120}return 0U;') {
    throw 'Parameter Undo gate is still enabled'
}

if ($functional -notmatch 'test_play_step_round_trip' -or
    $functional -notmatch 'test_special_round_trip' -or
    $functional -notmatch 'test_copy_paste_scope' -or
    $functional -notmatch 'test_depth_and_branching' -or
    $functional -notmatch 'test_negative_param_contract' -or
    $functional -notmatch 'test_noop_and_atomic_failure') {
    throw 'Functional Undo/Redo test matrix is incomplete'
}

if ($functional -notmatch 'SEQ_PLAY_STEP_MAX_LOCKS' -or
    $functional -notmatch 'flags' -or
    $functional -notmatch 'SEQ_SPECIAL_ACTION_TRIGGER') {
    throw 'Play/Special step payload coverage is incomplete'
}

'undo_v2_step2_static_validation=PASS structural_only=yes non_structural_producers=removed no_op_guard=yes variants=lowcost,premium'
