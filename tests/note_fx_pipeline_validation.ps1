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
if ($pipeline -notmatch 'seq_play_scheduler_dispatch_terminal_note') { throw 'terminal dispatcher is not shared' }
if ($scheduler -match 'keyboard_arp|PARAM_ARP_|arp_hold_step') { throw 'legacy scheduler ARP remains present' }
if ($keyboard -match 'keyboard_engine_send_note_for_owner_track[\s\S]{0,260}keyboard_engine_emit_note_for_track') { throw 'keyboard retains direct and transformed emission' }
if ($mute -notmatch 'note_fx_pipeline_suspend_track') { throw 'mute lifecycle is not connected' }
if ($params -notmatch 'note_fx_pipeline_before_model_change') { throw 'MODEL transition lacks pre-cleanup' }
if ($pattern -notmatch 'note_fx_pipeline_reset_all_runtime_overrides') { throw 'pattern restore lacks pre-cleanup' }

Write-Output 'note_fx_pipeline_validation=PASS sources=unified terminal=shared sample_domain=yes lifecycle=yes legacy_arp=purged'
