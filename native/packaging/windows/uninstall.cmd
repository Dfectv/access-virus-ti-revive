@echo off
rem uninstall.cmd - runs uninstall.ps1 as administrator
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0uninstall.ps1\"'"
