@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect-logs.ps1"
pause
