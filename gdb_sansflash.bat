@echo off
setlocal

set "BMP_PORT=COM11"
set "BMP_FREQ=1M"

set "GDB=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin\arm-none-eabi-gdb.exe"
set "ELF=%~dp0build\Release\BRICK6_CUBE.elf"

"%GDB%" --quiet "%ELF%" ^
    -ex "set confirm off" ^
    -ex "set pagination off" ^
    -ex "set mem inaccessible-by-default off" ^
    -ex "target extended-remote \\.\%BMP_PORT%" ^
    -ex "monitor frequency %BMP_FREQ%" ^
    -ex "monitor swd_scan" ^
    -ex "attach 1"

endlocal