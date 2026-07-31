$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$boundary = Get-Content (Join-Path $root 'Src/Seq/seq_boundary_engine.c') -Raw
$runtime = Get-Content (Join-Path $root 'Src/Core/track_runtime.c') -Raw

function Require-Match([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Require-Match $boundary 'SEQ_PLOCK_SET_TONE[\s\S]*SEQ_PLOCK_SET_COLORS[\s\S]*SEQ_PLOCK_SET_MOD[\s\S]*SEQ_PLOCK_SET_MIX' 'SEQ LINK whitelist is incomplete'
Require-Match $boundary 'seq_param_iface_slot_to_param\s*\(\s*source_track[\s\S]*seq_param_iface_param_to_slot\s*\(\s*target_track' 'SEQ LINK does not translate source slots by param identity'
Require-Match $boundary 'seq_boundary_engine_set_is_seq_linked\s*\(\s*entry->set_id\s*\)\s*==\s*0U' 'SEQ LINK is not gated by the closed set whitelist'

foreach ($forbidden in @('SEQ_PLOCK_SET_PLAY')) {
    $helper = [regex]::Match($boundary, 'static uint8_t seq_boundary_engine_set_is_seq_linked[\s\S]*?^}', 'Multiline').Value
    if ($helper -match $forbidden) {
        throw "$forbidden must remain outside SEQ LINK"
    }
}

foreach ($engineCatalog in @(
    'g_track_runtime_tone_slots_prism',
    'g_track_runtime_tone_slots_stack',
    'g_track_runtime_tone_slots_wave',
    'g_track_runtime_tone_slots_deluge',
    'g_track_runtime_tone_slots_sampler'
)) {
    Require-Match $runtime $engineCatalog "Missing TONE catalog coverage: $engineCatalog"
}

foreach ($discreteParam in @(
    'PARAM_STACK_OSC1_MODEL',
    'PARAM_STACK_PHASE_RESET',
    'PARAM_WAVE_OSC1_FLIP',
    'PARAM_DELUGE_MODEL'
)) {
    Require-Match $runtime $discreteParam "Missing enum/bool TONE coverage: $discreteParam"
}

Write-Output 'SEQ LINK parameter propagation validation: PASS'
