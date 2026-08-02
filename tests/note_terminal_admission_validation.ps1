$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$midi = Get-Content -Raw (Join-Path $root 'Inc/MIDI/midi.h')
$midiSource = Get-Content -Raw (Join-Path $root 'Src/MIDI/midi.c')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$schedulerHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_play_scheduler.h')
$guard = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_output_guard.c')
$guardHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_output_guard.h')

if ($midi -notmatch 'MIDI_ADMISSION_UART 0x01U' -or
    $midi -notmatch 'MIDI_ADMISSION_USB  0x02U' -or
    $midi -notmatch 'MIDI_NOTE_OFF_RESERVE 16U' -or
    $midi -notmatch 'midi_note_on_admit' -or
    $midi -notmatch 'midi_note_off_admit') {
    throw 'MIDI destination admission API is missing'
}
if ($midiSource -notmatch 'midi_usb_channel_voice_can_admit' -or
    $midiSource -notmatch 'midi_usb_force_note_off' -or
    $midiSource -notmatch 'note_on_admission_refused' -or
    $midiSource -notmatch 'note_off_admission_refused') {
    throw 'MIDI queue admission/reserve is missing'
}

$terminalStart = $scheduler.IndexOf('note_fx_result_t seq_play_scheduler_dispatch_terminal_event')
$terminalEnd = $scheduler.IndexOf('void seq_play_scheduler_dispatch_terminal_note', $terminalStart)
$terminal = $scheduler.Substring($terminalStart, $terminalEnd - $terminalStart)
if ($terminal -notmatch 'g_seq_terminal_admission' -or
    $terminal -notmatch 'seq_play_scheduler_emit_engine_note' -or
    $terminal -notmatch 'midi_note_on_admit' -or
    $terminal -notmatch 'midi_note_off_admit' -or
    $terminal -notmatch 'seq_output_guard_note_on_seen_mask') {
    throw 'terminal ledger does not perform independent internal/MIDI admission'
}
if ($terminal -match 'midi_note_on\(' -or $terminal -match 'midi_note_off\(') {
    throw 'terminal seam still bypasses admission with raw MIDI calls'
}
if ($schedulerHeader -notmatch 'terminal_on_internal_admitted' -or
    $schedulerHeader -notmatch 'terminal_on_midi_refused' -or
    $schedulerHeader -notmatch 'terminal_off_refused') {
    throw 'terminal admission diagnostics are missing'
}
if ($guardHeader -notmatch 'seq_output_guard_note_on_seen_mask' -or
    $guard -notmatch 'record->midi_dest_mask' -or
    $guard -notmatch 'seq_play_scheduler_terminal_reset') {
    throw 'guard does not retain admitted MIDI destinations'
}
if ($scheduler -notmatch '(?s)seq_play_scheduler_emit_engine_note.*?voice == SYNTH_POLYPHONY_NO_VOICE.*?return 0U') {
    throw 'polyphonic internal refusal is not propagated'
}

Write-Output 'note_terminal_admission_validation=PASS ledger=occurrence+generation internal=midi_independent midi_off_reserve=16 guard=mask'
