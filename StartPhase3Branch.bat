@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\StartBranchFromMain.ps1" -Branch feature/phase-3-step0-replication
pause
