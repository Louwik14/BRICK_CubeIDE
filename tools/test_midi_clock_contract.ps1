$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$owner = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_transport_owner.c')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime.c')
$port = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine_port_h743.c')
$midi = Get-Content -Raw (Join-Path $root 'Src/MIDI/midi.c')
$noteTest = Join-Path $root 'tools/test_seq_midi_output_contract.ps1'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw "MIDI CLOCK contract: $message" }
}

function Measure-Clock([uint32]$bpmMilli, [uint32]$quarterCount) {
    $sampleRate = [uint64]48000
    $stepsPerQuarter = [uint64]4
    $clocksPerStep = [uint64]6
    $samplesPerStepQ16 = (($sampleRate * 60 * 1000) -shl 16) / `
        ([uint64]$bpmMilli * $stepsPerQuarter)
    $periodQ16 = [uint64][math]::Floor($samplesPerStepQ16 / $clocksPerStep)
    $endQ16 = (($sampleRate * 60 * 1000 * $quarterCount) -shl 16) / $bpmMilli
    $nextQ16 = $periodQ16
    $count = 0
    $positions = @()
    while ($nextQ16 -le $endQ16) {
        $positions += $nextQ16
        $count++
        $nextQ16 += $periodQ16
    }
    return @{ Count = $count; Positions = $positions; PeriodQ16 = $periodQ16 }
}

$quarter120 = Measure-Clock 120000 1
$bar120 = Measure-Clock 120000 4
$quarter90 = Measure-Clock 90000 1
Assert-Contract ($quarter120.Count -eq 24) '120 BPM quarter must contain exactly 24 clocks'
Assert-Contract ($bar120.Count -eq 96) '120 BPM 4/4 bar must contain exactly 96 clocks'
Assert-Contract ($quarter90.Count -eq 24) '90 BPM quarter must contain exactly 24 clocks'
Assert-Contract ($quarter90.PeriodQ16 -gt $quarter120.PeriodQ16) `
    'clock spacing must increase when tempo decreases'

Assert-Contract ($runtime.Contains('seq_runtime_send_transport_realtime(0xFAU)')) `
    'START must be emitted before clock scheduling is enabled'
Assert-Contract ($runtime.IndexOf('seq_runtime_send_transport_realtime(0xFAU)') -lt `
    $runtime.IndexOf('seq_transport_owner_set_midi_clock_enabled(1U)')) `
    'FA must precede the first possible F8'
Assert-Contract ($runtime -match 'seq_runtime_send_transport_realtime\(0xFBU\);\s+midi_clock_set_running\(true\);') `
    'internal CONTINUE must resume clock production'
Assert-Contract ($owner.Contains('g_midi_clock_enabled=0U;')) `
    'STOP must disable clock production'
Assert-Contract ($port.Contains('seq_runtime_midi_clock_audio_boundary(block_start_sample)')) `
    'clock production must be driven by the audio sample timeline'
Assert-Contract ($midi.Contains('cable | 0x0FU')) `
    'USB System Realtime packets must use CIN 0xF'
Assert-Contract ($midi.Contains('midi_usb_tx_drop_count')) `
    'USB queue saturation must remain observable'
Assert-Contract (-not $midi.Contains('__HAL_TIM_SET_COMPARE(&htim5')) `
    'a parallel timer tempo engine must not remain'

$noteResult = & $noteTest
Assert-Contract (($noteResult -join "`n").Contains('PASS')) `
    'MIDI Note On/Off regression contract failed'

Write-Output 'MIDI Clock end-to-end contract: PASS'
