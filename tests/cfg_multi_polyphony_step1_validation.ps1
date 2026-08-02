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

$cfg = Read-RepoFile 'Src\UI\pages\ui_page_template_cfg.c'
$runtimeHeader = Read-RepoFile 'Inc\Core\track_runtime.h'
$runtime = Read-RepoFile 'Src\Core\track_runtime.c'
$catalog = Read-RepoFile 'Src\UI\ui_track_catalog.c'
$seq = Read-RepoFile 'Src\Seq\seq_param_iface.c'
$mod = Read-RepoFile 'Src\Mod\mod_destination_catalog.c'
$store = Read-RepoFile 'Inc\Param\param_store.h'

$polyBank = 'PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_CFG_POLY_VOICES, PARAM_CFG_POLY_SPREAD'
Require-Text $cfg 'static const ui_template_family_t g_ui_template_cfg_synth_family' 'Synth CFG template is missing'
Require-Text $cfg 'static const ui_template_family_t g_ui_template_cfg_multi_family' 'Multi CFG template is missing'
Require-Text $cfg $polyBank 'Multi CFG template does not expose VOICES then SPREAD'
Require-Text $cfg 'ui_get_track_family(active_track) == UI_TRACK_FAMILY_SAMPLER' 'Multi CFG resolver family guard is missing'
Require-Text $cfg 'ui_get_track_type(active_track) == UI_TRACK_TYPE_MULTI' 'Multi CFG resolver type guard is missing'
Require-Text $catalog 'UI_TRACK_TYPE_MULTI' 'Multi type is missing from the sampler catalog'

Require-Text $runtimeHeader 'TRACK_RUNTIME_RESOURCE_POLYPHONY' 'Dedicated polyphony resource is missing'
Require-Text $runtime 'case PARAM_CFG_POLY_VOICES:' 'VOICES runtime rule is missing'
Require-Text $runtime 'case PARAM_CFG_POLY_SPREAD:' 'SPREAD runtime rule is missing'
Require-Text $runtime 'rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_CFG;' 'Polyphony CFG domain is missing'
Require-Text $runtime 'rule.resource = TRACK_RUNTIME_RESOURCE_POLYPHONY;' 'Polyphony is still classified as Synth resource'
Require-Text $runtime 'ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI' 'Multi polyphony capability check is missing'
Require-Text $runtime 'TRACK_RUNTIME_FLAG_CAN_SYNTH' 'Synth capability check is missing'
Require-Text $seq 'rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG' 'CFG parameters are not rejected by sequence p-lock routing'
Require-Text $mod 'if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)' 'CFG parameters are not rejected by modulation routing'
Require-Text $store 'PARAM_CFG_POLY_VOICES = 16' 'VOICES ID changed'
Require-Text $store 'PARAM_CFG_POLY_SPREAD = 17' 'SPREAD ID changed'

'cfg_multi_polyphony_step1_validation=PASS template=multi voices_then_spread resource=polyphony synth_capability=isolated cfg_non_plockable=preserved'
