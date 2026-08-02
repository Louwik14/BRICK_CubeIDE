$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$topology = Get-Content -Raw (Join-Path $repo 'Inc\Core\track_topology.h')
$runtime = Get-Content -Raw (Join-Path $repo 'Inc\Core\track_runtime.h')
$tone = Get-Content -Raw (Join-Path $repo 'Src\UI\pages\ui_page_template_tone.c')
$flow = Get-Content -Raw (Join-Path $repo 'Src\UI\ui_hall_mode_flow.c')
$audio = Get-Content -Raw (Join-Path $repo 'Src\Core\brick6_audio_runtime.c')
$undo = Get-Content -Raw (Join-Path $repo 'Src\Storage\undo_v2.c')

if ($topology -match 'ROLE_MASTER|ROLE_FX') { throw 'Master/FX track role remains' }
if ($runtime -match 'SPECIAL_MASTER|SPECIAL_FX') { throw 'Master/FX runtime track identity remains' }
if (-not ($tone.Contains('g_ui_template_tone_global_master') -and
          $tone.Contains('ui_page_template_tone_open_global_master'))) {
    throw 'Explicit global Master UI context is absent'
}
if ($flow -notmatch 'if \(hall == 15U\)[\s\S]{0,320}ui_page_template_tone_open_global_master') {
    throw 'SHIFT + STEP 16 global Master shortcut is absent'
}
if ((Test-Path (Join-Path $repo 'Src\Audio\fx_master_macro.c')) -or
    (Test-Path (Join-Path $repo 'Inc\Audio\fx_master_macro.h')) -or
    $audio.Contains('fx_master_macro')) {
    throw 'MacroFX DSP path remains active'
}
if ($undo -match 'PARAM_MASTER|global_master') { throw 'Global Master leaked into Undo' }

'global_master_fx_removal_validation=PASS master=global shortcut=shift+step16 active_track=invariant fx_track=none macrofx_dsp=removed undo=excluded'
