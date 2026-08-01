$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtime = Get-Content -Raw (Join-Path $root 'Src/Core/brick6_sampler_runtime.c')
$header = Get-Content -Raw (Join-Path $root 'Inc/Core/brick6_sampler_runtime.h')
$keyboard = Get-Content -Raw (Join-Path $root 'Src/Keyboard/keyboard_engine.c')
$scheduler = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_play_scheduler.c')
$pipeline = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_pipeline.c')
$trackRuntime = Get-Content -Raw (Join-Path $root 'Src/Core/track_runtime.c')

function Require-Match([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}

Require-Match $header '#define STREAM_SAMPLER_ROOT_NOTE\s+\(60U\)' 'Stream root note is not explicit'
Require-Match $runtime '\.note_delta = \(int16_t\)played_note - \(int16_t\)STREAM_SAMPLER_ROOT_NOTE' 'Stream note delta is not signed'
Require-Match $runtime 'source_sample_rate[\s\S]*?BOARD_AUDIO_SAMPLE_RATE_HZ[\s\S]*?sample_rate_ratio[\s\S]*?musical_ratio' 'Stream pitch lacks explicit sample-rate composition'
Require-Match $runtime 'timing_ratio \* desired_pitch_ratio' 'Reader mode does not compose timing and musical pitch'
Require-Match $runtime 'sample_rate_ratio \* timing_ratio[\s\S]*?represented_pitch_ratio / reader_timing_ratio' 'Shifter mode does not preserve reader timing and divide pitch by timing'
Require-Match $runtime 'voice->note,[\s\S]*?desc,[\s\S]*?clip,[\s\S]*?use_shifter' 'Playback does not build pitch from the retained note'
if ($runtime -match 'voice->note\s*=\s*60U') { throw 'Stream playback still overwrites the played note' }
Require-Match $runtime 'brick6_sampler_runtime_trigger_note_velocity[\s\S]*?g_sampler_voice\[track_id\]\.note = note;[\s\S]*?brick6_sampler_runtime_clip_start_playback' 'Stream Note On does not rebuild its plan on retrigger'

foreach ($source in @($keyboard, $scheduler)) {
    Require-Match $source 'note_fx_pipeline_submit' 'Keyboard or sequencer bypasses NoteFx'
}
Require-Match $pipeline 'seq_play_scheduler_dispatch_terminal_note_to_channel' 'NoteFx lacks the shared terminal dispatcher'
Require-Match $scheduler 'brick6_sampler_runtime_trigger_note_velocity\(track, note, velocity\)' 'Terminal dispatcher does not preserve the canonical note'
Require-Match $trackRuntime 'track_runtime_ctx_is_sampler_clip_or_looper\(ctx\)[\s\S]*?return 0U;' 'Stream VCA contract changed in the pitch-only pass'

function To-Q16([double]$Ratio) {
    $bounded = [Math]::Max(0.03125, [Math]::Min(32.0, $Ratio))
    return [UInt32][Math]::Floor($bounded * 65536.0 + 0.5)
}

function Pitch-Plan([int]$Note, [double]$ClipPitch, [UInt32]$SourceRate,
                    [double]$Timing, [bool]$Shifter) {
    $delta = [Int16]$Note - [Int16]60
    $desired = ([double]$SourceRate / 48000.0) * [Math]::Pow(2.0, ($delta + $ClipPitch) / 12.0)
    $pitchQ16 = To-Q16 $desired
    if ($Shifter) {
        $stepQ16 = To-Q16 (($SourceRate / 48000.0) * $Timing)
        $correction = ($pitchQ16 / 65536.0) / ($stepQ16 / 65536.0)
    } else {
        $stepQ16 = To-Q16 ($Timing * $desired)
        $correction = 1.0
    }
    return @{ Delta=$delta; Desired=$desired; PitchQ16=$pitchQ16; StepQ16=$stepQ16; Correction=$correction }
}

$cases = @(
    @{ Note=60; Ratio=1.0 }, @{ Note=59; Ratio=0.9438743127 },
    @{ Note=61; Ratio=1.0594630944 }, @{ Note=48; Ratio=0.5 },
    @{ Note=72; Ratio=2.0 }, @{ Note=0; Ratio=0.03125 },
    @{ Note=127; Ratio=47.9458264601 }
)
foreach ($case in $cases) {
    $plan = Pitch-Plan $case.Note 0.0 48000 1.0 $false
    if ([Math]::Abs($plan.Desired - $case.Ratio) -gt 0.000001) { throw "Bad ratio for note $($case.Note)" }
    if (($plan.StepQ16 -lt 2048) -or ($plan.StepQ16 -gt 2097152)) { throw "Q16 wrap for note $($case.Note)" }
}
if ((Pitch-Plan 127 0.0 48000 1.0 $false).StepQ16 -ne 2097152) { throw 'High-note Q16 saturation is not deterministic' }
if ((Pitch-Plan 0 -12.0 48000 1.0 $false).StepQ16 -ne 2048) { throw 'Low-note Q16 saturation is not deterministic' }

$positive = Pitch-Plan 60 7.0 48000 1.0 $false
$negative = Pitch-Plan 60 -7.0 48000 1.0 $false
if (($positive.StepQ16 -le 65536) -or ($negative.StepQ16 -ge 65536)) { throw 'Clip pitch sign is not composed' }
$rate441 = Pitch-Plan 60 0.0 44100 1.0 $false
$rate96 = Pitch-Plan 60 0.0 96000 1.0 $false
if (($rate441.StepQ16 -ne (To-Q16 (44100.0 / 48000.0))) -or ($rate96.StepQ16 -ne 131072)) { throw 'Synthetic sample-rate factor failed' }
$rate96Shifter = Pitch-Plan 60 0.0 96000 1.0 $true
if (($rate96Shifter.StepQ16 -ne 131072) -or ([Math]::Abs($rate96Shifter.Correction - 1.0) -gt 0.000001)) { throw 'Shifter reader sample-rate factor failed' }

$timed = Pitch-Plan 72 0.0 48000 0.5 $false
$shifted = Pitch-Plan 72 0.0 48000 0.5 $true
if ($timed.StepQ16 -ne 65536) { throw 'Timing/non-shifter composition failed' }
if (($shifted.StepQ16 -ne 32768) -or ([Math]::Abs($shifted.Correction - 4.0) -gt 0.000001)) { throw 'Shifter composition failed' }

$first = Pitch-Plan 59 0.0 48000 1.0 $false
$second = Pitch-Plan 61 0.0 48000 1.0 $false
if ($first.StepQ16 -eq $second.StepQ16) { throw 'Retrigger notes reuse one pitch ratio' }

Write-Output 'stream_sampler_pitch_validation=PASS root=60 signed=yes endpoints=unified notes=0..127 clip_pitch=yes retrigger=yes timing=yes shifter=yes sample_rate=yes vca=unchanged'
