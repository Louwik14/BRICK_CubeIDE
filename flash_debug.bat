@echo off
REM ============================================================
REM  STM32H743 - DEBUG GDB (CubeIDE ELF avec toolchain ChibiOS)
REM ============================================================

REM ------------------------------------------------------------
REM  Toolchain ARM (ChibiStudio)
REM ------------------------------------------------------------
set PATH=C:\ChibiStudio\tools\GNU Tools ARM Embedded\11.3 2022.08\bin;%PATH%

REM ------------------------------------------------------------
REM  OpenOCD
REM ------------------------------------------------------------
set "OPENOCD=C:\openocd\OpenOCD-20250710-0.12.0\bin\openocd.exe"

REM ------------------------------------------------------------
REM  ELF CubeIDE
REM ------------------------------------------------------------
set "ELF=C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6_CUBE_fonctionnel\Debug\BRICK6_CUBE.elf"

REM ------------------------------------------------------------
REM  Flash
REM ------------------------------------------------------------
echo.
echo === Flash STM32H743 ===
"%OPENOCD%" -f interface/stlink.cfg -f target/stm32h7x.cfg ^
  -c "transport select swd; adapter speed 200; init; halt; program {%ELF%} verify reset exit"

if errorlevel 1 (
    echo !!!
    echo !!! ECHEC FLASH !!!
    echo !!!
    pause
    exit /b 1
)

REM ------------------------------------------------------------
REM  OpenOCD serveur
REM ------------------------------------------------------------
echo.
echo === Lancement OpenOCD ===
start "OpenOCD STM32H7" cmd /k ^
""%OPENOCD%" -f interface/stlink.cfg -f target/stm32h7x.cfg ^
 -c "transport select swd; adapter speed 200; init; halt""

REM ------------------------------------------------------------
REM  Pause
REM ------------------------------------------------------------
timeout /t 2 >nul

REM ------------------------------------------------------------
REM  GDB
REM ------------------------------------------------------------
echo.
echo === Lancement GDB ===
start "GDB STM32H7" cmd /k ^
"arm-none-eabi-gdb "%ELF%" ^
 -ex "set confirm off" ^
 -ex "target extended-remote localhost:3333" ^
 -ex "monitor reset halt" ^
 -ex "load" ^
 -ex "break HardFault_Handler" ^
 -ex "break USBH_MIDI_InterfaceInit" ^
 -ex "break USBH_MIDI_Process" ^
 -ex "break main""

echo.
echo === DEBUG PRET ===
pause
