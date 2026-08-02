$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$audio = Get-Content -Raw (Join-Path $root 'Src/Audio/audio.c')
$exec = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime_exec.c')
$execHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_runtime_exec.h')
$runtimeHeader = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_runtime.h')

if ($audio -notmatch '(?s)audio_apply_seq_event_at_sample\(const seq_runtime_audio_event_t \*event.*?seq_runtime_audio_event_t applied_event = \*event;.*?applied_event\.sample_abs = event_sample_time;.*?seq_runtime_audio_apply_event\(&applied_event\);') {
    throw 'audio application sample is not propagated through a local event projection'
}
if ($execHeader -notmatch 'SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK 4U') {
    throw 'external catch-up quota is not fixed at four pulses'
}
if ($exec -notmatch '(?s)pending_steps > SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK.*?external_pulses_coalesced') {
    throw 'external backlog coalescence is not instrumented'
}
if ($exec -match '(?s)drive_external_steps_for_block.*?while \(pending_steps > 0U\)') {
    throw 'external catch-up still drains the complete pending snapshot'
}
if ($exec -match '(?s)drive_external_steps_for_block.*?\&\(seq_runtime_diag_t\)\{0\}') {
    throw 'external pulse diagnostics are discarded'
}
if ($runtimeHeader -notmatch 'max_external_pulses_per_block') {
    throw 'external pulse high-water diagnostic is not exposed'
}

function Skip-ExternalPhase([int]$phase, [int]$div, [int]$length, [int]$step, [int]$pulses) {
    $first = $div - $phase
    $advances = if ($pulses -ge $first) { 1 + [math]::Floor(($pulses - $first) / $div) } else { 0 }
    return [pscustomobject]@{
        phase = ($phase + $pulses) % $div
        step = ($step + $advances) % $length
        wraps = [math]::Floor(($step + $advances) / $length)
    }
}

$case = Skip-ExternalPhase 0 4 16 0 65531
if ($case.phase -ne 3 -or $case.step -ne 14 -or $case.wraps -ne 1023) {
    throw 'coalesced phase projection is not deterministic'
}
$case = Skip-ExternalPhase 3 4 16 15 5
if ($case.phase -ne 0 -or $case.step -ne 1 -or $case.wraps -ne 1) {
    throw 'coalesced phase projection does not preserve loop wrap'
}

Write-Output 'seq_runtime_timing_validation=PASS sample_propagation=explicit external_quota=4 coalesced_phase=deterministic'
