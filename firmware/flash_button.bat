@echo off
setlocal EnableExtensions
REM ============================================================
REM  Flash the ESP-NOW doorbell BUTTON firmware onto a SEPARATE ESP.
REM  Usage:  flash_button.bat  COM9                  (defaults to XIAO_ESP32C3)
REM          flash_button.bat  COM9  esp32c3        (generic C3)
REM          flash_button.bat  COM9  esp32          (classic ESP32)
REM  Run this from the "firmware" folder (where this .bat lives).
REM ============================================================

set "HERE=%~dp0"
set "SKETCH=%HERE%DoorbellButton"
set "ACLI=%HERE%arduino-cli.exe"
if not exist "%ACLI%" set "ACLI=arduino-cli"

REM pin the data/user dirs to where the core + libraries are installed
set "ARDUINO_DIRECTORIES_DATA=C:\Users\Admin\AppData\Local\Arduino15"
set "ARDUINO_DIRECTORIES_USER=C:\Users\Admin\Documents\Arduino"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM9"
set "BOARD=%~2"
if "%BOARD%"=="" set "BOARD=XIAO_ESP32C3"

echo ============================================================
echo   Doorbell BUTTON firmware
echo   Port  : %PORT%
echo   Board : esp32:esp32:%BOARD%
echo ============================================================
echo.

"%ACLI%" compile --upload -p %PORT% --fqbn "esp32:esp32:%BOARD%" "%SKETCH%"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo [SUCCESS] Button flashed on %PORT%. It DEEP-sleeps between presses.
  echo Wire the button to the D2 pad ^(GPIO4^) and 3V3 ^(active-HIGH^) to ring/wake.
) else (
  echo [FAILED] exit code %RC%
  echo   - Right port^?  run  list_ports.bat
  echo   - Right board^?  pass it as arg 2, e.g.  flash_button.bat %PORT% esp32
)
echo.
pause
