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

$store = Read-RepoFile 'Inc\Param\param_store.h'
$runtimeHeader = Read-RepoFile 'Inc\Core\track_runtime.h'
$runtime = Read-RepoFile 'Src\Core\track_runtime.c'
$seq = Read-RepoFile 'Src\Seq\seq_param_iface.c'
$macro = Read-RepoFile 'Src\Param\param_macro.c'
$mod = Read-RepoFile 'Src\Mod\mod_destination_catalog.c'
$registry = Read-RepoFile 'Src\Param\param_registry.c'
$ui = Read-RepoFile 'Src\UI\ui_param.c'
$pattern = Read-RepoFile 'Src\Storage\pattern_live_ram.c'
$snapshot = Read-RepoFile 'Src\Core\track_snapshot.c'
$snapshotHeader = Read-RepoFile 'Inc\Core\track_snapshot.h'
$kit = Read-RepoFile 'Src\Storage\kit_v1.c'
$kitHeader = Read-RepoFile 'Inc\Storage\kit_v1.h'

Require-Text $store 'PARAM_CFG_POLY_VOICES = 16' 'VOICES canonical ID changed'
Require-Text $store 'PARAM_CFG_POLY_SPREAD = 17' 'SPREAD canonical ID changed'
$paramEnum = [regex]::Match($store, '(?s)enum\s*\{(?<body>.*?)\};').Groups['body'].Value
$paramOrdinal = 0
foreach ($match in [regex]::Matches($paramEnum, '(?m)^\s*(PARAM_[A-Z0-9_]+)\s*(?:=\s*([0-9]+))?\s*,')) {
    if ($match.Groups[2].Success) {
        $paramOrdinal = [int]$match.Groups[2].Value
    }
    if ($match.Groups[1].Value -eq 'PARAM_CFG_POLY_VOICES' -and $paramOrdinal -ne 16) {
        throw "VOICES numeric ID changed: $paramOrdinal"
    }
    if ($match.Groups[1].Value -eq 'PARAM_CFG_POLY_SPREAD' -and $paramOrdinal -ne 17) {
        throw "SPREAD numeric ID changed: $paramOrdinal"
    }
    $paramOrdinal++
}
Require-Text $runtimeHeader 'TRACK_RUNTIME_PARAM_DOMAIN_CFG = 1' 'CFG runtime domain is missing'
Require-Text $runtime 'case PARAM_CFG_POLY_VOICES:' 'VOICES runtime rule is missing'
Require-Text $runtime 'rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_CFG;' 'CFG rule is missing'
Require-Text $runtime 'rule.resource = TRACK_RUNTIME_RESOURCE_POLYPHONY;' 'CFG is not bound to polyphony authority'
if ($runtime -match '(?s)case PARAM_SEQ_PLAY_V4_MICTIM:.*?case PARAM_CFG_POLY_VOICES:.*?rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_PLAY') {
    throw 'VOICES/SPREAD still fall through the PLAY rule'
}

Require-Text $seq 'rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG' 'Sequence interface has no CFG guard'
Require-Text $seq '(rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)' 'Sequence slot matcher accepts CFG'
Require-Text $macro 'case TRACK_RUNTIME_PARAM_DOMAIN_CFG:' 'Macro lock interface has no CFG guard'
Require-Text $mod 'if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)' 'Modulation catalog has no CFG guard'
Require-Text $ui 'case TRACK_RUNTIME_PARAM_DOMAIN_CFG:' 'UI sequence resolver has no CFG guard'
Forbid-Text $ui 'if (param == PARAM_CFG_POLY_VOICES) return 0U;' 'Stale VOICES-only UI guard remains'
Require-Text $ui '&& (param != PARAM_CFG_POLY_VOICES)) return 0U;' 'Structural undo guard changed unexpectedly'
Forbid-Text $ui '&& (param != PARAM_CFG_POLY_SPREAD)) return 0U;' 'SPREAD remains on the structural undo path'

Require-Text $registry 'if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))' 'Polyphony IDs are not handled together'
Require-Text $registry 'return param_apply_non_filter_track_value_rt_fast(id, track, clamped);' 'RT apply path changed unexpectedly'
Require-Text $registry 'if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)' 'Runtime CFG rejection is missing'
Require-Text $registry 'synth_polyphony_set_spread(track, clamped);' 'SPREAD does not use synth_polyphony authority'

Require-Text $pattern '|| (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)' 'Pattern sound domain does not own CFG'
Forbid-Text $pattern 'else if (pattern->mix.track_valid[track][PARAM_CFG_POLY_VOICES]' 'Pattern still has a MIX voice fallback'
Require-Text $pattern 'pattern_live_is_param_in_mix_domain(id) != 0U' 'Pattern MIX restore is not domain-gated'
Require-Text $pattern 'g_current_pattern.sound.track_values[track][PARAM_CFG_POLY_SPREAD] =' 'Pattern does not canonicalize SPREAD'

Require-Text $snapshotHeader 'uint8_t poly_voice_count;' 'Track snapshot does not carry generic polyphony voice count'
Require-Text $snapshotHeader 'float poly_spread;' 'Track snapshot does not carry generic SPREAD'
Require-Text $snapshot 'out_snapshot->poly_spread = synth_polyphony_get_spread(track);' 'Track snapshot does not capture Synth SPREAD'
Require-Text $snapshot 'out_snapshot->poly_spread = brick6_sampler_runtime_get_multi_spread(track);' 'Track snapshot does not capture Multi SPREAD'
Require-Text $snapshot 'param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD' 'Track snapshot does not restore SPREAD canonically'
Require-Text $kitHeader 'float poly_spread;' 'Kit payload does not carry generic SPREAD'
Require-Text $kit 'dst->poly_spread = synth_polyphony_get_spread(track);' 'Kit capture does not carry Synth SPREAD'
Require-Text $kit 'dst->poly_spread = brick6_sampler_runtime_get_multi_spread(track);' 'Kit capture does not carry Multi SPREAD'
Require-Text $kit 'param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD' 'Kit restore does not carry SPREAD canonically'

Require-Text (Read-RepoFile 'Src\Storage\pattern_sd_bank.c') '#define PATTERN_VERSION    5U' 'Pattern version changed unexpectedly'
Require-Text (Read-RepoFile 'Inc\Storage\project_v1.h') '#define PROJECT_V1_FILE_VERSION    5U' 'Project version changed unexpectedly'
Require-Text (Read-RepoFile 'Inc\Storage\kit_sd_bank.h') '#define KIT_SD_FILE_VERSION 3U' 'Kit version changed unexpectedly'
Require-Text (Read-RepoFile 'Inc\Storage\patch_sd_bank.h') '#define PATCH_SD_FILE_VERSION 3U' 'Patch version changed unexpectedly'

'cfg_polyphony_ownership_validation=PASS domain=CFG p_lock=off modulation=off pattern_sound=canonical ids=unchanged versions=stable'
