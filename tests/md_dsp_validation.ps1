$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$lutPath = Join-Path $repo "Src/Audio/md_sine_lut.inc"
$sourcePath = Join-Path $repo "Src/Audio/md_dsp.c"

$values = [regex]::Matches([System.IO.File]::ReadAllText($lutPath), "-?\d+") |
    ForEach-Object { [int]$_.Value }
if ($values.Count -ne 1025) { throw "MD LUT size mismatch: $($values.Count)" }
if ($values[0] -ne 0 -or $values[256] -lt 32766 -or
    [Math]::Abs($values[512]) -gt 1 -or $values[768] -gt -32766 -or
    [Math]::Abs($values[1024]) -gt 1) {
    throw "MD LUT cardinal points invalid"
}
for ($i = 0; $i -lt 512; $i++) {
    if ([Math]::Abs($values[$i] + $values[$i + 512]) -gt 1) {
        throw "MD LUT symmetry invalid at $i"
    }
}

$source = [System.IO.File]::ReadAllText($sourcePath)
foreach ($required in @(
    "md_phase_sine_next",
    "md_decay_env_process",
    "md_rng_next_u32",
    "md_hpf_process",
    "md_lpf_process",
    "md_retrigger_fade_process"
)) {
    if (-not $source.Contains($required)) { throw "Missing primitive: $required" }
}
if ($source.Contains("malloc(") -or $source.Contains("calloc(") -or
    $source.Contains("realloc(") -or $source.Contains("free(")) {
    throw "Dynamic allocation found in MD primitives"
}

$state = [uint32]1234
for ($i = 0; $i -lt 32; $i++) {
    $state = [uint32]($state -bxor [uint32]($state -shl 13))
    $state = [uint32]($state -bxor ($state -shr 17))
    $state = [uint32]($state -bxor [uint32]($state -shl 5))
    if ($state -eq 0) { throw "MD PRNG entered zero state" }
}

Write-Output "MD DSP validation: PASS"
