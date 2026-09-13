$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$root = Split-Path -Parent $PSScriptRoot
$recorder = Get-Content -Raw (Join-Path $root 'Src/Storage/audio_recorder.c')
$capture = Get-Content -Raw (Join-Path $root 'Src/Storage/SampleCapture/sample_capture_editor.inc')
$block = Get-Content -Raw (Join-Path $root 'Src/SD/sd_block_device.c')
$reservationHeader = Get-Content -Raw (Join-Path $root 'Inc/Storage/recorder_file_reservation.h')
$reservation = Get-Content -Raw (Join-Path $root 'Src/Storage/recorder_file_reservation.c')
$adapter = Get-Content -Raw (Join-Path $root 'Src/Storage/generic_recorder_adapters.c')
$generic = Get-Content -Raw (Join-Path $root 'Src/Storage/generic_recorder.c')
$storage = Get-Content -Raw (Join-Path $root 'Src/Storage/audio_recorder_storage.c')

$busyStates = '(?s)AUDIO_RECORDER_STATE_RECORDING\).*?' +
    'AUDIO_RECORDER_STATE_DRAINING\).*?' +
    'AUDIO_RECORDER_STATE_FINALIZING\)\)\s*' +
    'return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;'
Assert-Contract ([regex]::IsMatch($recorder, $busyStates)) `
    'Recorder discard must classify RECORDING/DRAINING/FINALIZING as NOT_NOW'

$armStart = $capture.IndexOf('uint8_t sample_capture_model_set_arm(')
$armEnd = $capture.IndexOf('uint8_t sample_capture_model_step_arm(', $armStart)
Assert-Contract (($armStart -ge 0) -and ($armEnd -gt $armStart)) `
    'ARM model function not found'
$armFunction = $capture.Substring($armStart, $armEnd - $armStart)
$busyIndex = $armFunction.IndexOf('if (discarded == AUDIO_RECORDER_LIFECYCLE_NOT_NOW)')
$mutationIndex = $armFunction.IndexOf('g_sample_capture.state.arm = arm;')
Assert-Contract (($busyIndex -ge 0) -and ($mutationIndex -gt $busyIndex)) `
    'Temporary recorder refusal must precede ARM/session mutations'
$busyBranch = $armFunction.Substring($busyIndex,
    $armFunction.IndexOf('if (discarded == AUDIO_RECORDER_LIFECYCLE_ERROR)', $busyIndex) - $busyIndex)
Assert-Contract (-not $busyBranch.Contains('sample_capture_set_error')) `
    'Temporary recorder refusal must not become a Sample Capture error'
$errorIndex = $armFunction.IndexOf(
    'if (discarded == AUDIO_RECORDER_LIFECYCLE_ERROR)', $busyIndex)
$errorBranchEnd = $armFunction.IndexOf("`n    }", $errorIndex)
$errorBranch = $armFunction.Substring($errorIndex, $errorBranchEnd - $errorIndex)
Assert-Contract ($errorBranch.Contains(
        'sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO)')) `
    'A terminal recorder discard failure must remain an SD IO product error'

$pollStart = $block.IndexOf('void sd_block_device_async_poll(void)')
$pollEnd = $block.IndexOf('uint8_t sd_block_device_async_take_completion(', $pollStart)
Assert-Contract (($pollStart -ge 0) -and ($pollEnd -gt $pollStart)) `
    'Block-device poll function not found'
$pollFunction = $block.Substring($pollStart, $pollEnd - $pollStart)
$completionIndex = $pollFunction.IndexOf('if(dma_complete != 0U)')
$startedTimeoutIndex = $pollFunction.IndexOf(
    'if((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)',
    $completionIndex)
$cardReadyIndex = $pollFunction.IndexOf(
    'if(BSP_SD_GetCardState() == SD_TRANSFER_OK)', $completionIndex)
$hardwareErrorIndex = $pollFunction.IndexOf(
    'if(g_sd_block_device_async_error != 0U)')
Assert-Contract (($completionIndex -ge 0) -and ($cardReadyIndex -gt $completionIndex) `
        -and ($startedTimeoutIndex -gt $cardReadyIndex)) `
    'A valid DMA callback/card-ready completion must be consumed before timeout recovery'
Assert-Contract (($hardwareErrorIndex -ge 0) -and ($hardwareErrorIndex -lt $completionIndex)) `
    'A real block-device hardware error must remain terminal'

foreach ($owner in @('PREPARATION', 'LIVE_EXTEND', 'FINALIZATION')) {
    Assert-Contract $reservationHeader.Contains("RECORDER_FILE_JOB_OWNER_$owner") `
        "Missing explicit Recorder filesystem owner: $owner"
}
Assert-Contract $reservationHeader.Contains('recorder_file_job_owner_t job_owner;') `
    'Reservation jobs must store their functional owner'
Assert-Contract $reservation.Contains(
    'recorder_file_job_owner_matches_phase(session, owner)') `
    'step/poll/finish must validate owner and phase'
Assert-Contract (-not $generic.Contains('recorder_file_reservation_job_active(')) `
    'Live filesystem provider must not infer ownership from generic job activity'
Assert-Contract $generic.Contains('job_owner == RECORDER_FILE_JOB_OWNER_LIVE_EXTEND') `
    'Live filesystem provider must expose only LIVE_EXTEND jobs'
Assert-Contract $adapter.Contains(
    'else if (owner != RECORDER_FILE_JOB_OWNER_LIVE_EXTEND)') `
    'Extension adapter must reject foreign jobs before stepping them'
Assert-Contract $adapter.Contains(
    'RECORDER_FILE_JOB_OWNER_LIVE_EXTEND) == 0U') `
    'Extension adapter must finish only its own terminal'
Assert-Contract (-not $adapter.Contains('RECORDER_FILE_JOB_OWNER_FINALIZATION')) `
    'Extension adapter must have no path to finalization jobs'

$finalTransitions = @{
    COMMIT = 'RELEASE'
    RELEASE = 'HEADER'
    SYNC = 'CLOSE'
    RENAME = 'DONE'
}
foreach ($transition in $finalTransitions.GetEnumerator()) {
    $pattern = '(?s)case AUDIO_RECORDER_FINAL_' + $transition.Key +
        ':.*?runtime->final_phase = AUDIO_RECORDER_FINAL_' + $transition.Value +
        ';.*?recorder_file_reservation_job_finish\(.*?' +
        'RECORDER_FILE_JOB_OWNER_FINALIZATION\)'
    Assert-Contract ([regex]::IsMatch($storage, $pattern)) `
        "Finalization owner did not consume $($transition.Key) -> $($transition.Value)"
}
Assert-Contract $storage.Contains(
    'runtime->phase = AUDIO_RECORDER_STORAGE_TAKE_READY;') `
    'RENAME terminal must advance to TAKE_READY'

function Invoke-OwnerOperation([string]$actualOwner, [string]$callerOwner) {
    return ($actualOwner -eq $callerOwner)
}

foreach ($foreign in @('PREPARATION', 'LIVE_EXTEND')) {
    Assert-Contract (-not (Invoke-OwnerOperation 'FINALIZATION' $foreign)) `
        "Foreign owner accepted for finalization: $foreign"
}
Assert-Contract (Invoke-OwnerOperation 'LIVE_EXTEND' 'LIVE_EXTEND') `
    'LIVE_EXTEND owner must retain its own cooperative job'

Write-Output 'Recorder lifecycle/finalization contract tests: PASS'
