@echo off
setlocal

set "BMP_PORT=COM11"
set "BMP_FREQ=1M"

set "GDB=C:\ST\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin\arm-none-eabi-gdb.exe"
set "ELF=%~dp0build\Release\BRICK6_CUBE.elf"
set "BIN=%~dp0build\Release\BRICK6_CUBE.bin"
set "READBACK=%~dp0build\Release\BRICK6_CUBE.readback.bin"

if not exist "%GDB%" goto :failure
if not exist "%ELF%" goto :failure
if not exist "%BIN%" goto :failure
if exist "%READBACK%" del /q "%READBACK%"
for %%A in ("%BIN%") do set "BIN_SIZE=%%~zA"
set /a FLASH_END=0x08000000+BIN_SIZE

echo.
echo === FLASH LOWCOST BLACK MAGIC ===
echo %ELF%
echo.

"%GDB%" --quiet --batch --return-child-result "%ELF%" ^
    -ex "set confirm off" ^
    -ex "set pagination off" ^
    -ex "set mem inaccessible-by-default off" ^
    -ex "set remote memory-write-packet-size fixed 32" ^
    -ex "target extended-remote \\.\%BMP_PORT%" ^
    -ex "monitor version" ^
    -ex "monitor frequency %BMP_FREQ%" ^
    -ex "monitor swd_scan" ^
    -ex "attach 1" ^
    -ex "monitor erase_mass" ^
    -ex "load" ^
    -ex "dump binary memory %READBACK% 0x08000000 %FLASH_END%" ^
    -ex "kill" ^
    -ex "quit"
if errorlevel 1 goto :failure
if not exist "%READBACK%" goto :failure

fc /b "%BIN%" "%READBACK%" >nul
if errorlevel 1 goto :verify_failure
del /q "%READBACK%"

echo.
echo FLASH LOWCOST TERMINE
echo.

endlocal
exit /b 0

:verify_failure
echo.
echo ECHEC VERIFICATION FLASH : le readback differe du BIN Release.
if exist "%READBACK%" del /q "%READBACK%"

:failure
echo.
echo ECHEC DU FLASH LOWCOST
endlocal
exit /b 1
