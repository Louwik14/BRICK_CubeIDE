@echo off
echo === Merge Codex work into main ===

:: Assure qu'on est bien dans un repo
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo Not a git repo
    pause
    exit /b
)

:: Sauvegarde branche actuelle
for /f "delims=" %%i in ('git branch --show-current') do set CURRENT_BRANCH=%%i

echo Current branch: %CURRENT_BRANCH%

:: Commit auto si modifs
git diff --quiet
if errorlevel 1 (
    echo Uncommitted changes detected -> committing
    git add .
    git commit -m "codex: auto commit"
)

:: Passage sur main
echo Switching to main
call git checkout main

:: Merge
echo Merging %CURRENT_BRANCH% into main
call git merge %CURRENT_BRANCH%

:: Retour branche de travail
echo Returning to %CURRENT_BRANCH%
call git checkout %CURRENT_BRANCH%

echo === Done ===
pause