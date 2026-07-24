@echo off

set "OPENOCD=C:\openocd\OpenOCD-20250710-0.12.0\bin\openocd.exe"
set "ELF=C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6\build\Release\BRICK6_CUBE.elf"

if not exist "%OPENOCD%" (
echo OpenOCD introuvable
pause
exit /b 1
)

if not exist "%ELF%" (
echo ELF lowcost introuvable :
echo %ELF%
pause
exit /b 1
)

echo.
echo === FLASH LOWCOST ===
echo %ELF%
echo.

"%OPENOCD%" ^
-f interface/stlink.cfg ^
-f target/stm32h7x.cfg ^
-c "transport select swd" ^
-c "adapter speed 125" ^
-c "init; halt; program {%ELF%} verify reset exit"

if errorlevel 1 (
echo.
echo ECHEC DU FLASH
pause
exit /b 1
)

echo.
echo FLASH LOWCOST TERMINE
pause