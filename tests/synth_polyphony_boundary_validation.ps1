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

Require-Text $ui '&& (param != PARAM_CFG_POLY_VOICES)) return 0U;' `
    'SPREAD still starts a structural snapshot transaction'
Forbid-Text $ui '&& (param != PARAM_CFG_POLY_VOICES)`n            && (param != PARAM_CFG_POLY_SPREAD)' `
    'SPREAD remains on the structural undo path'

'synth_polyphony_boundary_validation=PASS logical_track_mix_lane_split=explicit spread_update=non_structural'
