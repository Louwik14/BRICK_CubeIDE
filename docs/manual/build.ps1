[CmdletBinding()]
param(
    [switch]$Diagnose,
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = "Stop"
$ManualDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutputPdf = Join-Path $ManualDir "BRICK_Manual.pdf"

$Chapters = @(
    "01_quick_start.md",
    "02_interface.md",
    "03_tracks_instruments.md",
    "04_sound_engines.md",
    "05_sampler.md",
    "06_sequencer.md",
    "07_midi_fx.md",
    "08_modulation.md",
    "09_scenes.md",
    "10_mixer_fx.md",
    "11_audio_recorder.md",
    "12_patterns.md",
    "13_projects.md",
    "14_patches.md",
    "15_midi_usb_sync.md",
    "16_settings.md",
    "17_power_resume.md",
    "18_reference.md"
)

$Missing = @()
$Pandoc = Get-Command pandoc -ErrorAction SilentlyContinue
$Typst = Get-Command typst -ErrorAction SilentlyContinue
if ($null -eq $Pandoc) { $Missing += "pandoc" }
if ($null -eq $Typst) { $Missing += "typst" }
if ($Missing.Count -ne 0) {
    Write-Error ("Missing required command(s): " + ($Missing -join ", ") + ". See README.md for official installation links.")
    exit 1
}

$MissingFiles = $Chapters | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ManualDir $_) -PathType Leaf) }
if ($MissingFiles.Count -ne 0) {
    Write-Error ("Missing chapter file(s): " + ($MissingFiles -join ", "))
    exit 1
}

$CommonArguments = @(
    "--from=markdown+fenced_divs+implicit_figures",
    "--pdf-engine=typst",
    "--template=$(Join-Path $ManualDir 'template.typ')",
    "--metadata-file=$(Join-Path $ManualDir 'metadata.yaml')",
    "--resource-path=$ManualDir",
    "--toc",
    "--toc-depth=3",
    "--number-sections"
)

function Invoke-DiagnosticPass {
    param(
        [string[]]$InputFiles,
        [string]$OutputFile,
        [int]$Timeout
    )

    $Arguments = @($CommonArguments) + @("--output=$OutputFile") + $InputFiles
    $QuotedArguments = $Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }
    $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Pandoc.Source
    $StartInfo.Arguments = $QuotedArguments -join " "
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Process = [System.Diagnostics.Process]::new()
    $Process.StartInfo = $StartInfo
    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$Process.Start()
    if (-not $Process.WaitForExit($Timeout * 1000)) {
        try { $Process.Kill() } catch { }
        $Stopwatch.Stop()
        return [pscustomobject]@{ Success = $false; TimedOut = $true; Seconds = $Stopwatch.Elapsed.TotalSeconds; ExitCode = $null; Error = "" }
    }
    $StandardError = $Process.StandardError.ReadToEnd()
    $Stopwatch.Stop()
    return [pscustomobject]@{
        Success = ($Process.ExitCode -eq 0)
        TimedOut = $false
        Seconds = $Stopwatch.Elapsed.TotalSeconds
        ExitCode = $Process.ExitCode
        Error = $StandardError
    }
}

if ($Diagnose) {
    $DiagnosticPdf = Join-Path ([System.IO.Path]::GetTempPath()) ("BRICK_Manual_diagnostic_{0}.pdf" -f $PID)
    try {
        for ($Index = 0; $Index -lt $Chapters.Count; ++$Index) {
            $Included = $Chapters[0..$Index] | ForEach-Object { Join-Path $ManualDir $_ }
            Write-Host ("[{0:D2}/{1:D2}] + {2}" -f ($Index + 1), $Chapters.Count, $Chapters[$Index])
            $Result = Invoke-DiagnosticPass -InputFiles $Included -OutputFile $DiagnosticPdf -Timeout $TimeoutSeconds
            if ($Result.Success) {
                Write-Host ("Build OK - {0:N2} s`n" -f $Result.Seconds) -ForegroundColor Green
                continue
            }
            if ($Result.TimedOut) {
                Write-Error ("Build timed out after {0:N2} s. First suspect: {1}" -f $Result.Seconds, $Chapters[$Index])
            } else {
                if ($Result.Error) { Write-Host $Result.Error -ForegroundColor Red }
                Write-Error ("Build failed with exit code {0}. First suspect: {1}" -f $Result.ExitCode, $Chapters[$Index])
            }
            exit 1
        }
        Write-Host "All cumulative diagnostic builds succeeded." -ForegroundColor Green
        exit 0
    } finally {
        Remove-Item -LiteralPath $DiagnosticPdf -Force -ErrorAction SilentlyContinue
    }
}

$Arguments = @($CommonArguments) + @("--output=$OutputPdf")
$Arguments += $Chapters | ForEach-Object { Join-Path $ManualDir $_ }

Write-Host "Building BRICK User Manual..."
$Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
& $Pandoc.Source @Arguments
$Stopwatch.Stop()
if ($LASTEXITCODE -ne 0) {
    Write-Error "Pandoc/Typst generation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}
if (-not (Test-Path -LiteralPath $OutputPdf -PathType Leaf)) {
    Write-Error "The build command completed but did not create $OutputPdf."
    exit 1
}

Write-Host ("PDF generated successfully in {0:N2} s: {1}" -f $Stopwatch.Elapsed.TotalSeconds, $OutputPdf) -ForegroundColor Green
