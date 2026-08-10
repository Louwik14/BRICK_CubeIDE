@echo off
setlocal

set "GDB=C:\ChibiStudio\tools\GNU Tools ARM Embedded\11.3 2022.08\bin\arm-none-eabi-gdb.exe"
set "OPENOCD=C:\openocd\OpenOCD-20250710-0.12.0\bin\openocd.exe"
set "OPENOCD_SCRIPTS=C:\openocd\OpenOCD-20250710-0.12.0\share\openocd\scripts"
set "ELF=C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6\build\Release\BRICK6_CUBE.elf"

start "OpenOCD STM32H7" "%OPENOCD%" ^
-s "%OPENOCD_SCRIPTS%" ^
-f interface/stlink.cfg ^
-f target/stm32h7x.cfg ^
-c "transport select swd" ^
-c "adapter speed 200" ^
-c "init" ^
-c "halt"

timeout /t 3 /nobreak >nul

start "GDB STM32H7" "%GDB%" "%ELF%" ^
-ex "set confirm off" ^
-ex "target extended-remote localhost:3333" ^
-ex "monitor reset halt" ^
-ex "break HardFault_Handler"

endlocal