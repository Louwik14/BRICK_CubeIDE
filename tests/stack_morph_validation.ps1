$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$wavePath = Join-Path $repo 'Src\Core\brick6_stack_waveform.c'
$runtimeHeaderPath = Join-Path $repo 'Inc\Core\brick6_stack_runtime.h'
$rendererPath = Join-Path $repo 'Src\UI\ui_renderer_template.c'

$wave = Get-Content -Raw $wavePath
$headWave = git -C $repo show HEAD:Src/Core/brick6_stack_waveform.c
$marker = 'static uint32_t brick6_stack_waveform_skew_phase'
$markerAt = $wave.IndexOf($marker, [StringComparison]::Ordinal)
if ($markerAt -lt 0) {
    throw 'Missing Stack morph waveform implementation'
}
$legacyPrefix = ($wave.Substring(0, $markerAt) -replace "`r`n", "`n").TrimEnd()
if ($legacyPrefix -ne (($headWave -join "`n").TrimEnd())) {
    throw 'Legacy Stack waveform implementation changed'
}

$runtimeHeader = Get-Content -Raw $runtimeHeaderPath
$legacyModels = @(
    'BRICK6_STACK_MODEL_SINFD',
    'BRICK6_STACK_MODEL_SHAPE',
    'BRICK6_STACK_MODEL_WAVETABLE',
    'BRICK6_STACK_MODEL_SUB',
    'BRICK6_STACK_MODEL_FM',
    'BRICK6_STACK_MODEL_FEEDBACK_FM',
    'BRICK6_STACK_MODEL_RING',
    'BRICK6_STACK_MODEL_TRIPLE_SAW',
    'BRICK6_STACK_MODEL_TRIPLE_SQUARE',
    'BRICK6_STACK_MODEL_SWARM',
    'BRICK6_STACK_MODEL_TRIFD'
)
$lastAt = -1
foreach ($model in $legacyModels) {
    $at = $runtimeHeader.IndexOf($model, [StringComparison]::Ordinal)
    if ($at -le $lastAt) {
        throw "Legacy Stack model order changed at $model"
    }
    $lastAt = $at
}
foreach ($model in @('BRICK6_STACK_MODEL_SINMORPH', 'BRICK6_STACK_MODEL_TRIMORPH')) {
    $at = $runtimeHeader.IndexOf($model, [StringComparison]::Ordinal)
    if ($at -le $lastAt) {
        throw "New Stack model was not appended: $model"
    }
    $lastAt = $at
}

$renderer = Get-Content -Raw $rendererPath
foreach ($call in @('brick6_stack_waveform_sine_morph(', 'brick6_stack_waveform_tri_morph(')) {
    if (-not $renderer.Contains($call)) {
        throw "Stack preview does not use runtime waveform helper: $call"
    }
}

function Mix([double]$a, [double]$b, [double]$amount) {
    return $a + (($b - $a) * $amount)
}

function Warp([double]$phase, [double]$skew) {
    $amount = ($skew - 0.5) * 1.5
    return $phase + ($phase * (1.0 - $phase) * $amount)
}

function Triangle([double]$phase) {
    return 1.0 - (4.0 * [Math]::Abs($phase - 0.5))
}

$worstMorphStep = 0.0
foreach ($phaseIndex in 0..1023) {
    $phase = ($phaseIndex + 0.5) / 1024.0
    $sine = [Math]::Sin(2.0 * [Math]::PI * $phase)
    $triangle = Triangle $phase
    foreach ($target in 0.0, 0.3333333333, 0.6666666667, 1.0) {
        foreach ($shape in 0.0, 0.5, 1.0) {
            if ([Math]::Abs((Mix $sine 0.25 0.0) - $sine) -gt 1e-12) {
                throw 'SINMORPH does not start at sine'
            }
            if ([Math]::Abs((Mix $triangle -0.25 0.0) - $triangle) -gt 1e-12) {
                throw 'TRIMORPH does not start at triangle'
            }
            $warped = Warp $phase $shape
            if (($warped -lt -1e-12) -or ($warped -gt 1.0 + 1e-12)) {
                throw 'Skew phase left the cycle'
            }
        }
    }
    $targetWave = [Math]::Abs($sine)
    $previous = Mix $sine $targetWave 0.5
    $next = Mix $sine $targetWave (0.5 + (1.0 / 32767.0))
    $worstMorphStep = [Math]::Max($worstMorphStep, [Math]::Abs($next - $previous))
}
if ($worstMorphStep -gt 0.0001) {
    throw "Excessive one-LSB morph step: $worstMorphStep"
}

"stack_morph_validation=PASS legacy_waveforms_unchanged=1 preview_shared_helpers=1 max_param_step=$($worstMorphStep.ToString('F8'))"
