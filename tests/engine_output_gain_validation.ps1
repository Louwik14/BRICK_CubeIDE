$ErrorActionPreference = 'Stop'

$expected = [ordered]@{
    BRICK6_DELUGE_OUTPUT_GAIN  = @{
        Gain = 0.44157045
        Db = -7.1
        Path = '..\Src\Core\brick6_deluge_runtime.cpp'
        Application = 'out_mono[i] = rendered * BRICK6_DELUGE_OUTPUT_GAIN'
        UseCount = 3
    }
    BRICK6_WAVE_OUTPUT_GAIN    = @{
        Gain = 0.42169650
        Db = -7.5
        Path = '..\Src\Core\brick6_wave_runtime.c'
        Application = 'out_mono[frame] = rendered * BRICK6_WAVE_OUTPUT_GAIN'
        UseCount = 2
    }
}

foreach ($entry in $expected.GetEnumerator()) {
    $runtimePath = Join-Path $PSScriptRoot $entry.Value.Path
    $source = Get-Content -LiteralPath $runtimePath -Raw
    $match = [regex]::Match(
        $source,
        ('#define\s+{0}\s+([0-9.]+)f' -f [regex]::Escape($entry.Key)))
    if (-not $match.Success) {
        throw "Missing precalculated gain $($entry.Key)"
    }
    $gain = [double]::Parse(
        $match.Groups[1].Value,
        [Globalization.CultureInfo]::InvariantCulture)
    $db = 20.0 * [Math]::Log10($gain)
    if ([Math]::Abs($gain - $entry.Value.Gain) -gt 0.00000001) {
        throw "Unexpected linear value for $($entry.Key): $gain"
    }
    if ([Math]::Abs($db - $entry.Value.Db) -gt 0.000001) {
        throw "Unexpected dB value for $($entry.Key): $db"
    }
    if (($gain -gt 1.0) -or (($gain * 1.0) -ge 1.0)) {
        throw "Full-scale input clips for $($entry.Key)"
    }
    if (-not $source.Contains($entry.Value.Application)) {
        throw "Missing fused output-gain application: $($entry.Value.Application)"
    }
    $useCount = [regex]::Matches($source, ('\b' + $entry.Key + '\b')).Count
    if ($useCount -ne $entry.Value.UseCount) {
        throw "Unexpected application count for $($entry.Key): $useCount"
    }
}

$audioRuntimePath = Join-Path $PSScriptRoot '..\Src\Core\brick6_audio_runtime.c'
$audioRuntimeSource = Get-Content -LiteralPath $audioRuntimePath -Raw
if ($audioRuntimeSource -match 'brick6_apply_output_gain_(?:mono|stereo)\s*\(') {
    throw 'Post-render output-gain passes must remain removed from the audio runtime'
}
if ($audioRuntimeSource -match 'BRICK6_SAMPLER_OUTPUT_GAIN') {
    throw 'Sampler output must remain at its nominal renderer level'
}

# Raw calibration deltas selected for this pass, expressed relative to PRISM.
# Applying the fixed engine gains must bring each mean back to the PRISM ratio.
$rawMeanRelativeToPrism = [ordered]@{
    DELUGE = [Math]::Pow(10.0, 7.1 / 20.0)
    WAVE = [Math]::Pow(10.0, 7.5 / 20.0)
}
$gainByEngine = @{
    DELUGE = $expected.BRICK6_DELUGE_OUTPUT_GAIN.Gain
    WAVE = $expected.BRICK6_WAVE_OUTPUT_GAIN.Gain
}
foreach ($engine in $rawMeanRelativeToPrism.Keys) {
    $corrected = $rawMeanRelativeToPrism[$engine] * $gainByEngine[$engine]
    if ([Math]::Abs($corrected - 1.0) -gt 0.000001) {
        throw "$engine mean does not converge to PRISM: $corrected"
    }
}

Write-Host 'Engine output gain validation: PASS'
