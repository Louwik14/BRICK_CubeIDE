$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$types = Get-Content -Raw (Join-Path $root 'Inc\Seq\seq_types.h')
$header = Get-Content -Raw (Join-Path $root 'Inc\Seq\seq_param_iface.h')
$iface = Get-Content -Raw (Join-Path $root 'Src\Seq\seq_param_iface.c')
$runtime = Get-Content -Raw (Join-Path $root 'Src\Core\track_runtime.c')
$store = Get-Content -Raw (Join-Path $root 'Inc\Param\param_store.h')
$pattern = Get-Content -Raw (Join-Path $root 'Src\Storage\pattern_sd_bank.c')
$project = Get-Content -Raw (Join-Path $root 'Inc\Storage\project_v1.h')
$kit = Get-Content -Raw (Join-Path $root 'Inc\Storage\kit_sd_bank.h')

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

foreach ($contract in (@{
    'SEQ_PARAM_ENV_SLOT_COUNT' = 25
    'SEQ_PARAM_TONE_SLOT_COUNT' = 21
    'SEQ_PARAM_PLAY_SLOT_COUNT' = 16
    'SEQ_PARAM_MOD_SLOT_COUNT' = 12
    'SEQ_PARAM_MIDI_FX_SLOT_COUNT' = 16
    'SEQ_PARAM_MIX_SLOT_COUNT' = 4
    'SEQ_PARAM_RUNTIME_SLOT_COUNT' = 94
}).GetEnumerator()) {
    if ($contract.Key -eq 'SEQ_PARAM_RUNTIME_SLOT_COUNT') {
        Assert-Contract ($types -match '#define\s+SEQ_PARAM_RUNTIME_SLOT_COUNT\s+\(SEQ_PARAM_MIX_SLOT_OFFSET\s*\+\s*SEQ_PARAM_MIX_SLOT_COUNT\)') "$($contract.Key) contract mismatch"
    } else {
        Assert-Contract ($types -match "#define\s+$($contract.Key)\s+$($contract.Value)U") "$($contract.Key) contract mismatch"
    }
}

foreach ($offset in (@{
    'SEQ_PARAM_ENV_SLOT_OFFSET' = 0
    'SEQ_PARAM_TONE_SLOT_OFFSET' = 25
    'SEQ_PARAM_PLAY_SLOT_OFFSET' = 46
    'SEQ_PARAM_MOD_SLOT_OFFSET' = 62
    'SEQ_PARAM_MIDI_FX_SLOT_OFFSET' = 74
    'SEQ_PARAM_MIX_SLOT_OFFSET' = 90
}).GetEnumerator()) {
    Assert-Contract ($header -match "$($offset.Key)\s*==\s*$($offset.Value)U") "$($offset.Key) assertion missing"
}
Assert-Contract ($header -match 'SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT\s*==\s*165U') 'runtime bitmap assertion missing'
Assert-Contract ($header -match 'PARAM_MIDI_FX_S4_MODEL - PARAM_MIDI_FX_S1_PARAM1') 'MIDI FX inverse-table assertion missing'

$excludedBody = [regex]::Match($iface, '(?s)static uint8_t seq_param_iface_is_excluded_from_plock\([^)]*\)\s*\{(.*?)\n\}').Groups[1].Value
Assert-Contract (-not [string]::IsNullOrEmpty($excludedBody)) 'central p-lock exclusion authority missing'
$excluded = @(
    'PARAM_SAMPLER_SLICE_COUNT',
    'PARAM_LOOPER_STRETCH', 'PARAM_LOOPER_PITCH', 'PARAM_LOOPER_GRAIN',
    'PARAM_MOD_MATRIX_SLOT', 'PARAM_MOD_MATRIX_SOURCE', 'PARAM_MOD_MATRIX_DEST', 'PARAM_MOD_MATRIX_DEPTH',
    'PARAM_MOD_MULTI_1_A', 'PARAM_MOD_MULTI_1_B', 'PARAM_MOD_MULTI_2_A', 'PARAM_MOD_MULTI_2_B',
    'PARAM_MOD_SLEW_1_SOURCE', 'PARAM_MOD_SLEW_1_AMOUNT', 'PARAM_MOD_SLEW_2_SOURCE', 'PARAM_MOD_SLEW_2_AMOUNT'
)
foreach ($param in $excluded) {
    Assert-Contract ($excludedBody -match [regex]::Escape("case ${param}:")) "central exclusion missing: $param"
}
foreach ($param in @('PARAM_LOOPER_ARM', 'PARAM_LOOPER_LEN', 'PARAM_LOOPER_PLAY', 'PARAM_LOOPER_XFADE')) {
    Assert-Contract ($excludedBody -notmatch [regex]::Escape("case ${param}:")) "Looper p-lock decision unexpectedly excludes: $param"
}
Assert-Contract ($iface -match 'seq_param_iface_is_param_plockable\(param\)') 'mapping validation does not use central p-lock authority'
Assert-Contract ($iface -match 'g_seq_param_param_to_slot\[param_id\]') 'direct mapping table is not used for param-to-slot lookup'
Assert-Contract ($iface -match 'g_seq_param_inverse_tables\[set_id\]') 'inverse mapping tables are not used for slot-to-param lookup'
Assert-Contract ($iface -match 'TONE slots are selected by the active engine') 'generic TONE mapping guard missing'

