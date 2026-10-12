@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\CheckIn.ps1" %*
pause
