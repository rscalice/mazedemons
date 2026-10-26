# Adds sword-trail Timed Niagara Effect notifies to the 12 combat montages, then verifies.
# Close the Unreal Editor first (do NOT save the combat montages when closing).
$p = Split-Path -Parent $PSScriptRoot
$ue = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$d = "$p\Saved\ClaudeDump"
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "Unreal Editor is still open. Close it (don't save the montages) and run this again." -ForegroundColor Yellow; exit 1 }
Write-Host "Applying trail notifies..."
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$p\Scripts\Python\apply1.py" -unattended -nosplash -NullRHI *> "$d\apply1.log"
Write-Host "Verifying..."
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$p\Scripts\Python\verify1.py" -unattended -nosplash -NullRHI *> "$d\verify1.log"
Write-Host "`n== Save results ==" -ForegroundColor Cyan
if (Test-Path "$d\apply1.txt") { Get-Content "$d\apply1.txt" | Select-String "saved" } else { Write-Host "apply1.txt missing - see $d\apply1.log" -ForegroundColor Red }
Write-Host "`n== Verify ==" -ForegroundColor Cyan
if (Test-Path "$d\verify1.txt") { Get-Content "$d\verify1.txt" } else { Write-Host "verify1.txt missing - see $d\verify1.log" -ForegroundColor Red }
