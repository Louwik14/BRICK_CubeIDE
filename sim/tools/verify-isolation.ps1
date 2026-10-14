param(
    [ValidateSet('Baseline', 'Check')]
    [string]$Mode = 'Check'
)

$ErrorActionPreference = 'Stop'
$simRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repoRoot = Split-Path -Parent $simRoot
$stateFile = Join-Path $simRoot 'build\isolation-baseline.txt'

function Get-OutsideSimStatus {
    Push-Location $repoRoot
    try {
        git status --porcelain=v1 --untracked-files=all |
            Where-Object { $_ -notmatch '^[ MARCUD?!]{2} sim[\\/]' }
    } finally {
        Pop-Location
    }
}

if ($Mode -eq 'Baseline') {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $stateFile) | Out-Null
    [System.IO.File]::WriteAllLines($stateFile, [string[]]@(Get-OutsideSimStatus))
    exit 0
}

if (-not (Test-Path -LiteralPath $stateFile)) {
    throw 'Isolation baseline missing. Run with -Mode Baseline before implementation.'
}
$baseline = @(Get-Content -LiteralPath $stateFile)
$current = @(Get-OutsideSimStatus)
if (Compare-Object $baseline $current) {
    throw 'A path outside sim/ changed since the isolation baseline.'
}

$cache = Join-Path $simRoot 'build\windows-ucrt\CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $cmakeHomeEntry = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=' | Select-Object -First 1
    $normalizedSimRoot = [System.IO.Path]::GetFullPath($simRoot).Replace([System.IO.Path]::DirectorySeparatorChar, '/')
    if (($null -eq $cmakeHomeEntry) -or ($cmakeHomeEntry.Line -ne "CMAKE_HOME_DIRECTORY:INTERNAL=$normalizedSimRoot")) {
        throw 'CMake build directory does not point to sim/ as its source root.'
    }
}
Write-Output 'Isolation check passed: no path outside sim/ changed.'
