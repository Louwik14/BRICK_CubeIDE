$ErrorActionPreference = 'Stop'

function Assert-Contains([string]$Path, [string]$Pattern, [string]$Message) {
    $text = Get-Content -Raw $Path
    if ($text -notmatch $Pattern) { throw $Message }
}

Assert-Contains 'Src/Core/track_mute.c' 'TRACK_TOPOLOGY_ROLE_MASTER:\s*return TRACK_MUTE_KIND_NONE' 'Master must not expose ordinary mute.'
Assert-Contains 'Src/Core/track_mute.c' 'TRACK_RUNTIME_FAMILY_MIDI[\s\S]*TRACK_MUTE_KIND_MIDI' 'MIDI capability dispatch missing.'
Assert-Contains 'Src/Core/track_mute.c' 'TRACK_RUNTIME_FAMILY_EXTERNAL[\s\S]*TRACK_MUTE_KIND_EXTERNAL' 'External capability dispatch missing.'
Assert-Contains 'Src/Core/track_mute.c' 'seq_runtime_set_tracks_muted' 'Sequencer suspension is not owned by central mute.'
Assert-Contains 'Src/Core/track_mute.c' 'keyboard_engine_all_notes_off_for_track' 'Manual notes are not stopped by mute.'
Assert-Contains 'Src/Core/track_mute.c' 'mixer_set_track_mute' 'Audio paths are not faded by central mute.'
Assert-Contains 'Src/Audio/mixer.c' 'mute_gain_current' 'Mixer mute fade state missing.'
Assert-Contains 'Src/Audio/fx_master_macro.c' 'left\[i\] = g_fxmm_dry_l\[i\] \+ \(\(left\[i\] - g_fxmm_dry_l\[i\]\) \* g_fxmm_mute_gain\)' 'FX mute must fade contribution to dry.'
Assert-Contains 'Src/Seq/seq_play_scheduler.c' 'track_mute_should_suppress_note_on' 'Sequencer note-on guard missing.'
Assert-Contains 'Src/Keyboard/keyboard_engine.c' 'track_mute_should_suppress_note_on' 'Keyboard note-on guard missing.'
Assert-Contains 'Src/Keyboard/keyboard_engine.c' 'g_kbd_rec_track_note_count\[owner_track\]\[note\][\s\S]*midi_note_off' 'Exact manual MIDI note release missing.'
Assert-Contains 'Src/Seq/seq_mute_bridge.c' 'seq_play_scheduler_suspend_tracks[\s\S]*seq_play_scheduler_resume_tracks' 'Mute scheduler bridge must purge on both transitions.'

Write-Output 'track mute authority validation: PASS'
