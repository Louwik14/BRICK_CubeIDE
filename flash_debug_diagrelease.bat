@echo off
setlocal

REM ============================================================
REM  STM32H743 - BUILD DEBUGDIAG + FLASH CUBEPROGRAMMER + GDB
REM  Flash fiable des deux banques, puis OpenOCD sans reflash
REM ============================================================

REM ------------------------------------------------------------
REM  Projet
REM ------------------------------------------------------------
set "PROJECT=C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6"
set "ELF=%PROJECT%\build\DebugDiag\BRICK6_CUBE.elf"

REM ------------------------------------------------------------
REM  Toolchain ARM pour GDB
REM ------------------------------------------------------------
set "PATH=C:\ChibiStudio\tools\GNU Tools ARM Embedded\11.3 2022.08\bin;%PATH%"

REM ------------------------------------------------------------
REM  OpenOCD
REM ------------------------------------------------------------
set "OPENOCD=C:\openocd\OpenOCD-20250710-0.12.0\bin\openocd.exe"

REM ------------------------------------------------------------
REM  STM32CubeProgrammer CLI
REM  Adapte uniquement ce chemin si CubeProgrammer est ailleurs
REM ------------------------------------------------------------
set "CUBECLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

REM ------------------------------------------------------------
REM  Vérifications outils
REM ------------------------------------------------------------
if not exist "%PROJECT%" (
echo.
echo !!! DOSSIER PROJET INTROUVABLE
echo !!! %PROJECT%
pause
exit /b 1
)

if not exist "%OPENOCD%" (
echo.
echo !!! OPENOCD INTROUVABLE
echo !!! %OPENOCD%
pause
exit /b 1
)

if not exist "%CUBECLI%" (
echo.
echo !!! STM32CUBEPROGRAMMER CLI INTROUVABLE
echo !!! %CUBECLI%
echo.
echo Modifie la variable CUBECLI dans ce script.
pause
exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
echo.
echo !!! CMAKE INTROUVABLE DANS LE PATH
pause
exit /b 1
)

where arm-none-eabi-gdb >nul 2>&1
if errorlevel 1 (
echo.
echo !!! ARM-NONE-EABI-GDB INTROUVABLE
pause
exit /b 1
)

REM ------------------------------------------------------------
REM  Répertoire projet
REM ------------------------------------------------------------
cd /d "%PROJECT%"

REM ------------------------------------------------------------
REM  Configuration DebugDiag
REM ------------------------------------------------------------
echo.
echo ============================================================
echo  CONFIGURATION DEBUGDIAG
echo ============================================================
echo.

cmake --preset DebugDiag
if errorlevel 1 (
echo.
echo !!! ECHEC CONFIGURATION DEBUGDIAG
pause
exit /b 1
)

REM ------------------------------------------------------------
REM  Compilation DebugDiag
REM ------------------------------------------------------------
echo.
echo ============================================================
echo  COMPILATION DEBUGDIAG
echo ============================================================
echo.

cmake --build build/DebugDiag --target BRICK6_CUBE.elf -j4
if errorlevel 1 (
echo.
echo !!! ECHEC COMPILATION DEBUGDIAG
pause
exit /b 1
)

if not exist "%ELF%" (
echo.
echo !!! ELF DEBUGDIAG INTROUVABLE
echo !!! %ELF%
pause
exit /b 1
)

echo.
echo ELF trouve :
echo %ELF%

REM ------------------------------------------------------------
REM  Fermer anciens serveurs OpenOCD
REM ------------------------------------------------------------
taskkill /F /IM openocd.exe >nul 2>&1

REM ------------------------------------------------------------
REM  Flash complet avec STM32CubeProgrammer
REM  CubeProgrammer programme correctement les deux banques
REM ------------------------------------------------------------
echo.
echo ============================================================
echo  FLASH DEBUGDIAG AVEC STM32CUBEPROGRAMMER
echo ============================================================
echo.

"%CUBECLI%" ^
-c port=SWD freq=4000 mode=UR reset=HWrst ^
-e all ^
-d "%ELF%" ^
-v ^
-rst

if errorlevel 1 (
echo.
echo !!!
echo !!! ECHEC FLASH STM32CUBEPROGRAMMER
echo !!!
pause
exit /b 1
)

REM ------------------------------------------------------------
REM  Lancement OpenOCD comme serveur uniquement
REM ------------------------------------------------------------
echo.
echo ============================================================
echo  LANCEMENT OPENOCD
echo ============================================================
echo.

start "OpenOCD STM32H7 DebugDiag" cmd /k ^
""%OPENOCD%" ^
-f interface/stlink.cfg ^
-f target/stm32h7x.cfg ^
-c "transport select swd; adapter speed 200; init; reset halt""

timeout /t 2 >nul

REM ------------------------------------------------------------
REM  Lancement GDB
REM  IMPORTANT : aucun LOAD, la carte est déjà flashée
REM ------------------------------------------------------------
echo.
echo ============================================================
echo  LANCEMENT GDB DEBUGDIAG
echo ============================================================
echo.

start "GDB STM32H7 DebugDiag" cmd /k ^
"arm-none-eabi-gdb "%ELF%" ^
-ex "set confirm off" ^
-ex "set pagination off" ^
-ex "set print pretty on" ^
-ex "set print array on" ^
-ex "set print elements 0" ^
-ex "target extended-remote localhost:3333" ^
-ex "monitor reset halt" ^
-ex "break sample_multi_stream_diag_breakpoint" ^
-ex "break crash_capsule_fault_capture_and_reset" ^
-ex "x/2wx 0x08106868""

echo.
echo ============================================================
echo  DEBUGDIAG PRET
echo ============================================================
echo.
echo Dans GDB, verifie que 0x08106868 contient :
echo   0x080002c5
echo   0x080624b5
echo.
echo Puis tape :
echo   continue
echo.
echo IMPORTANT :
echo   Ne tape pas load dans GDB.
echo.
pause

endlocal
