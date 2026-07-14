@echo off
setlocal EnableExtensions
REM Build only (compile, no upload) - useful to check for errors.
set "HERE=%~dp0"
set "SKETCH=%HERE%ReminderESP32"
set "FQBN=esp32:esp32:esp32c6:PartitionScheme=custom,CDCOnBoot=cdc,FlashSize=16M"
set "ACLI=%HERE%arduino-cli.exe"
if not exist "%ACLI%" set "ACLI=arduino-cli"
set "ARDUINO_DIRECTORIES_DATA=C:\Users\Admin\AppData\Local\Arduino15"
set "ARDUINO_DIRECTORIES_USER=C:\Users\Admin\Documents\Arduino"

echo Building ReminderESP32 (no upload)...
"%ACLI%" compile --fqbn "%FQBN%" "%SKETCH%"
echo.
if "%ERRORLEVEL%"=="0" (echo [SUCCESS] Compiled clean.) else (echo [FAILED] see errors above.)
echo.
pause
