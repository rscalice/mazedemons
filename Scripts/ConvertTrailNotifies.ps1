# Converts stock Timed Niagara Effect notifies in the combat montages to MD Weapon Niagara. Build the project first.
# Close the Unreal Editor first.
$p = Split-Path -Parent $PSScriptRoot
$ue = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$d = "$p\Saved\ClaudeDump"
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "Unreal Editor is still open. Close it and run this again." -ForegroundColor Yellow; exit 1 }
if (Test-Path "$d\convertwn.txt") { Remove-Item "$d\convertwn.txt" }
Write-Host "Converting trail notifies..."
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$p\Scripts\Python\convertwn.py" -unattended -nosplash -NullRHI *> "$d\convertwn.log"
Write-Host "`n== Report ==" -ForegroundColor Cyan
if (Test-Path "$d\convertwn.txt") { Get-Content "$d\convertwn.txt" } else { Write-Host "convertwn.txt missing - see $d\convertwn.log" -ForegroundColor Red }
