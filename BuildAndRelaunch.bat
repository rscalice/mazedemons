@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\BuildAndRelaunchEditor.ps1" %*
if errorlevel 1 (
  echo.
  echo BUILD FAILED - see errors above. Full log: %LOCALAPPDATA%\UnrealBuildTool\Log.txt
  pause
)
