$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manager = Get-Content -Raw (Join-Path $root 'Src/Sampler/sample_stream_manager.c')
$runtime = Get-Content -Raw (Join-Path $root 'Src/Core/brick6_sampler_runtime.c')
$cache = Get-Content -Raw (Join-Path $root 'Src/Sampler/sample_cache.c')

if ($manager -notmatch '\(\(uint64_t\)source_distance \* SAMPLE_STREAM_STEP_Q16_ONE\) / desc->step_q16') {
    throw 'stream deadline is not scaled by the Q16 source speed'
}
if ($manager -notmatch 'if \(desc->step_q16 == 0U\)[\s\S]*?return UINT32_MAX') {
    throw 'zero stream speed is not handled safely'
}
if ($manager -notmatch 'output_distance > UINT32_MAX') {
    throw 'stream deadline does not saturate overflow'
}
if (($runtime | Select-String -AllMatches '\.step_q16 = (?:voice->play_plan|common_plan)\.step_q16').Matches.Count -ne 5) {
    throw 'not all pitched, reverse and loop stream descriptors carry voice speed'
}
if (($cache | Select-String -AllMatches '\.step_q16 = SAMPLE_STREAM_STEP_Q16_ONE').Matches.Count -ne 2) {
    throw 'classic 1x stream descriptors do not preserve their deadline behavior'
}

function Get-Deadline([UInt32]$sourceDistance, [UInt32]$stepQ16) {
    if ($sourceDistance -eq 0) { return [UInt32]0 }
    if ($stepQ16 -eq 0) { return [UInt32]::MaxValue }
    $deadline = ([UInt64]$sourceDistance * 65536) / [UInt64]$stepQ16
    if ($deadline -gt [UInt32]::MaxValue) { return [UInt32]::MaxValue }
    return [UInt32][Math]::Floor($deadline)
}

$distance = [UInt32]65536
$normal = Get-Deadline $distance 65536
$up = Get-Deadline $distance 131072
$down = Get-Deadline $distance 32768
$reverse = Get-Deadline $distance 131072

if ($up -ge $normal) { throw 'step=2 is not more urgent than step=1' }
if ($down -le $normal) { throw 'step=0.5 is not less urgent than step=1' }
if ($reverse -ne $up) { throw 'reverse does not use the absolute speed magnitude' }
if ($normal -ne $distance) { throw 'step=1 deadline behavior changed' }
if ((Get-Deadline $distance 0) -ne [UInt32]::MaxValue) { throw 'step=0 is not saturated' }
if ((Get-Deadline ([UInt32]::MaxValue) 1) -ne [UInt32]::MaxValue) { throw 'very low step overflows' }
