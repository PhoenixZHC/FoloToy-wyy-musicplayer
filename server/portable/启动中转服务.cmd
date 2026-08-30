@echo off
title FoloToy Relay Service
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-relay.ps1" %*
set "relay_exit=%errorlevel%"
if not "%relay_exit%"=="0" (
  echo.
  echo Startup failed. See the data folder logs for details.
  pause
)
exit /b %relay_exit%
