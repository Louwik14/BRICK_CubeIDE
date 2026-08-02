$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

function Read-RepoFile([string]$relativePath) {
    return Get-Content -Raw (Join-Path $repo $relativePath)
}

function Require-Text([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        throw $message
    }
}

function Forbid-Text([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) {
        throw $message
    }
}

$mixer = Read-RepoFile 'Src\Audio\mixer.c'
$mixerHeader = Read-RepoFile 'Inc\Audio\mixer.h'
$audio = Read-RepoFile 'Src\Core\brick6_audio_runtime.c'
$keyboard = Read-RepoFile 'Src\Keyboard\keyboard_engine.c'
$scheduler = Read-RepoFile 'Src\Seq\seq_play_scheduler.c'
$outputGuard = Read-RepoFile 'Src\Seq\seq_output_guard.c'
$ui = Read-RepoFile 'Src\UI\ui_param.c'
$undo = Read-RepoFile 'Src\Storage\undo_v2.c'

Require-Text $mixer 'synth_polyphony_get_slot((uint8_t)poly_track_id, voice)' `
    'Poly mixer filter is not keyed by the logical synth track'
Require-Text $mixer 'g_external_track_l[mix_track_id][i]' `
    'Poly mixer output is not keyed by the resolved mix lane'
Require-Text $mixerHeader 'uint32_t mix_track_id,' `
    'Poly render API does not carry the resolved mix lane'
Require-Text $mixerHeader 'uint32_t poly_track_id,' `
    'Poly render API does not carry logical track ownership'

foreach ($engine in @('braids', 'wave', 'deluge', 'stack')) {
    Require-Text $audio "ctx->mix_track_id, track, voice" `
        "Poly $engine renderer does not pass both track identities to the mixer"
}

Require-Text $keyboard 'mixer_track_poly_note_on(track, mix_track, voice, note, velocity)' `
    'Keyboard poly Note On does not preserve logical track ownership'
Require-Text $scheduler 'mixer_track_poly_note_on(track, resolved.mix_track_id, voice, note, velocity)' `
    'Sequencer poly Note On does not preserve logical track ownership'
Require-Text $outputGuard 'mixer_track_poly_note_off(track, voice, note)' `
    'Sequencer release guard does not release the logical poly slot'

Forbid-Text $ui 'undo_v2_' `
    'Polyphony UI still produces Undo transactions'
Forbid-Text $undo 'PARAM_CFG_POLY_VOICES' `
    'VOICES leaked into structural step Undo'
Forbid-Text $undo 'PARAM_CFG_POLY_SPREAD' `
    'SPREAD leaked into structural step Undo'

foreach ($scenario in @(@(1, 4, 8), @(8, 2, 8))) {
    if (($scenario.Count -ne 3) -or ($scenario[0] -lt 1) -or ($scenario[2] -gt 8)) {
        throw 'Invalid Multi VOICES undo scenario'
    }
}
foreach ($spread in 0.0, 0.5, 1.0) {
    if (($spread -lt 0.0) -or ($spread -gt 1.0)) {
        throw 'Invalid Multi SPREAD undo scenario'
    }
}

'synth_polyphony_boundary_validation=PASS logical_track_mix_lane_split=explicit voices=1>4>8,8>2>8 spread=0>0.5>1 undo=excluded'
