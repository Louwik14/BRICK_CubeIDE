@echo off
setlocal
pushd "%~dp0.." || exit /b 1
set "SIM_EXE=sim\build\windows-ucrt\brick-ui-sim.exe"

if not exist "%SIM_EXE%" (
  echo Simulator not built: %SIM_EXE% 1>&2
  popd
  exit /b 1
)

for %%S in (
  manual-01-tone.jsonl
  manual-02-sample-browser.jsonl
  manual-03-sequencer.jsonl
  manual-07-project-browser.jsonl
  manual-08-patch-browser.jsonl
) do (
  "%SIM_EXE%" --headless --scenario "sim\scenarios\%%S" || (
    echo Scenario failed: %%S 1>&2
    popd
    exit /b 1
  )
)

popd
exit /b 0
