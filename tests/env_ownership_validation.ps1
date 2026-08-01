$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$runtime = Get-Content -Raw (Join-Path $root 'Src/Core/track_runtime.c')
$iface = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_param_iface.c')
$types = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_types.h')
$store = Get-Content -Raw (Join-Path $root 'Inc/Param/param_store.h')
$macro = Get-Content -Raw (Join-Path $root 'Src/Param/param_macro.c')
$catalog = Get-Content -Raw (Join-Path $root 'Src/Mod/mod_destination_catalog.c')
$clipboard = Get-Content -Raw (Join-Path $root 'Src/UI/ui_core_clipboard.c')
$snapshot = Get-Content -Raw (Join-Path $root 'Src/Core/track_snapshot.c')
$pattern = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_live_ram.c')
$kit = Get-Content -Raw (Join-Path $root 'Src/Storage/kit_v1.c')
$project = Get-Content -Raw (Join-Path $root 'Inc/Storage/project_v1.h')
$patternBank = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_sd_bank.c')
$kitBank = Get-Content -Raw (Join-Path $root 'Inc/Storage/kit_sd_bank.h')

$domains = @{}
$groups = [regex]::Matches($runtime, '(?s)((?:\s*case PARAM_[A-Z0-9_]+:)+)\s*rule\.domain = TRACK_RUNTIME_PARAM_DOMAIN_([A-Z_]+);')
foreach ($group in $groups) {
    $domain = $group.Groups[2].Value
    foreach ($case in [regex]::Matches($group.Groups[1].Value, 'case (PARAM_[A-Z0-9_]+):')) {
        $domains[$case.Groups[1].Value] = $domain
    }
}

$vca = @('PARAM_VCA_ATTACK','PARAM_VCA_DECAY','PARAM_VCA_SUSTAIN','PARAM_VCA_RELEASE','PARAM_ENV_RETRIG_VCA')
$env3 = @('PARAM_ENV3_ATTACK','PARAM_ENV3_DECAY','PARAM_ENV3_SUSTAIN','PARAM_ENV3_RELEASE','PARAM_ENV_RETRIG_MOD')
$filter = @('PARAM_FILTER_TYPE','PARAM_FILTER_CUTOFF','PARAM_FILTER_RESONANCE','PARAM_FILTER_EG_AMT',
            'PARAM_FILTER_ATTACK','PARAM_FILTER_DECAY','PARAM_FILTER_SUSTAIN','PARAM_FILTER_RELEASE',
            'PARAM_FILTER_KEYTRK','PARAM_FILTER_ENVRST','PARAM_FILTER_ENVDLY','PARAM_FILTER_EQ_LOW',
            'PARAM_FILTER_EQ_MID','PARAM_FILTER_EQ_HIGH','PARAM_ENV_RETRIG_FILTER')
foreach ($param in ($filter + $vca + $env3)) {
    if ($domains[$param] -ne 'ENV') { throw "$param does not belong to ENV" }
}
foreach ($param in $vca) {
    if ($domains[$param] -eq 'MIX') { throw "$param still belongs to MIX" }
}
foreach ($param in $env3) {
    if ($domains[$param] -eq 'MOD') { throw "$param still belongs to MOD" }
}

$mixExpected = @('PARAM_MIX_LEVEL','PARAM_MIX_PAN','PARAM_MIX_SEND1','PARAM_MIX_SEND2')
$mixTable = [regex]::Match($iface, '(?s)g_seq_param_mix_slot_to_id\[[^\]]+\]\s*=\s*\{(.*?)\};').Groups[1].Value
$mixActual = @([regex]::Matches($mixTable, 'PARAM_[A-Z0-9_]+') | ForEach-Object { $_.Value })
if (($mixActual -join ',') -ne ($mixExpected -join ',')) { throw "MIX slot table mismatch: $($mixActual -join ',')" }
if ($types -notmatch '#define SEQ_PARAM_MIX_SLOT_COUNT\s+4U') { throw 'MIX contract capacity is not four slots' }

$enumBody = [regex]::Match($store, '(?s)enum\s*\{(.*?)\sPARAM_COUNT\s*[,}]').Groups[1].Value
$enumParams = @([regex]::Matches($enumBody, 'PARAM_[A-Z0-9_]+') | ForEach-Object { $_.Value })
$excluded = @('PARAM_SAMPLER_SLICE_COUNT','PARAM_LOOPER_ARM','PARAM_LOOPER_LEN','PARAM_LOOPER_PLAY',
              'PARAM_LOOPER_STRETCH','PARAM_LOOPER_PITCH','PARAM_LOOPER_GRAIN','PARAM_MOD_MATRIX_SLOT',
              'PARAM_MOD_MATRIX_SOURCE','PARAM_MOD_MATRIX_DEST','PARAM_MOD_MATRIX_DEPTH','PARAM_MOD_MULTI_1_A',
              'PARAM_MOD_MULTI_1_B','PARAM_MOD_MULTI_2_A','PARAM_MOD_MULTI_2_B','PARAM_MOD_SLEW_1_SOURCE',
              'PARAM_MOD_SLEW_1_AMOUNT','PARAM_MOD_SLEW_2_SOURCE','PARAM_MOD_SLEW_2_AMOUNT')
