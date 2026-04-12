@echo off
echo.
echo ===============================
echo   GIT AUTO COMMIT + PUSH
echo ===============================
echo.

set /p msg="Message du commit : "

git add .
git commit -m "%msg%"
git push

echo.
echo ----- Termine -----
pause
