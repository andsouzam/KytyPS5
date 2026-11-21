@echo off
setlocal enabledelayedexpansion
title KytyPS5 - Ghost of Yotei Runner

echo ===================================================
echo   KytyPS5 - Ghost of Yotei (PPSA26344) Runner
echo ===================================================
echo.

set "KYTY_EXE="
if exist "%~dp0kyty_emulator.exe" (
    set "KYTY_EXE=%~dp0kyty_emulator.exe"
    set "RUN_DIR=%~dp0"
) else if exist "%~dp0..\..\KytyPS5-Windows-x64\kyty_emulator.exe" (
    set "KYTY_EXE=%~dp0..\..\KytyPS5-Windows-x64\kyty_emulator.exe"
    set "RUN_DIR=%~dp0..\..\KytyPS5-Windows-x64"
) else if exist "F:\KytyPS5-Windows-x64\kyty_emulator.exe" (
    set "KYTY_EXE=F:\KytyPS5-Windows-x64\kyty_emulator.exe"
    set "RUN_DIR=F:\KytyPS5-Windows-x64"
) else if exist "%~dp0..\_Build\windows-custom\kyty_emulator.exe" (
    set "KYTY_EXE=%~dp0..\_Build\windows-custom\kyty_emulator.exe"
    set "RUN_DIR=%~dp0..\_Build\windows-custom"
) else (
    echo [ERRO] kyty_emulator.exe nao foi encontrado!
    pause
    exit /b 1
)

if "%YOTEI_GAME_PATH%"=="" (
    set "GAME_PATH=F:\GOI\PPSA26344-app"
) else (
    set "GAME_PATH=%YOTEI_GAME_PATH%"
)

echo Executavel: %KYTY_EXE%
echo Jogo:       %GAME_PATH%
echo Diretorio:  %RUN_DIR%
echo.

cd /d "%RUN_DIR%"
"%KYTY_EXE%" --game "%GAME_PATH%" --addcont GHOST2BASE000000 --screen-width 1920 --screen-height 1080 --shader-cache --tessellation --occlusion-queries --amd-cpu %*

echo.
echo ===================================================
echo   O emulador foi finalizado (codigo %ERRORLEVEL%).
echo ===================================================
pause
