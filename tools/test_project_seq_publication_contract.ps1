$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$project = Get-Content -Raw (Join-Path $root 'Src/Storage/project_product.c')
$pattern = Get-Content -Raw (Join-Path $root 'Src/Storage/persistent_pattern_control.c')
$live = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_live_ram.c')
$audio = Get-Content -Raw (Join-Path $root 'Src/Audio/audio_command_executor.c')
$owner = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_pattern_owner.c')
$port = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine_port_h743.c')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime.c')
$paramIface = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_param_iface.c')

$projectOrder = [regex]::Match($project,
    'persistent_pattern_control_install_into_active_snapshot[\s\S]*?' +
    'seq_engine_control_replace_with_workspace\([\s\S]*?' +
    'audio_state_snapshot_control_commit\(\)')
if (-not $projectOrder.Success) {
    throw 'Project replacement must publish a reset SEQ generation before AUDIO commit'
}

if ($pattern -notmatch '\(resume_transport == 0U\)[\s\S]*?' +
        'seq_engine_control_replace_with_workspace\(workspace\)[\s\S]*?' +
        'seq_engine_control_flush_with_workspace\(workspace\)') {
    throw 'Stopped Pattern replacement must reset and publish SEQ before AUDIO commit'
}

$atomicReplace = [regex]::Match($owner,
    'g_published_generation = g_build_generation;[\s\S]*?' +
    'seq_engine_execution_replace\(g_build_generation\)[\s\S]*?' +
    '__set_PRIMASK\(primask\)')
if (-not $atomicReplace.Success) {
    throw 'SEQ generation publication and mutable execution retirement must be IRQ-atomic'
}

$executionReplace = [regex]::Match($port,
    'void seq_engine_execution_replace[\s\S]*?^\}',
    [System.Text.RegularExpressions.RegexOptions]::Multiline)
foreach ($required in @('g_slot_state', 'g_audio_slot', 'g_ingress_count',
        'g_pending', 'g_force_stopped', 'seq_engine_core_init')) {
    if (-not $executionReplace.Success -or $executionReplace.Value -notmatch $required) {
        throw "SEQ replacement barrier does not retire $required"
    }
}
if ($owner -notmatch 'seq_runtime_live_rec_discard_effective\(\)' -or
    $runtime -notmatch 'void seq_runtime_live_rec_discard_effective') {
    throw 'SEQ replacement must discard pending CONTROL live-rec events'
}
if ($owner -notmatch 'seq_engine_control_replace_with_workspace[\s\S]*?' +
        'seq_param_iface_execution_replace\(\)[\s\S]*?' +
        'seq_engine_control_reset_note_fx_context\(\)') {
    throw 'Global replacement must retire derived parameter-lock ownership before rebuilding SEQ'
}
if ($paramIface -notmatch 'void seq_param_iface_execution_replace[\s\S]*?' +
        'g_seq_param_runtime_locked_bits[\s\S]*?' +
        'g_seq_param_patch_transaction_active = 0U') {
    throw 'Parameter replacement must retire lock bits, caches and patch transaction state'
}
if ($owner -notmatch 'seq_param_iface_slot_is_audio_terminal_supported') {
    throw 'Compiled PARAM terminals must use the AUDIO execution admission contract'
}
if ($port -notmatch 'g_terminal\[i\]\.generation != g_execution_generation[\s\S]*?' +
        'g_terminal\[i\]\.generation != generation') {
    throw 'AUDIO boundary must reject terminal blocks outside the active/published generations'
}

$workspace = Get-Content -Raw (Join-Path $root 'Inc/Storage/persistence_workspace.h')
if ($workspace -notmatch 'persist_codec_project_workspace_t codec_scratch;[\s\S]*?' +
        'persistence_groove_build_workspace_t groove_build;' -or
    $workspace -notmatch 'uint8_t encoded\[PERSISTENCE_PATTERN_ENCODED_MAX_BYTES\];[\s\S]*?' +
        'persistence_groove_build_workspace_t groove_build;') {
    throw 'Project and Pattern restore scratch must be reusable by SEQ groove publication'
}

if ($live -match 'pattern_live_publish_active[\s\S]*?' +
        'seq_engine_control_reset_note_fx_context\(\)') {
    throw 'Project metadata publication must not dirty SEQ after the transaction'
}

$panic = [regex]::Match($audio,
    'static uint8_t audio_command_apply_panic[\s\S]*?^\}',
    [System.Text.RegularExpressions.RegexOptions]::Multiline)
if (-not $panic.Success -or
    $panic.Value -notmatch 'memset\(g_audio_seq_output, 0, sizeof\(g_audio_seq_output\)\)' -or
    $panic.Value -notmatch 'g_audio_seq_track_mask = 0U') {
    throw 'Nested Project PANIC must invalidate the AUDIO SEQ ownership mirror'
}

Write-Output 'Project/Pattern SEQ publication contract: PASS'
