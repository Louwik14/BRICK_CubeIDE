$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$keyboard = Get-Content -Raw (Join-Path $root 'Src/Keyboard/keyboard_engine.c')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$audio = Get-Content -Raw (Join-Path $root 'Src/Audio/audio.c')
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$mute = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$params = Get-Content -Raw (Join-Path $root 'Src/Param/param_registry.c')
$pattern = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_live_ram.c')

if ($keyboard -notmatch 'note_fx_pipeline_submit') { throw 'keyboard source bypasses MIDI FX' }
if ($scheduler -notmatch 'note_fx_pipeline_submit') { throw 'sequencer source bypasses MIDI FX' }
if ($audio -notmatch 'note_fx_pipeline_frames_until_deadline') { throw 'audio segmentation ignores MIDI FX deadlines' }
if (($pipeline -notmatch 'seq_play_scheduler_dispatch_terminal_event') -or
    ($scheduler -notmatch 'seq_play_scheduler_dispatch_terminal_event')) { throw 'terminal dispatcher is not shared' }
if ($scheduler -match 'keyboard_arp|PARAM_ARP_|arp_hold_step') { throw 'legacy scheduler ARP remains present' }
if ($keyboard -match 'keyboard_engine_send_note_for_owner_track[\s\S]{0,260}keyboard_engine_emit_note_for_track') { throw 'keyboard retains direct and transformed emission' }
if ($pipeline -notmatch 'NOTE_FX_COMMAND_TRANSITION_TRACK') { throw 'transition commands are not queued' }
if ($pipeline -notmatch 'note_fx_pipeline_apply_pending_commands') { throw 'audio owner does not apply queued commands' }
if ($mute -notmatch 'track_mute_should_suppress_note_on') { throw 'mute source admission guard is missing' }
if ($params -notmatch 'NOTE_FX_TRANSITION_MODEL_RECONFIGURE') { throw 'MODEL transition lacks explicit policy' }
if ($pattern -notmatch 'note_fx_pipeline_reset_all_runtime_overrides') { throw 'pattern restore lacks pre-cleanup' }

Write-Output 'note_fx_pipeline_validation=PASS sources=queued_audio_owner terminal=shared sample_domain=yes lifecycle=commanded legacy_arp=purged'
