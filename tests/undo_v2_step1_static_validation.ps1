$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$undo = Get-Content -Raw (Join-Path $repo 'Src\Storage\undo_v2.c')
$undoHeader = Get-Content -Raw (Join-Path $repo 'Inc\Storage\undo_v2.h')
$edit = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_edit.c')
$clipboard = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_core_clipboard.c')
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

# Step 1 freezes the producer inventory. These checks intentionally describe the
# current snapshot implementation; producer removal belongs to Step 2.
foreach ($required in @(
    'seq_edit_begin_snapshot_undo',
    'seq_edit_finish_snapshot_undo',
    'seq_edit_clear_steps',
    'seq_edit_paste_steps',
    'ui_core_clipboard_begin_snapshot_undo',
    'ui_core_clipboard_finish_snapshot_undo'
)) {
    if (-not $edit.Contains($required) -and -not $clipboard.Contains($required)) {
        throw "Step 1 producer inventory changed unexpectedly: $required"
    }
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

'undo_v2_step1_static_validation=PASS producer_inventory=baseline functional_matrix=registered variants=lowcost,premium'
