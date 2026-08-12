$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$edit = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_edit.c')
$editHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_edit.h')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime.c')
$bridge = Get-Content -Raw (Join-Path $root 'Src/UI/ui_core_runtime_bridge.c')
$keyboard = Get-Content -Raw (Join-Path $root 'Src/Keyboard/keyboard_runtime.c')

if ($editHeader -notmatch 'seq_edit_prepare_held_note_capture' -or
    $editHeader -notmatch 'seq_edit_capture_held_note_on' -or
    $editHeader -notmatch 'seq_edit_note_capture_note_off') {
    throw 'note capture API is incomplete'
}

if ($edit -notmatch 'seq_model_step_is_empty\(track, steps\[0\]\)' -or
    $edit -notmatch 'SEQ_EDIT_HELD_CONTENT_MIXED' -or
    $edit -notmatch 'quick_length_applied' -or
    $edit -notmatch 'seq_model_step_is_empty\(track, g_seq_hold_state.step_id\[hall\]\)') {
    throw 'held-step classification does not reuse the existing empty semantics'
}

$replaceStart = $edit.IndexOf('static uint8_t seq_edit_replace_step_play_notes_impl')
$replaceEnd = $edit.IndexOf('uint8_t seq_edit_replace_step_play_notes(', $replaceStart)
if (($replaceStart -lt 0) -or ($replaceEnd -le $replaceStart)) {
    throw 'PLAY replacement implementation is missing'
}
$replace = $edit.Substring($replaceStart, $replaceEnd - $replaceStart)
if ($replace -notmatch 'SEQ_STEP_PLAY_FIELD_NOTE' -or
    $replace -notmatch 'SEQ_STEP_PLAY_FIELD_VELOCITY' -or
    $replace -notmatch 'SEQ_STEP_PLAY_FIELD_MICROTIMING' -or
    $replace -notmatch 'SEQ_STEP_PLAY_FIELD_MICROTIMING,\s*0' -or
    $replace -match 'SEQ_STEP_PLAY_FIELD_LENGTH') {
    throw 'PLAY replacement must replace NOTE/VEL, force MICTIM=0 and preserve LEN'
}
if ($replace -match 'seq_model_step_play_clear\(' -or
    $replace -match 'seq_model_set_step_roll\(') {
    throw 'PLAY replacement must preserve ROLL and unrelated PLAY fields'
}

if ($edit -notmatch 'seq_edit_begin_snapshot_undo\(g_seq_hold_state.note_capture_track' -or
    $edit -notmatch 'seq_edit_replace_step_play_notes_impl[\s\S]*?0U\)' -or
    $edit -notmatch 'seq_edit_finish_snapshot_undo\(g_seq_hold_state.note_capture_undo_open\)') {
    throw 'chord capture is not grouped in one undo transaction'
}

if ($runtime -notmatch 'source != SEQ_LIVE_REC_SRC_INTERNAL' -or
    $runtime -notmatch 'seq_edit_capture_held_note_on\(note, velocity\)' -or
    $runtime -notmatch 'seq_edit_note_capture_note_off\(note\)') {
    throw 'runtime arbitration seam is missing'
}
if ($runtime -notmatch 'seq_edit_note_capture_reset\(\)') {
    throw 'runtime capture reset seam is missing'
}
if ($bridge -notmatch 'seq_edit_note_capture_reset\(\)' -or
    $keyboard -notmatch 'seq_edit_note_capture_reset\(\)') {
    throw 'UI/panic capture reset hooks are missing'
}
if ($edit -notmatch 'seq_edit_finish_snapshot_undo\(g_seq_hold_state.note_capture_undo_open\)') {
    throw 'release of all steps does not finalize the capture undo transaction'
}

Write-Output 'seq_step_note_capture_static_validation=PASS empty-semantics=existing quick-length=priority play=len-preserved mictim=zero undo=grouped live-rec=arbitrated'
