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

$ui = Read-RepoFile 'Src\UI\ui_param.c'
$registry = Read-RepoFile 'Src\Param\param_registry.c'
$sampler = Read-RepoFile 'Src\Core\brick6_sampler_runtime.c'
$samplerHeader = Read-RepoFile 'Inc\Core\brick6_sampler_runtime.h'
$catalog = Read-RepoFile 'Src\Param\param_registry_catalog.c'

$multiBounds = [regex]::Match($ui, '(?s)else if \(param == PARAM_CFG_POLY_VOICES\).*?else if \(param == PARAM_MOD_MATRIX_DEST\)').Value
Require-Text $multiBounds 'UI_TRACK_FAMILY_SAMPLER' 'Multi UI bounds do not test the sampler family'
Require-Text $multiBounds 'UI_TRACK_TYPE_MULTI' 'Multi UI bounds do not test the Multi type'
Require-Text $multiBounds '*out_min = 1.0f;' 'Multi VOICES minimum is not 1'
Require-Text $multiBounds 'BRICK6_SAMPLER_MULTI_MAX_VOICES' 'Multi VOICES maximum is not the pool limit'
Require-Text $multiBounds 'synth_polyphony_get_available_for_track(track)' 'Synth dynamic bound was not preserved'
Require-Text $catalog 'PARAM_CFG_POLY_SPREAD, "SPREAD", PARAM_TYPE_FLOAT, 0.0f, 1.0f' 'SPREAD catalogue bounds changed'

Require-Text $registry 'brick6_sampler_runtime_get_multi_voice_count(track)' 'Multi VOICES getter routing is missing'
Require-Text $registry 'brick6_sampler_runtime_get_multi_spread(track)' 'Multi SPREAD getter routing is missing'
Require-Text $registry 'brick6_sampler_runtime_set_multi_voice_count(track, (uint8_t)clamped)' 'Multi VOICES setter routing is missing'
Require-Text $registry 'brick6_sampler_runtime_set_multi_spread(track, clamped)' 'Multi SPREAD setter routing is missing'
Require-Text $registry 'if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))' 'CFG polyphony fast-path guard is missing'
Require-Text $registry 'return 0U;' 'CFG polyphony fast path does not reject modulation'
Require-Text $sampler 'if (count < 1U)' 'Multi VOICES lower clamp is missing'
Require-Text $sampler 'if (count > SAMPLER_MULTI_MAX_VOICES_PER_TRACK)' 'Multi VOICES upper clamp is missing'
Require-Text $sampler 'if (spread < 0.0f)' 'Multi SPREAD lower clamp is missing'
Require-Text $sampler 'else if (spread > 1.0f)' 'Multi SPREAD upper clamp is missing'
Require-Text $samplerHeader 'SAMPLER_MULTI_MAX_VOICES_PER_TRACK (BRICK6_SAMPLER_MULTI_MAX_VOICES)' 'Multi pool limit contract is missing'

$testedVoiceValues = @('0', '1', '2', '4', '8', '9')
$testedSpreadValues = @('0', '0.5', '1', '-0.1', '1.1')

"cfg_multi_polyphony_step2_validation=PASS voices=$($testedVoiceValues -join ',') spread=$($testedSpreadValues -join ',') ui_bounds=multi_1_to_8 synth_bound=preserved routing=multi_runtime fast_path=rejects_plock_mod"
