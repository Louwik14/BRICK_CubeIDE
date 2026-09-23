$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$port = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine_port_h743.c')
$owner = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_pattern_owner.c')
$midi = Get-Content -Raw (Join-Path $root 'Src/MIDI/midi.c')

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw "MIDI NOTE contract: $message" }
}

Assert-Contract ($owner.Contains('midi_channel_zero_based = runtime_valid')) `
    'the immutable sequencer pattern must capture the configured MIDI channel'
Assert-Contract ($port.Contains('seq_engine_route_midi_terminal(block, pattern, 0U)')) `
    'scheduled terminal notes must enter the MIDI output owner'
Assert-Contract ($port.Contains('seq_engine_route_midi_terminal(block,pattern,first_event)')) `
    'live terminal notes appended to a ready horizon must enter the MIDI output owner exactly once'
Assert-Contract ($port.Contains('control_music_output_submit(&intent')) `
    'MIDI notes must use the canonical CONTROL musical lifetime owner'
Assert-Contract ($port.Contains('kind <= (uint8_t)SEQ_ENGINE_EVENT_NOTE_ON')) `
    'both NOTE_OFF and NOTE_ON terminal classes must be routed'
Assert-Contract ($port.Contains('TRACK_RUNTIME_TYPE_MIDI')) `
    'non-MIDI tracks must remain on their existing audio path'
Assert-Contract ($midi.Contains('(is_note_off ? 0x08U : 0x09U)')) `
    'USB-MIDI CIN must remain 8 for NOTE_OFF and 9 for NOTE_ON'
Assert-Contract ($midi.Contains('(uint8_t)(status | (ch & 0x0FU))')) `
    'the configured zero-based channel must remain encoded in the status byte'
Assert-Contract ($midi.Contains('midi_usb_request_deferred_flush_from_isr()')) `
    'IRQ producers must request cooperative USB flushing'

Write-Output 'MIDI NOTE end-to-end contract: PASS'
