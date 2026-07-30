$ErrorActionPreference = 'Stop'

$runtimePath = Join-Path $PSScriptRoot '..\Src\Core\brick6_audio_runtime.c'
$source = Get-Content -LiteralPath $runtimePath -Raw

$expected = [ordered]@{
    BRICK6_DELUGE_OUTPUT_GAIN  = @{ Gain = 0.44157045; Db = -7.1 }
    BRICK6_WAVE_OUTPUT_GAIN    = @{ Gain = 0.42169650; Db = -7.5 }
    BRICK6_SAMPLER_OUTPUT_GAIN = @{ Gain = 0.51880004; Db = -5.7 }
}

foreach ($entry in $expected.GetEnumerator()) {
    $match = [regex]::Match(
        $source,
        ('#define\s+{0}\s+\(([0-9.]+)f\)' -f [regex]::Escape($entry.Key)))
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
}

if ($source -match 'powf\s*\(') {
    throw 'powf must not be used by the audio runtime'
}

$requiredApplications = @(
    'direct_l, direct_r, frames, BRICK6_SAMPLER_OUTPUT_GAIN',
    'sampler_tmp_l, sampler_tmp_r, frames, BRICK6_SAMPLER_OUTPUT_GAIN',
    'direct_mono, frames, BRICK6_WAVE_OUTPUT_GAIN',
    'wave_tmp, frames, BRICK6_WAVE_OUTPUT_GAIN',
    'direct_mono, frames, BRICK6_DELUGE_OUTPUT_GAIN',
    'deluge_tmp, frames, BRICK6_DELUGE_OUTPUT_GAIN'
)
foreach ($application in $requiredApplications) {
    if (-not $source.Contains($application)) {
        throw "Missing output-gain application: $application"
    }
}
foreach ($entry in $expected.GetEnumerator()) {
    $useCount = [regex]::Matches($source, ('\b' + $entry.Key + '\b')).Count
    if ($useCount -ne 3) {
        throw "Unexpected application count for $($entry.Key): $useCount"
    }
}

foreach ($unchanged in 'prism_tmp', 'stack_tmp', 'drum_tmp') {
    $forbiddenPattern = 'brick6_apply_output_gain_(?:mono|stereo)\s*\([^;]*' `
        + [regex]::Escape($unchanged)
    if ($source -match $forbiddenPattern) {
        throw "Forbidden output correction applied to $unchanged"
    }
}

# Raw calibration deltas selected for this pass, expressed relative to PRISM.
# Applying the fixed engine gains must bring each mean back to the PRISM ratio.
$rawMeanRelativeToPrism = [ordered]@{
    DELUGE = [Math]::Pow(10.0, 7.1 / 20.0)
    WAVE = [Math]::Pow(10.0, 7.5 / 20.0)
    SAMPLER = [Math]::Pow(10.0, 5.7 / 20.0)
}
$gainByEngine = @{
    DELUGE = $expected.BRICK6_DELUGE_OUTPUT_GAIN.Gain
    WAVE = $expected.BRICK6_WAVE_OUTPUT_GAIN.Gain
    SAMPLER = $expected.BRICK6_SAMPLER_OUTPUT_GAIN.Gain
}
foreach ($engine in $rawMeanRelativeToPrism.Keys) {
    $corrected = $rawMeanRelativeToPrism[$engine] * $gainByEngine[$engine]
    if ([Math]::Abs($corrected - 1.0) -gt 0.000001) {
        throw "$engine mean does not converge to PRISM: $corrected"
    }
}

Write-Host 'Engine output gain validation: PASS'
