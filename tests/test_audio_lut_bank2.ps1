param([string]$BuildDir = "build/Release")
$ErrorActionPreference = "Stop"
$elf = Join-Path $BuildDir "BRICK6_CUBE.elf"
$tool = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.3 rel1\bin\arm-none-eabi-nm.exe"
if(!(Test-Path $elf)) { throw "Release ELF absent: $elf" }
$symbols = & $tool -S -n $elf
$expected = @{
    g_mixer_filter_log2_lut = @("08100000", "00000404")
    g_mixer_filter_time_lut = @("08100404", "00000402")
    g_fx_dj_eq3_coeff_lut = @("08100808", "00001e3c")
}
foreach($name in $expected.Keys) {
    $line = $symbols | Select-String " $name$" | Select-Object -First 1
    if(!$line) { throw "Symbole LUT absent: $name" }
    $parts = ($line.Line -split '\s+') | Where-Object { $_ }
    if(($parts[0] -ne $expected[$name][0]) -or ($parts[1] -ne $expected[$name][1])) {
        throw "Placement/taille invalide pour ${name}: $($line.Line)"
    }
}
$tpt = $symbols | Select-String " g_fx_filter_g_lut(\.lto_priv\.0)?$" | Select-Object -First 1
if(!$tpt) { throw "LUT TPT absente" }
$tptParts = ($tpt.Line -split '\s+') | Where-Object { $_ }
if(!($tptParts[0] -like "240*")) { throw "LUT TPT hors D1" }
Write-Output "audio LUT bank2 placement: OK"
