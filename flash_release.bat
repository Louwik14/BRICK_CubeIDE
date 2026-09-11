@echo off
setlocal

set "BMP_PORT=COM11"
set "GDB=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin\arm-none-eabi-gdb.exe"
set "ELF=%~dp0build\Release\BRICK6_CUBE.elf"

if not exist "%GDB%" (
    echo arm-none-eabi-gdb introuvable :
    echo %GDB%
    endlocal & exit /b 1
)

if not exist "%ELF%" (
    echo ELF lowcost introuvable :
    echo %ELF%
    endlocal & exit /b 1
)

echo.
echo === FLASH LOWCOST BLACK MAGIC ===
echo %ELF%
echo.

"%GDB%" --batch --quiet "%ELF%" ^
    -ex "set confirm off" ^
    -ex "set pagination off" ^
    -ex "target extended-remote \\.\%BMP_PORT%" ^
    -ex "monitor swd_scan" ^
    -ex "attach 1" ^
    -ex "load" ^
    -ex "compare-sections" ^
    -ex "detach" ^
    -ex "quit"

if errorlevel 1 (
    echo.
    echo ECHEC DU FLASH
    endlocal & exit /b 1
)

echo.
echo FLASH LOWCOST TERMINE
endlocal & exit /b 0
