@echo off
setlocal
cd /d "%~dp0"
echo(
echo   ============================================================
echo    ReminderESP32 flasher  -  Waveshare ESP32-C6-LCD-1.69
echo   ============================================================
echo(
echo   1) Plug the board into this PC with a USB-C cable.
echo   2) Close any serial monitor that is using the COM port.
echo   3) Press a key to flash.
echo(
echo   (Optional) pass the COM port if auto-detect fails, e.g.
echo        flash-windows.bat COM8
echo(
pause

set "PORTARG="
if not "%~1"=="" set "PORTARG=--port %~1"

esptool.exe --chip esp32c6 %PORTARG% --baud 921600 write_flash -z 0x0 firmware-merged.bin
set "RC=%ERRORLEVEL%"

echo(
if "%RC%"=="0" (
  echo   [DONE]  Firmware flashed. The device restarts on its own.
  echo          Open the IP shown on its screen to configure it.
) else (
  echo   [FAILED] exit code %RC%
  echo    - Is the board plugged in? Close any serial monitor.
  echo    - If the port isn't found, re-run with the port, e.g.  flash-windows.bat COM8
  echo    - Or put it in download mode: hold BOOT, tap RESET, release BOOT, then retry.
)
echo(
pause
