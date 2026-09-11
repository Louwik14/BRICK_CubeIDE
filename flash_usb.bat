@echo off
setlocal

set "FIRMWARE=%~dp0build\Release\BRICK6_CUBE.bin"
set "DFU_LIST=%TEMP%\brick_dfu_%RANDOM%_%RANDOM%.txt"

where dfu-util >nul 2>&1
if errorlevel 1 (
    echo dfu-util introuvable dans PATH.
    goto :failure
)

if not exist "%FIRMWARE%" (
    echo Firmware Release introuvable :
    echo %FIRMWARE%
    goto :failure
)

dfu-util -l >"%DFU_LIST%" 2>&1
set "DFU_LIST_RESULT=%ERRORLEVEL%"
type "%DFU_LIST%"
if not "%DFU_LIST_RESULT%"=="0" (
    del "%DFU_LIST%" >nul 2>&1
    goto :failure
)

%SystemRoot%\System32\findstr.exe /I /C:"0483:df11" "%DFU_LIST%" >nul
if errorlevel 1 (
    del "%DFU_LIST%" >nul 2>&1
    echo.
    echo BRICK DFU introuvable. Faire SHIFT + PLAY/PAUSE puis relancer Flash USB.
    goto :failure
)
del "%DFU_LIST%" >nul 2>&1

echo.
echo === FLASH USB DFU ===
echo %FIRMWARE%
echo.

dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D "%FIRMWARE%"
if errorlevel 1 goto :failure

echo.
echo FLASH USB TERMINE
endlocal & exit /b 0

:failure
echo.
echo ECHEC DU FLASH USB
endlocal & exit /b 1
