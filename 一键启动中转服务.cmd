@echo off
start "FoloToy NetEase Local Relay" powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0server\scripts\start-all-local-relay.ps1"