$groups = [regex]::Matches($runtime, '(?s)((?:\s*case PARAM_[A-Z0-9_]+:)+)\s*rule\.domain = TRACK_RUNTIME_PARAM_DOMAIN_([A-Z_]+);')
$domains = @{}
foreach ($group in $groups) {
    foreach ($case in [regex]::Matches($group.Groups[1].Value, 'case (PARAM_[A-Z0-9_]+):')) {
        $domains[$case.Groups[1].Value] = $group.Groups[2].Value
    }
}
$enumBody = [regex]::Match($store, '(?s)enum\s*\{(.*?)\sPARAM_COUNT').Groups[1].Value
$enumParams = @([regex]::Matches($enumBody, 'PARAM_[A-Z0-9_]+') | ForEach-Object { $_.Value })
$mix = @('PARAM_MIX_LEVEL', 'PARAM_MIX_PAN', 'PARAM_MIX_SEND1', 'PARAM_MIX_SEND2')
$counts = @{ ENV = 0; PLAY = 0; MOD = 0; MIDI_FX = 0; MIX = $mix.Count }
$slots = @{ ENV = @{}; PLAY = @{}; MOD = @{}; MIDI_FX = @{}; MIX = @{} }
$next = @{ ENV = 0; PLAY = 0; MOD = 0; MIDI_FX = 0 }
foreach ($param in $enumParams) {
    if ($excluded -contains $param) { continue }
    $domain = $domains[$param]
    if ($domain -notin @('ENV', 'PLAY', 'MOD', 'MIDI_FX')) { continue }
    $slots[$domain][$param] = $next[$domain]
    $next[$domain]++
}
for ($i = 0; $i -lt $mix.Count; ++$i) { $slots.MIX[$mix[$i]] = $i }

foreach ($expected in (@{
    ENV = 25; PLAY = 16; MOD = 12; MIDI_FX = 16; MIX = 4
}).GetEnumerator()) {
    $actual = if ($expected.Key -eq 'MIX') { $counts.MIX } else { $next[$expected.Key] }
    Assert-Contract ([bool]($actual -eq $expected.Value)) "$($expected.Key) mapped count mismatch"
}
foreach ($set in @('ENV', 'PLAY', 'MOD', 'MIDI_FX')) {
    $values = @($slots[$set].Values | Sort-Object)
    $isBijective = ($values.Count -eq $next[$set]) -and (($values -join ',') -eq ((0..($next[$set] - 1)) -join ','))
    Assert-Contract ([bool]$isBijective) "$set mapped slots are not bijective and contiguous"
}
$mixTable = [regex]::Match($iface, '(?s)g_seq_param_mix_slot_to_id\[[^\]]+\]\s*=\s*\{(.*?)\};').Groups[1].Value
$mixActual = @([regex]::Matches($mixTable, 'PARAM_[A-Z0-9_]+') | ForEach-Object { $_.Value })
Assert-Contract (($mixActual -join ',') -eq ($mix -join ',')) 'MIX inverse table mismatch'

$toneArrays = [regex]::Matches($runtime, '(?s)static const param_id_t (g_track_runtime_tone_slots_[a-z0-9_]+)\[\]\s*=\s*\{(.*?)\};')
Assert-Contract ($toneArrays.Count -eq 12) "TONE engine table coverage changed: $($toneArrays.Count)"
$toneCounts = @{}
foreach ($array in $toneArrays) {
    $count = @([regex]::Matches($array.Groups[2].Value, 'PARAM_[A-Z0-9_]+')).Count
    $toneCounts[$array.Groups[1].Value] = $count
    Assert-Contract ($count -le 21) "$($array.Groups[1].Value) exceeds TONE capacity"
}
Assert-Contract ($toneCounts['g_track_runtime_tone_slots_stack'] -eq 21) 'STACK does not define the TONE maximum'
foreach ($type in @('PRISM','STACK','WAVE','DELUGE','RAM','STREAM','LOOPER','MULTI','MIDI','EXTERNAL','SPECIAL_FX','DRUM_BD_ANALOG','DRUM_MD')) {
    Assert-Contract ($runtime -match "TRACK_RUNTIME_TYPE_$type") "TONE runtime type is not covered: $type"
}

Assert-Contract ($pattern -match '#define PATTERN_VERSION\s+5U') 'Pattern p-lock format version not bumped'
Assert-Contract ($project -match '#define PROJECT_V1_FILE_VERSION\s+5U') 'Project p-lock format version not bumped'
Assert-Contract ($kit -match '#define KIT_SD_FILE_VERSION 3U') 'Kit version changed although Kit has no p-lock slot payload'
Assert-Contract ($header -match 'compact') 'compact slot contract is not documented in the interface'

Write-Output "seq_param_compact_contract_validation=PASS env=25 tone_max=21 play=16 mod=12 midi_fx=16 mix=4 runtime=94 bitmap=165 pattern=v5 project=v5 kit=v3"
