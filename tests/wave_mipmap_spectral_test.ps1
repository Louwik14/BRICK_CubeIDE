$sampleRate = 48000.0
$outputCount = 1024
$maxSize = 2048

function New-HarmonicTable([int]$size, [int]$harmonics) {
    $table = [double[]]::new($size)
    for ($i = 0; $i -lt $size; $i++) {
        $value = 0.0
        for ($h = 1; $h -le $harmonics; $h++) {
            $value += [Math]::Sin(2.0 * [Math]::PI * $h * $i / $size) / $h
        }
        $table[$i] = $value
    }
    return $table
}

function Read-Linear([double[]]$table, [double]$phase) {
    $size = $table.Length
    $x = $phase * $size
    $i0 = ([int][Math]::Floor($x)) -band ($size - 1)
    $frac = $x - [Math]::Floor($x)
    return $table[$i0] + (($table[($i0 + 1) -band ($size - 1)] - $table[$i0]) * $frac)
}

$raw = New-HarmonicTable $maxSize (($maxSize / 2) - 1)
$rawError = 0.0
$mipError = 0.0
foreach ($note in @(36, 60, 84, 108)) {
    $frequency = 440.0 * [Math]::Pow(2.0, ($note - 69.0) / 12.0)
    $size = $maxSize
    while (($size -gt 8) -and (($frequency / $sampleRate) -gt (1.25 / $size))) {
        $size = [int]($size / 2)
    }
    $harmonics = [int]($size / 2) - 1
    $mip = New-HarmonicTable $size $harmonics
    $phase = 0.137
    for ($i = 0; $i -lt $outputCount; $i++) {
        $ideal = 0.0
        for ($h = 1; $h -le $harmonics; $h++) {
            $ideal += [Math]::Sin(2.0 * [Math]::PI * $h * $phase) / $h
        }
        $oldDelta = (Read-Linear $raw $phase) - $ideal
        $mipDelta = (Read-Linear $mip $phase) - $ideal
        $rawError += $oldDelta * $oldDelta
        $mipError += $mipDelta * $mipDelta
        $phase = ($phase + ($frequency / $sampleRate)) % 1.0
    }
}
$ratio = [Math]::Sqrt($mipError / $rawError)
Write-Output ("wave spectral RMS ratio mip/legacy={0:F6}" -f $ratio)
if ($ratio -ge 0.45) { exit 1 }
