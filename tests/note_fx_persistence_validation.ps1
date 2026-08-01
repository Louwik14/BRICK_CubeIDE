$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$patternH = Get-Content -Raw (Join-Path $root 'Inc/Storage/pattern_live_ram.h')
$patternC = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_live_ram.c')
$snapshotH = Get-Content -Raw (Join-Path $root 'Inc/Core/track_snapshot.h')
$snapshotC = Get-Content -Raw (Join-Path $root 'Src/Core/track_snapshot.c')
$undo = Get-Content -Raw (Join-Path $root 'Src/Storage/undo_v2.c')
$paramH = Get-Content -Raw (Join-Path $root 'Inc/Param/param_store.h')
if ($patternH -notmatch 'note_fx_track_state_t note_fx\[NOTE_FX_TRACK_COUNT\]') { throw 'Pattern lacks MIDI FX bases' }
if ($patternC -notmatch 'note_fx_state_capture_track' -or $patternC -notmatch 'note_fx_state_restore_track') { throw 'Pattern capture/restore incomplete' }
if ($snapshotH -notmatch 'note_fx_track_state_t note_fx') { throw 'Track snapshot lacks MIDI FX' }
if ($snapshotC -notmatch 'note_fx_state_capture_track' -or $snapshotC -notmatch 'note_fx_state_restore_track') { throw 'Track clipboard capture/restore incomplete' }
if ($undo -notmatch 'note_fx_state_restore_track') { throw 'Undo/Redo restore missing' }
if ($paramH -notmatch '#define PARAM_PERSIST_COUNT PARAM_MIDI_FX_S1_PARAM1') { throw 'Patch/Kit exclusion boundary changed' }
if ($patternH -match 'note_fx_engine|phase|token|owned') { throw 'runtime MIDI FX leaked into Pattern' }
Write-Output 'note_fx_persistence_validation=PASS pattern=yes project=via_pattern track_snapshot=yes clipboard=yes undo=yes patch_kit=excluded runtime=excluded'
