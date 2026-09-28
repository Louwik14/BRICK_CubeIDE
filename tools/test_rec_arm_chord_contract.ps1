$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$root = Split-Path -Parent $PSScriptRoot
$recorder = Get-Content -Raw (Join-Path $root 'Src/Storage/audio_recorder.c')
$capture = Get-Content -Raw (Join-Path $root 'Src/Storage/SampleCapture/sample_capture_service.inc')
$editor = Get-Content -Raw (Join-Path $root 'Src/Storage/SampleCapture/sample_capture_editor.inc')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime.c')
$core = Get-Content -Raw (Join-Path $root 'Src/UI/ui_core.c')
$flow = Get-Content -Raw (Join-Path $root 'Src/UI/ui_hall_mode_flow.c')

# A zero-frame finalization must be observable as terminal, so STOP cannot
# retain the recorder building slot or the capture model's recording latch.
Assert-Contract ($recorder.Contains('audio_recorder_storage_committed_tail() == 0U') -and
    $recorder.Contains('rec_source_abort_building();')) 'Empty take never releases REC_SOURCE'
Assert-Contract ([regex]::IsMatch($recorder,
    '(?s)AUDIO_RECORDER_STORAGE_TAKE_READY\).*?g_build_stream_registered == 0U.*?register_build_stream\(\);')) `
    'Finalized nonempty take cannot retry publication after an early registration miss'
Assert-Contract ([regex]::IsMatch($recorder,
    '(?s)audio_recorder_get_last_take_client\(.*?\*frames = .*?return 1U;')) `
    'Finalized empty take is hidden from the capture model'

# PATTERN must have an actual producer in the control path for both an
# already-running transport and a transport started after arming.
Assert-Contract ($capture.Contains('sample_capture_service_pattern_trigger();') -and
    $capture.Contains('seq_engine_pattern_cycle_boundary(') -and
    $capture.Contains('sample_capture_start_prepared_at(sample_time)')) `
    'Running PATTERN has no boundary-to-recording path'
Assert-Contract ($runtime.Contains('sample_capture_control_on_transport_start(')) `
    'Transport start does not reach pending PATTERN NOW'
Assert-Contract ($capture.Contains('g_sample_capture.state.quant == SAMPLE_CAPTURE_QUANT_BAR') -and
    $capture.Contains('seq_runtime_get_musical_time(')) `
    'BAR quantization has no musical boundary producer'

# Disarm must release the model-owned ARM latch. Trigger editing is guarded
# by ARM_OFF, and a completed take must also restore that state.
Assert-Contract ($capture.Contains('sample_capture_model_set_arm(SAMPLE_CAPTURE_ARM_OFF);')) `
    'Global REC disarm leaves Audio REC armed'
Assert-Contract ($editor.Contains('g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF')) `
    'Trigger editing guard changed unexpectedly'

# The chord owns the temporary Audio REC page only while TRACK is held.
# Releasing REC first must leave it open until TRACK is released; releasing
# TRACK first must restore the saved page/mode independently of REC.
Assert-Contract ($core.Contains('ui_hall_mode_flow_enter_audio_rec_chord();') -and
    $core.Contains('ui_hall_mode_flow_release_audio_rec_chord();')) `
    'TRACK+REC chord has no entry/release symmetry'
Assert-Contract ($flow.Contains('if (g_lowcost_rec_chord_active != 0U)') -and
    $flow.Contains('ui_hall_mode_flow_close_lowcost_rec();') -and
    $flow.Contains('ui_set_hall_mode(return_mode);')) `
    'Chord exit does not restore the previous LED scene owner'
Assert-Contract ($flow.Contains('if (chord_active == 0U)') -and
    $flow.Contains('sample_capture_model_return_to_audio_rec();')) `
    'Chord exit cancels an armed threshold trigger'

Write-Output 'REC ARM / TRACK+REC contract tests: PASS'
