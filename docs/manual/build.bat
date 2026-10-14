@echo off
setlocal
set "MANUAL_DIR=%~dp0"
set "OUTPUT=%MANUAL_DIR%BRICK_Manual.pdf"

set "MISSING_PANDOC=0"
set "MISSING_TYPST=0"
where pandoc >nul 2>nul
if errorlevel 1 set "MISSING_PANDOC=1"
where typst >nul 2>nul
if errorlevel 1 set "MISSING_TYPST=1"
if "%MISSING_PANDOC%%MISSING_TYPST%"=="00" goto dependencies_ok
echo ERROR: required commands are missing from PATH:
if "%MISSING_PANDOC%"=="1" echo   - pandoc
if "%MISSING_TYPST%"=="1" echo   - typst
echo See %MANUAL_DIR%README.md for the official installation links.
exit /b 1

:dependencies_ok

echo Building BRICK User Manual...
pandoc ^
  --from=markdown+fenced_divs+implicit_figures ^
  --pdf-engine=typst ^
  --template="%MANUAL_DIR%template.typ" ^
  --metadata-file="%MANUAL_DIR%metadata.yaml" ^
  --resource-path="%MANUAL_DIR%" ^
  --toc ^
  --toc-depth=3 ^
  --number-sections ^
  --output="%OUTPUT%" ^
  "%MANUAL_DIR%01_quick_start.md" ^
  "%MANUAL_DIR%02_interface.md" ^
  "%MANUAL_DIR%03_tracks_instruments.md" ^
  "%MANUAL_DIR%04_sound_engines.md" ^
  "%MANUAL_DIR%05_sampler.md" ^
  "%MANUAL_DIR%06_sequencer.md" ^
  "%MANUAL_DIR%07_midi_fx.md" ^
  "%MANUAL_DIR%08_modulation.md" ^
  "%MANUAL_DIR%09_scenes.md" ^
  "%MANUAL_DIR%10_mixer_fx.md" ^
  "%MANUAL_DIR%11_audio_recorder.md" ^
  "%MANUAL_DIR%12_patterns.md" ^
  "%MANUAL_DIR%13_projects.md" ^
  "%MANUAL_DIR%14_patches.md" ^
  "%MANUAL_DIR%15_midi_usb_sync.md" ^
  "%MANUAL_DIR%16_settings.md" ^
  "%MANUAL_DIR%17_power_resume.md" ^
  "%MANUAL_DIR%18_reference.md"

if errorlevel 1 (
  echo ERROR: Pandoc/Typst generation failed.
  exit /b 1
)
if not exist "%OUTPUT%" (
  echo ERROR: the build completed without creating "%OUTPUT%".
  exit /b 1
)

echo PDF generated successfully: "%OUTPUT%"
exit /b 0
