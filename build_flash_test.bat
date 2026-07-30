@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "OPENOCD=C:\openocd\OpenOCD-20250710-0.12.0\bin\openocd.exe"
set "ELF=%PROJECT_DIR%build\Test\BRICK6_CUBE.elf"

where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake introuvable dans le PATH
    pause
    exit /b 1
)

if not exist "%OPENOCD%" (
    echo OpenOCD introuvable :
    echo %OPENOCD%
    pause
    exit /b 1
)

pushd "%PROJECT_DIR%"
if errorlevel 1 (
    echo Impossible d'ouvrir le dossier du projet :
    echo %PROJECT_DIR%
    pause
    exit /b 1
)

echo.
echo === CONFIGURATION TEST LOWCOST ===
echo.

cmake --preset Test
if errorlevel 1 (
    echo.
    echo ECHEC DE LA CONFIGURATION
    popd
    pause
    exit /b 1
)

echo.
echo === COMPILATION TEST LOWCOST ===
echo.

cmake --build --preset Test
if errorlevel 1 (
    echo.
    echo ECHEC DE LA COMPILATION
    popd
    pause
    exit /b 1
)

if not exist "%ELF%" (
    echo.
    echo ELF Test lowcost introuvable :
    echo %ELF%
    popd
    pause
    exit /b 1
)

echo.
echo === FLASH TEST LOWCOST ===
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
    popd
    pause
    exit /b 1
)

popd

echo.
echo CONFIGURATION, COMPILATION ET FLASH TEST LOWCOST TERMINES
pause
exit /b 0