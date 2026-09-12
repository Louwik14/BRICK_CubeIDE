@echo off
setlocal

set "BMP_PORT=COM11"
set "BMP_FREQ=4M"

set "GDB=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin\arm-none-eabi-gdb.exe"
set "ELF=%~dp0build\Release\BRICK6_CUBE.elf"

if not exist "%GDB%" (
    echo.
    echo ERREUR : arm-none-eabi-gdb introuvable
    echo %GDB%
    echo.
    pause
    endlocal & exit /b 1
)

if not exist "%ELF%" (
    echo.
    echo ERREUR : ELF Release introuvable
    echo %ELF%
    echo.
    pause
    endlocal & exit /b 1
)

echo.
echo === GDB BRICK / BLACK MAGIC ===
echo Port : %BMP_PORT%
echo ELF  : %ELF%
echo.
echo Le CPU sera attache et HALTE.
echo TIM5 sera automatiquement gele pendant chaque HALT debug.
echo.
echo Commandes utiles :
echo   c              = reprendre l'execution
echo   Ctrl+C         = interrompre / halter
echo   bt             = backtrace
echo   info registers = registres CPU
echo   detach         = detacher proprement
echo   quit           = quitter GDB
echo.

"%GDB%" --quiet "%ELF%" ^
    -ex "set confirm off" ^
    -ex "set pagination off" ^
    -ex "set mem inaccessible-by-default off" ^
    -ex "target extended-remote \\.\%BMP_PORT%" ^
    -ex "monitor frequency %BMP_FREQ%" ^
    -ex "monitor swd_scan" ^
    -ex "attach 1" ^
    -ex "set {unsigned int}0x5C00103C = (*(unsigned int*)0x5C00103C) | 0x8" ^
    -ex "break HardFault_Handler"

endlocal