$slots = @{ ENV = @{}; MIX = @{}; MOD = @{} }
$next = @{ ENV = 0; MOD = 0 }
foreach ($param in $enumParams) {
    $domain = $domains[$param]
    if (($domain -eq 'ENV' -or $domain -eq 'MOD') -and ($excluded -notcontains $param)) {
        $slots[$domain][$param] = $next[$domain]
        $next[$domain]++
    }
}
for ($i = 0; $i -lt $mixExpected.Count; ++$i) { $slots.MIX[$mixExpected[$i]] = $i }

$expectedEnvSlots = @{
    PARAM_FILTER_TYPE=0; PARAM_FILTER_CUTOFF=1; PARAM_FILTER_RESONANCE=2; PARAM_FILTER_EG_AMT=3;
    PARAM_FILTER_ATTACK=4; PARAM_FILTER_DECAY=5; PARAM_FILTER_SUSTAIN=6; PARAM_FILTER_RELEASE=7;
    PARAM_FILTER_KEYTRK=8; PARAM_FILTER_ENVRST=9; PARAM_FILTER_ENVDLY=10; PARAM_FILTER_EQ_LOW=11;
    PARAM_FILTER_EQ_MID=12; PARAM_FILTER_EQ_HIGH=13; PARAM_VCA_ATTACK=14; PARAM_VCA_DECAY=15;
    PARAM_VCA_SUSTAIN=16; PARAM_VCA_RELEASE=17; PARAM_ENV3_ATTACK=18; PARAM_ENV3_DECAY=19;
    PARAM_ENV3_SUSTAIN=20; PARAM_ENV3_RELEASE=21; PARAM_ENV_RETRIG_FILTER=22;
    PARAM_ENV_RETRIG_VCA=23; PARAM_ENV_RETRIG_MOD=24
}
foreach ($param in $expectedEnvSlots.Keys) {
    if ($slots.ENV[$param] -ne $expectedEnvSlots[$param]) {
        throw "$param ENV slot mismatch: expected $($expectedEnvSlots[$param]), got $($slots.ENV[$param])"
    }
}
if ($next.ENV -gt 256) { throw "ENV capacity exceeded: $($next.ENV)/256" }
if ($next.MOD -ne 12) { throw "MOD must contain exactly the twelve LFO parameters, got $($next.MOD)" }

if ($macro -notmatch 'TRACK_RUNTIME_PARAM_DOMAIN_ENV[\s\S]*SEQ_PLOCK_SET_ENV') { throw 'Macro ENV set routing missing' }
if (($catalog -notmatch 'domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV') -or
    ($catalog -notmatch 'mod_destination_is_direct_vca\(dest\)') -or
    ($catalog -notmatch 'PARAM_ENV3_ATTACK.*PARAM_ENV3_RELEASE')) { throw 'ENV modulation destination routing incomplete' }
if (($clipboard -notmatch 'ui_core_clipboard_collect_ensemble_params') -or
    ($clipboard -notmatch 'ui_core_clipboard_clear_param_list_to_min')) { throw 'Clipboard/clear family path missing' }
if (($snapshot -notmatch 'memcpy\(&out_snapshot->sound, sound') -or
    ($snapshot -notmatch 'memcpy\(dst_sound, &snapshot->sound')) { throw 'Track snapshot sound round-trip missing' }
if (($pattern -notmatch 'rule\.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV') -or
    ($pattern -notmatch 'out_pattern->sound\.track_values') -or
    ($pattern -notmatch 'ctx->pattern->sound\.track_valid')) { throw 'Pattern ENV sound round-trip missing' }
if (($kit -notmatch 'memcpy\(&dst->sound, sound') -or
    ($kit -notmatch 'memcpy\(dst_sound, &src->sound') -or
    ($kit -notmatch 'rule\.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV')) { throw 'Kit ENV sound round-trip missing' }
if (($project -notmatch 'PatternSaveV1 live') -or
    ($project -notmatch '#define PROJECT_V1_FILE_VERSION\s+5U') -or
    ($patternBank -notmatch '#define PATTERN_VERSION\s+5U') -or
    ($kitBank -notmatch '#define KIT_SD_FILE_VERSION 3U')) { throw 'Persistence version contract mismatch' }

Write-Output "env_ownership_validation=PASS env_slots=$($next.ENV)/256 mix_slots=4 mod_slots=$($next.MOD) pattern_project=v5 kit=v3"
