@echo off
setlocal enabledelayedexpansion
title ESP-Hole Auto-Compiler and Flasher
color 0b

echo ========================================================
echo         ESP-HOLE: AUTO-COMPILER AND FLASHER
echo ========================================================
echo.

set ARDUINO_CLI="C:\Users\niten_2e\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
set CONFIG="%~dp0arduino-cli.yaml"
set SKETCH="%~dp0ESP-Hole.ino"
set BUILD_DIR="%~dp0build"
set FQBN=esp32:esp32:esp32
set PORT=COM7

if not exist %ARDUINO_CLI% (
    echo [ERROR] Could not find arduino-cli.exe!
    pause
    exit /b 1
)

if not exist %BUILD_DIR% mkdir %BUILD_DIR%

echo [1/2] Compiling sketch for ESP32...
echo.
%ARDUINO_CLI% compile --config-file %CONFIG% --fqbn %FQBN% --build-path %BUILD_DIR% %SKETCH%
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo.
echo ========================================================
echo [2/2] Ready to upload to ESP32 on %PORT%
echo --------------------------------------------------------
echo Make sure:
echo  1. Your ESP32 is plugged into the PC via USB data cable.
echo  2. Arduino IDE or Serial Monitor is CLOSED so %PORT% is free.
echo.
echo NOTE: If you see "Connecting........_____.....",
echo       HOLD DOWN the BOOT button on your ESP32 for 2 seconds!
echo ========================================================
echo.
pause

echo Uploading to %PORT%...
%ARDUINO_CLI% upload --config-file %CONFIG% -p %PORT% --fqbn %FQBN% --input-dir %BUILD_DIR%
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Upload failed!
    echo If it failed with Access Denied: Close Arduino IDE and try again.
    echo If it failed with Connecting timeout: Hold BOOT and press EN once.
    pause
    exit /b 1
)

echo.
echo ========================================================
echo  SUCCESS! ESP-Hole is flashed and running!
echo ========================================================
echo.
pause
