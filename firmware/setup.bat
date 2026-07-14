@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM ============================================================
REM  ReminderESP32  -  ONE-TIME setup on a fresh PC.
REM  Installs the ESP32 core + the three libraries and drops
REM  lv_conf.h into the Arduino libraries folder. Run once, then
REM  use build_upload.bat from then on.
REM ============================================================

set "HERE=%~dp0"
set "ACLI=%HERE%arduino-cli.exe"
if not exist "%ACLI%" set "ACLI=arduino-cli"
set "ARDUINO_DIRECTORIES_DATA=C:\Users\Admin\AppData\Local\Arduino15"
set "ARDUINO_DIRECTORIES_USER=C:\Users\Admin\Documents\Arduino"

echo [1/8] init config + add ESP32 board index...
"%ACLI%" config init --overwrite
"%ACLI%" config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
"%ACLI%" core update-index

echo [2/8] install ESP32 core (large - a few minutes)...
"%ACLI%" core install esp32:esp32

echo [3/8] install lvgl 8.3.11...
"%ACLI%" lib install "lvgl@8.3.11"

echo [4/8] install GFX Library for Arduino...
"%ACLI%" lib install "GFX Library for Arduino"

echo [5/8] install SensorLib (QMI8658 IMU)...
"%ACLI%" lib install "SensorLib"

echo [6/8] install ArduinoJson (web config)...
"%ACLI%" lib install "ArduinoJson"

echo [7/8] install WiFiManager (phone Wi-Fi setup)...
"%ACLI%" lib install "WiFiManager"

echo [8/8] place lv_conf.h in the Arduino libraries folder...
for /f "delims=" %%i in ('"%ACLI%" config get directories.user') do set "USERDIR=%%i"
if exist "%USERDIR%\libraries\" (
  copy /Y "%HERE%lv_conf.h" "%USERDIR%\libraries\lv_conf.h" >nul
  echo     copied to "%USERDIR%\libraries\lv_conf.h"
) else (
  echo     WARNING: could not find "%USERDIR%\libraries" - copy lv_conf.h there manually.
)

echo.
echo Setup done. Now run:  build_upload.bat  (with the board plugged in)
echo.
pause
