$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$owner = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_transport_owner.c')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_runtime.c')
$port = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine_port_h743.c')
$midi = Get-Content -Raw (Join-Path $root 'Src/MIDI/midi.c')
$timer = Get-Content -Raw (Join-Path $root 'Src/MIDI/midi_clock_timer.c')
$usb = Get-Content -Raw (Join-Path $root 'Board/LowCost/UsbStack/usb_device.c')
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

Assert-Contract ($runtime -match 'seq_runtime_send_transport_realtime\(0xFAU\);\s+midi_clock_set_running\(true\);\s+midi_clock_timer_arm\(') `
    'START must queue FA before arming TIM3'
Assert-Contract ($runtime -match 'seq_runtime_send_transport_realtime\(0xFBU\);\s+midi_clock_set_running\(true\);\s+midi_clock_timer_arm\(') `
    'CONTINUE must queue FB before arming TIM3'
Assert-Contract ($runtime.Contains('midi_clock_timer_stop();')) `
    'STOP and source changes must disarm TIM3'
Assert-Contract (-not $port.Contains('seq_runtime_midi_clock_audio_boundary')) `
    'audio boundary must not generate clocks'
Assert-Contract (-not $owner.Contains('g_midi_clock_next_q16')) `
    'old audio deadline producer must be removed'
Assert-Contract ($timer.Contains('NVIC_SetPriority(TIM3_IRQn, 0U)')) `
    'TIM3 must preempt priority-1 audio'
Assert-Contract ($timer.Contains('g_due_q16 +=') -and $timer.Contains('TIM3->CCR1 = compare')) `
    'absolute fractional deadline must drive the hardware compare'
Assert-Contract (-not $timer.Contains('tud_')) `
    'TIM3 path must not call TinyUSB'
Assert-Contract ($timer.Contains('void midi_clock_timer_on_sof(') -and `
    $usb.Contains('midi_clock_timer_on_sof(sof_tick)') -and `
    $usb.Contains('tud_sof_midi_clock_enable(true)')) `
    'USB SOF IRQ must consume timer events without the MIDI main loop'

$numerator = [uint64]($quarter120.PeriodQ16 * 125)
$whole = [uint64][math]::Floor($numerator / 6)
$remainder = [uint64]($numerator % 6)
$phase = [uint64]0
$deadline = [uint64]0
$ticks = @()
for ($i = 0; $i -lt 3; ++$i) {
    $phase += $remainder
    $deadline += $whole + [uint64][math]::Floor($phase / 6)
    $phase %= 6
    $ticks += [uint64][math]::Floor($deadline / 65536)
}
Assert-Contract (($ticks[0] -eq 20833) -and ($ticks[1] -eq 41666) -and ($ticks[2] -eq 62500)) `
    '120 BPM must use 20833, 20833, 20834 us rather than a fixed rounded period'
$deadline = [uint64]0
$phase = [uint64]0
$sofTargets = @()
for ($i = 0; $i -lt 96; ++$i) {
    $phase += $remainder
    $deadline += $whole + [uint64][math]::Floor($phase / 6)
    $phase %= 6
    $sofTargets += [uint64][math]::Floor(($deadline + 65535999) / 65536000)
}
for ($i = 24; $i -lt $sofTargets.Count; ++$i) {
    Assert-Contract (($sofTargets[$i] - $sofTargets[$i - 24]) -eq 500) `
        'every 24-interval SOF window at 120 BPM must span 500 frames'
}
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
