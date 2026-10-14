param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$SimulatorArgs
)

$ErrorActionPreference = 'Stop'
$simRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $simRoot 'build\windows-ucrt\brick-ui-sim.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw 'Simulator not built. Run .\sim\build.ps1 first.'
}
& $exe @SimulatorArgs
exit $LASTEXITCODE

