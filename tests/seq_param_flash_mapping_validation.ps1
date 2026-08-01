$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$iface = Get-Content -Raw (Join-Path $root 'Src\Seq\seq_param_iface.c')
$header = Get-Content -Raw (Join-Path $root 'Inc\Seq\seq_param_iface.h')

function Assert-Mapping([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Assert-Mapping ($iface -match 'static const seq_param_compact_map_t g_seq_param_param_to_slot\[PARAM_COUNT\]') 'direct mapping is not const or compact'
Assert-Mapping ($iface -match 'static const seq_param_inverse_table_t g_seq_param_inverse_tables\[SEQ_PLOCK_SET_COUNT\]') 'inverse mapping table is not const'
Assert-Mapping ($iface -match 'g_seq_param_param_to_slot\[param_id\]') 'param-to-slot lookup does not use the Flash table'
Assert-Mapping ($iface -match 'g_seq_param_inverse_tables\[set_id\]') 'slot-to-param lookup does not use the inverse tables'
Assert-Mapping ($iface -match '_Static_assert\(\(sizeof\(g_seq_param_param_to_slot\)') 'direct table size assertion missing'

foreach ($legacy in @(
    'g_seq_param_id_to_slot',
    'g_seq_param_slot_to_id',
    'seq_param_iface_rebuild_slot_maps',
    'seq_param_iface_map_param',
    'SEQ_PARAM_ID_UNMAPPED'
)) {
    Assert-Mapping ($iface -notmatch [regex]::Escape($legacy)) "legacy mapping remains in source: $legacy"
    Assert-Mapping ($header -notmatch [regex]::Escape($legacy)) "legacy mapping remains in header: $legacy"
}

Assert-Mapping ($iface -notmatch '256U') 'historical 256-entry capacity remains in the interface'
Assert-Mapping ($iface -notmatch 'memset\([^\r\n]*slot_to') 'mapping reconstruction/reset path remains'

$directEntries = @([regex]::Matches($iface, '(?m)^\s*\[PARAM_[A-Z0-9_]+\]\s*=\s*\{'))
Assert-Mapping ($directEntries.Count -eq 73) "direct mapping entry count changed: $($directEntries.Count)"

foreach ($inverse in @{
    'g_seq_param_env_slot_to_id' = 25
    'g_seq_param_play_slot_to_id' = 16
    'g_seq_param_mod_slot_to_id' = 12
    'g_seq_param_midi_fx_slot_to_id' = 16
    'g_seq_param_mix_slot_to_id' = 4
}.GetEnumerator()) {
    $table = [regex]::Match($iface, "(?s)static const param_id_t $($inverse.Key)\[[^\]]+\]\s*=\s*\{(.*?)\};")
    Assert-Mapping $table.Success "inverse table missing: $($inverse.Key)"
    $actual = @([regex]::Matches($table.Groups[1].Value, 'PARAM_[A-Z0-9_]+')).Count
    Assert-Mapping ($actual -eq $inverse.Value) "$($inverse.Key) entry count changed: $actual"
}

Write-Output 'seq_param_flash_mapping_validation=PASS direct=73 inverse=25,16,12,16,4 legacy=none'
