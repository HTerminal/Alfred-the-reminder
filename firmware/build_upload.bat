@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM ============================================================
REM  ReminderESP32  -  build + upload to the board
REM  Usage:   build_upload.bat            (uses COM8)
REM           build_upload.bat COM5       (uses COM5)
REM  Double-click also works (defaults to COM8).
REM ============================================================

set "HERE=%~dp0"
set "SKETCH=%HERE%ReminderESP32"
set "FQBN=esp32:esp32:esp32c6:PartitionScheme=custom,CDCOnBoot=cdc,FlashSize=16M"

REM COM port: first argument, else COM8
set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM8"

REM arduino-cli: the copy next to this .bat, else whatever is on PATH
set "ACLI=%HERE%arduino-cli.exe"
if not exist "%ACLI%" set "ACLI=arduino-cli"

REM pin the data/user dirs to where the core + libraries are installed
set "ARDUINO_DIRECTORIES_DATA=C:\Users\Admin\AppData\Local\Arduino15"
set "ARDUINO_DIRECTORIES_USER=C:\Users\Admin\Documents\Arduino"

echo ============================================================
echo   ReminderESP32   build + upload
echo   Port : %PORT%
echo   FQBN : %FQBN%
echo ============================================================
echo.

REM arduino-cli flashes with "--before default-reset --after hard-reset",
REM so the board reboots into the new firmware by itself once this succeeds.
"%ACLI%" compile --upload -p %PORT% --fqbn "%FQBN%" "%SKETCH%"
set "RC=%ERRORLEVEL%"

REM One automatic retry: a board that is mid-boot or briefly holding the port
REM will refuse the first reset, and a second attempt almost always lands.
if not "%RC%"=="0" (
  echo.
  echo [RETRY] First attempt failed - trying once more...
  timeout /t 2 /nobreak >nul
  "%ACLI%" compile --upload -p %PORT% --fqbn "%FQBN%" "%SKETCH%"
  set "RC=!ERRORLEVEL!"
)

echo.
if "%RC%"=="0" (
  echo [SUCCESS] Uploaded to %PORT% - the board is restarting into the new firmware.
) else (
  echo [FAILED] exit code %RC%
  echo   - Close any Serial Monitor / terminal holding %PORT% and try again.
  echo   - Is the board plugged in and on %PORT%?  Check with:  list_ports.bat
  echo   - Still failing? Force the bootloader: hold BOOT, tap RESET,
  echo     release BOOT, then re-run this script.
  echo   - First time on a new PC?  Run  setup.bat  once.
)
echo.
pause
