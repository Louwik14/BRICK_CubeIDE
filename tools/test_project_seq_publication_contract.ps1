$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$project = Get-Content -Raw (Join-Path $root 'Src/Storage/project_product.c')
$pattern = Get-Content -Raw (Join-Path $root 'Src/Storage/persistent_pattern_control.c')
$live = Get-Content -Raw (Join-Path $root 'Src/Storage/pattern_live_ram.c')

$projectOrder = [regex]::Match($project,
    'persistent_pattern_control_install_into_active_snapshot[\s\S]*?' +
    'seq_engine_control_reset_note_fx_context\(\)[\s\S]*?' +
    'seq_engine_control_flush\(\)[\s\S]*?' +
    'audio_state_snapshot_control_commit\(\)')
if (-not $projectOrder.Success) {
    throw 'Project replacement must publish a reset SEQ generation before AUDIO commit'
}

if ($pattern -notmatch 'if \(resume_transport == 0U\)[\s\S]*?' +
        'seq_engine_control_reset_note_fx_context\(\);[\s\S]*?' +
        'if \(seq_engine_control_flush\(\) == 0U\)') {
    throw 'Stopped Pattern replacement must reset and publish SEQ before AUDIO commit'
}

if ($live -match 'pattern_live_publish_active[\s\S]*?' +
        'seq_engine_control_reset_note_fx_context\(\)') {
    throw 'Project metadata publication must not dirty SEQ after the transaction'
}

Write-Output 'Project/Pattern SEQ publication contract: PASS'
