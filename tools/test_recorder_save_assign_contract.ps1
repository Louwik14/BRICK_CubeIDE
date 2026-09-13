$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$root = Split-Path -Parent $PSScriptRoot
$capture = Get-Content -Raw (Join-Path $root 'Src/Storage/sample_capture.c')
$record = Get-Content -Raw (Join-Path $root 'Src/Storage/SampleCapture/sample_capture_record.inc')
$assign = Get-Content -Raw (Join-Path $root 'Src/Storage/SampleCapture/sample_capture_save_assign.inc')
$control = Get-Content -Raw (Join-Path $root 'Src/Storage/project_control.c')
$ui = Get-Content -Raw (Join-Path $root 'Src/UI/pages/AudioRec/ui_audio_rec_page.inc')

Assert-Contract ($capture.Contains('#define SAMPLE_CAPTURE_REC_DIR "0:/REC"')) `
    'Recorder canonical directory is not 0:/REC'
Assert-Contract ($capture.Contains('SAMPLE_CAPTURE_REC_DIR "/AUDIOREC_TMP.REC"')) `
    'Recorder temporary .REC must share the canonical directory'
Assert-Contract ($capture.Contains('SAMPLE_CAPTURE_REC_DIR "/AUDIOREC_TMP.WAV"')) `
    'Recorder finalized working WAV must share the canonical directory'
Assert-Contract ($record.Contains('"%s/REC%04u.WAV"')) `
    'Final take path must be built from the Recorder directory authority'

Assert-Contract ($control.Contains('project_control_resolve_sample_runtime_kind(')) `
    'Sample runtime resolution must carry asset kind'
Assert-Contract ($control.Contains('PERSIST_ASSET_SAMPLE_RAM,logical,&resolved')) `
    'RAM completion must not use the ambiguous Stream-first resolver'

Assert-Contract ($assign.Contains('entity_topology_get(entity, &topology)')) `
    'ASSIGN candidates must derive from logical entity topology'
Assert-Contract ($assign.Contains('(type == TRACK_TYPE_RAM) || (type == TRACK_TYPE_STREAM)')) `
    'ASSIGN candidates must be limited to RAM and Stream'
Assert-Contract ($assign.Contains('project_control_ram_load_begin(')) `
    'RAM ASSIGN must use project_control preparation'
Assert-Contract ($assign.Contains('project_control_track_asset_select_logical(')) `
    'ASSIGN must commit through the canonical track asset selector'
Assert-Contract ($ui.Contains('sample_capture_model_assign_count() != 0U')) `
    'Zero compatible tracks must skip the ASSIGN popup'
Assert-Contract ($ui.Contains('sample_capture_model_assign_cancel();')) `
    'NO ASSIGN must clear transient assignment state'

Write-Output 'Recorder SAVE/ASSIGN contract tests: PASS'
