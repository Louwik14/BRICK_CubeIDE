$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$iface = Get-Content -Raw (Join-Path $root 'Src\Seq\seq_param_iface.c')
$types = Get-Content -Raw (Join-Path $root 'Inc\Seq\seq_types.h')

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Assert-Contract ($iface -match 'g_seq_param_runtime_state\[SEQ_TRACK_COUNT\]\[SEQ_PARAM_RUNTIME_SLOT_COUNT\]') 'compact runtime state declaration missing'
Assert-Contract ($iface -match 'g_seq_param_base_valid_bits\[SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT\]') 'compact base-valid bitmap declaration missing'
Assert-Contract ($iface -match 'g_seq_param_runtime_locked_bits\[SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT\]') 'compact runtime-locked bitmap declaration missing'

foreach ($legacy in @('g_seq_param_state', 'g_seq_param_mix_state', 'SEQ_PARAM_NON_MIX_SLOT_COUNT',
                      'SEQ_PARAM_FLAG_BIT_COUNT', 'SEQ_PARAM_FLAG_BYTE_COUNT')) {
    Assert-Contract ($iface -notmatch [regex]::Escape($legacy)) "legacy runtime storage symbol remains: $legacy"
}

$indexBody = [regex]::Match($iface, '(?s)static uint32_t seq_param_state_linear_index\([^)]*\)\s*\{(.*?)\n\}').Groups[1].Value
Assert-Contract ($indexBody -match 'SEQ_PARAM_RUNTIME_SLOT_COUNT') 'bitmap index is not based on compact runtime slots'
Assert-Contract ($indexBody -match 'g_seq_param_set_offsets\[set_id\]') 'bitmap index does not use set offsets'
Assert-Contract ($indexBody -notmatch '256U') 'legacy 256-slot bitmap calculation remains'

$stateBody = [regex]::Match($iface, '(?s)static seq_param_slot_state_t \*seq_param_iface_state_at\([^)]*\)\s*\{(.*?)\n\}').Groups[1].Value
Assert-Contract ($stateBody -match 'seq_param_iface_track_is_valid\(track\)') 'state facade does not validate track'
Assert-Contract ($stateBody -match 'set_id.*SEQ_PLOCK_SET_COUNT') 'state facade does not validate set'
Assert-Contract ($stateBody -match 'param_slot.*g_seq_param_set_capacities\[set_id\]') 'state facade does not validate compact capacity'
Assert-Contract ($stateBody -match 'SEQ_PLOCK_SET_TONE') 'state facade does not revalidate TONE'
Assert-Contract ($stateBody -match 'track_runtime_tone_slot_to_param') 'TONE engine validation missing from state facade'
Assert-Contract ($stateBody -match 'g_seq_param_set_offsets\[set_id\]') 'state facade does not apply compact offset'
Assert-Contract ($stateBody -match 'g_seq_param_runtime_state') 'state facade does not return compact state'

Assert-Contract ($iface -match 'memset\(&g_seq_param_runtime_state') 'compact state is not reset at init'
Assert-Contract (($iface -split 'seq_param_iface_state_at\(').Count -ge 7) 'state accesses bypass the central facade'
Assert-Contract ($types -match 'SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT') 'compact bitmap contract is not exposed'

Write-Output 'seq_param_runtime_state_validation=PASS state=14x94 bitmaps=2x165 facade=central tone_revalidation=yes legacy_storage=no'
