@echo off
setlocal EnableExtensions
REM Show connected boards / COM ports (find which COM your board is on).
set "HERE=%~dp0"
set "ACLI=%HERE%arduino-cli.exe"
if not exist "%ACLI%" set "ACLI=arduino-cli"
"%ACLI%" board list
echo.
pause
