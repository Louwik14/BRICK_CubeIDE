[CmdletBinding()]
param()

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

$Arguments = @(
    "--from=markdown+fenced_divs+implicit_figures",
    "--pdf-engine=typst",
    "--template=$(Join-Path $ManualDir 'template.typ')",
    "--metadata-file=$(Join-Path $ManualDir 'metadata.yaml')",
    "--resource-path=$ManualDir",
    "--toc",
    "--toc-depth=3",
    "--number-sections",
    "--output=$OutputPdf"
)
$Arguments += $Chapters | ForEach-Object { Join-Path $ManualDir $_ }

Write-Host "Building BRICK User Manual..."
& $Pandoc.Source @Arguments
if ($LASTEXITCODE -ne 0) {
    Write-Error "Pandoc/Typst generation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}
if (-not (Test-Path -LiteralPath $OutputPdf -PathType Leaf)) {
    Write-Error "The build command completed but did not create $OutputPdf."
    exit 1
}

Write-Host "PDF generated successfully: $OutputPdf" -ForegroundColor Green
