$ErrorActionPreference = 'Stop'
$simRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ucrtBin = 'C:\msys64\ucrt64\bin'
if (-not (Test-Path -LiteralPath (Join-Path $ucrtBin 'gcc.exe'))) {
    throw 'MSYS2 UCRT64 GCC was not found in C:\msys64\ucrt64\bin.'
}
$env:Path = "$ucrtBin;$env:Path"
Push-Location $simRoot
try {
    cmake --preset windows-ucrt
    cmake --build --preset windows-ucrt
    ctest --test-dir build/windows-ucrt -R keymap_contract --output-on-failure
} finally {
    Pop-Location
}
