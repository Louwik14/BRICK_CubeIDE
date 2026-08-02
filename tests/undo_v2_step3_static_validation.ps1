$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $repo 'Inc\Seq\seq_step_snapshot.h')
$source = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_step_snapshot.c')
$clipboard = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_clipboard.c')
$model = Get-Content -Raw (Join-Path $repo 'Src\Seq\seq_model.c')
$cmake = Get-Content -Raw (Join-Path $repo 'tests\CMakeLists.txt')

foreach ($required in @(
    'seq_step_snapshot_capture',
    'seq_step_snapshot_capture_list',
    'seq_step_snapshot_validate_for_track',
    'seq_step_snapshot_apply',
    'seq_step_snapshot_can_apply_list',
    'seq_step_snapshot_apply_list',
    'seq_step_snapshot_equal',
    'SEQ_STEP_SNAPSHOT_ROLE_PLAY',
    'SEQ_STEP_SNAPSHOT_ROLE_SPECIAL',
    'seq_step_snapshot_plock_t',
    'SEQ_STEP_SNAPSHOT_MAX_STEPS',
    'SEQ_STEP_SNAPSHOT_MAX_LOCKS'
)) {
    if (-not ($header.Contains($required) -or $source.Contains($required))) {
        throw "Missing canonical step snapshot contract: $required"
    }
}

foreach ($required in @(
    'seq_step_snapshot_capture_list',
    'seq_step_snapshot_apply',
    'seq_step_snapshot_t',
    'seq_step_snapshot_sort_locks',
    'seq_step_snapshot_validate_capacity',
    'SEQ_PLOCK_SET_PLAY'
)) {
    if (-not $source.Contains($required) -and -not $clipboard.Contains($required)) {
        throw "Missing canonical codec invariant: $required"
    }
}

if ($source.Contains('malloc') -or $source.Contains('calloc') -or $source.Contains('free')) {
    throw 'Canonical step snapshot codec must not allocate dynamically'
}
if ($clipboard.Contains('seq_clipboard_lock_t') -or $clipboard.Contains('seq_clipboard_paste_locks')) {
    throw 'Clipboard still owns a duplicate p-lock serialization format'
}
if (-not $cmake.Contains('../Src/Seq/seq_step_snapshot.c')) {
    throw 'Host functional tests do not compile the canonical codec'
}
if (($source -notmatch 'seq_model_get_track_plock_count\(track\)') -or
    ($source -match 'for \(seq_step_id_t step = 0U; step < \(seq_step_id_t\)SEQ_MAX_STEPS')) {
    throw 'Single-step snapshot capacity validation still scans all 64 steps'
}
if ($model -notmatch 'capacity - free_count') {
    throw 'Sequence model does not expose its constant-time p-lock usage count'
}

'undo_v2_step3_static_validation=PASS canonical_codec=yes clipboard_shared=yes play_special_separated=yes dynamic_allocations=no'
