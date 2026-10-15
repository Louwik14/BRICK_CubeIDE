$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Select-Victim($Voices) {
    $free = $Voices | Where-Object state -eq 'FREE' | Select-Object -First 1
    if ($null -ne $free) { return $free }
    $release = $Voices | Where-Object state -eq 'RELEASE' |
        Sort-Object age | Select-Object -First 1
    if ($null -ne $release) { return $release }
    return $Voices | Sort-Object age | Select-Object -First 1
}

$root = Join-Path $PSScriptRoot '..'
$seqModel = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_model.h')
$playUi = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/ui_page_template_play.c')
$capacity = Get-Content -Raw (Join-Path $root `
    'Inc/Seq/seq_capacity_contract.h')
$synthHeader = Get-Content -Raw (Join-Path $root `
    'Inc/Track/synth_polyphony.h')
$synth = Get-Content -Raw (Join-Path $root `
    'Src/Track/synth_polyphony.c')
$multi = Get-Content -Raw (Join-Path $root `
    'Src/Audio/Engines/Sampler/sampler_multi_voice.inc')
$cfgUi = Get-Content -Raw (Join-Path $root `
    'Src/UI/pages/ui_page_template_cfg.c')
$command = Get-Content -Raw (Join-Path $root `
    'Inc/ControlRT/control_audio_command.h')
$codecHeader = Get-Content -Raw (Join-Path $root `
    'Inc/Storage/persistent_control_codec.h')
$codec = Get-Content -Raw (Join-Path $root `
    'Src/Storage/persistent_control_codec.c')

Assert-True ($seqModel.Contains('#define SEQ_PLAY_MAX_CAPACITY 4U')) `
    'PLAY storage is not limited to four notes'
Assert-True (-not $playUi.Contains('PLAY 2')) 'PLAY 2 is still exposed'
Assert-True ($capacity.Contains(
    '#define SEQ_PRODUCT_GROUP_MAX_EMITTING_VOICES') -and
    $capacity.Contains('== 35U')) 'GROUP 35-lane proof is missing'
Assert-True ($synthHeader.Contains(
    '#define SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET 16U')) `
    'synth global pool is not 16 voices'
Assert-True ($multi.Contains('SAMPLER_MULTI_MAX_GLOBAL_VOICES') -and
    $multi.Contains('oldest_release') -and $multi.Contains('oldest_held')) `
    'MULTI free/release/active stealing policy is missing'
Assert-True ($synth.Contains('SYNTH_POLY_VOICE_RELEASE') -and
    $synth.Contains('SYNTH_POLY_VOICE_HELD') -and
    $synth.Contains('synth_polyphony_take_displaced_output')) `
    'synth deterministic stealing or stale NOTE_OFF retirement is missing'
Assert-True (-not $cfgUi.Contains('PARAM_CFG_POLY_VOICES')) `
    'track VOICES control is still exposed'
Assert-True (-not $command.Contains('CONTROL_AUDIO_CONFIG_POLY_VOICES') -and
    -not $command.Contains('PROGRAM_ENCODE_VOICES')) `
    'VOICE quota remains in the CONTROL/AUDIO ABI'
Assert-True ($codecHeader.Contains('#define PERSIST_CODEC_VERSION 21U') -and
    -not $codecHeader.Contains('PERSIST_CODEC_LEGACY_VERSION') -and
    -not $codec.Contains('legacy_voice_count')) `
    'v21 current-only persistence contract is missing'

$synthVoices = 0..15 | ForEach-Object {
    [pscustomobject]@{ index = $_; state = 'HELD'; age = ($_ + 1) }
}
$synthVoices[4].state = 'RELEASE'
$synthVoices[11].state = 'RELEASE'
Assert-True ((Select-Victim $synthVoices).index -eq 4) `
    '17th synth NOTE_ON did not prefer the oldest RELEASE voice'
$synthVoices[4].state = 'HELD'
$synthVoices[11].state = 'HELD'
Assert-True ((Select-Victim $synthVoices).index -eq 0) `
    '17th synth NOTE_ON did not select the oldest active voice'

$multiVoices = 0..7 | ForEach-Object {
    [pscustomobject]@{ index = $_; state = 'HELD'; age = ($_ + 10) }
}
$multiVoices[6].state = 'RELEASE'
Assert-True ((Select-Victim $multiVoices).index -eq 6) `
    '9th MULTI NOTE_ON did not prefer RELEASE'
$multiVoices[6].state = 'HELD'
Assert-True ((Select-Victim $multiVoices).index -eq 0) `
    '9th MULTI NOTE_ON did not select the oldest active voice'

Write-Output 'polyphony refactor regressions: PASS'
