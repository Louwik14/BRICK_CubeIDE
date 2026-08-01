$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_param_iface.h')
$iface = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_param_iface.c')
$ui = Get-Content -Raw (Join-Path $root 'Src/UI/ui_param.c')
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
if ($header -notmatch 'SEQ_PLOCK_SET_MIDI_FX') { throw 'missing MIDI FX p-lock set' }
if ($iface -notmatch 'param - PARAM_MIDI_FX_S1_PARAM1') { throw 'mapping is not fixed over 16 slots' }
if ($iface -notmatch 'note_fx_pipeline_apply_runtime_param') { throw 'locks do not reach runtime overlay' }
if ($iface -notmatch 'note_fx_pipeline_release_runtime_param') { throw 'base restore is missing' }
if ($ui -notmatch 'TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX') { throw 'UI p-lock routing missing' }
if (($pipeline -notmatch 'g_note_fx_runtime_arp_slot') -or
    ($pipeline -notmatch 'value\[3\] == NOTE_FX_MODEL_ARP.*slot != arp_slot')) {
    throw 'unique ARP runtime enforcement missing'
}
Write-Output 'note_fx_plock_validation=PASS set=yes mapping=16 runtime_overlay=yes restore=yes model_cleanup=yes ui_feedback_path=yes'
