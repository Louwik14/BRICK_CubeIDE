@echo off
setlocal

set "PROGRAMMER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
set "FIRMWARE=%~dp0build\Release\BRICK6_CUBE.bin"

if not exist "%PROGRAMMER%" (
    echo STM32_Programmer_CLI.exe introuvable :
    echo %PROGRAMMER%
    goto :failure
)

if not exist "%FIRMWARE%" (
    echo Firmware Release introuvable :
    echo %FIRMWARE%
    goto :failure
)

"%PROGRAMMER%" -c port=USB1
if errorlevel 1 (
    echo.
    echo BRICK DFU introuvable. Faire SHIFT + PLAY/PAUSE puis relancer Flash USB.
    goto :failure
)

echo.
echo === FLASH USB DFU ===
echo %FIRMWARE%
echo.

"%PROGRAMMER%" -c port=USB1 -w "%FIRMWARE%" 0x08000000 -v
if errorlevel 1 goto :failure

echo.
echo FLASH USB TERMINE
endlocal & exit /b 0

:failure
echo.
echo ECHEC DU FLASH USB
endlocal & exit /b 1
