# Swaps the combat montages to root-motion sources, sets Inertialization blends and adds the Motion Warping notify.
# Run order: rebuild the project (C++ changes) -> this script -> Saved\ClaudeDump\rmscale.py (in the editor).
# Close the Unreal Editor first (do NOT save the combat montages when closing).
$p = Split-Path -Parent $PSScriptRoot
$ue = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$d = "$p\Saved\ClaudeDump"
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "Unreal Editor is still open. Close it (don't save the montages) and run this again." -ForegroundColor Yellow; exit 1 }
if (Test-Path "$d\rmswap.txt") { Remove-Item "$d\rmswap.txt" }
Write-Host "Applying root-motion attacks..."
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$p\Scripts\Python\mdrmswap.py" -unattended -nosplash -NullRHI *> "$d\rmswap.log"
Write-Host "`n== Report ==" -ForegroundColor Cyan
if (Test-Path "$d\rmswap.txt") {
    Get-Content "$d\rmswap.txt" | ForEach-Object {
        if ($_ -match "SKIP|FAIL|EXC") { Write-Host $_ -ForegroundColor Yellow } else { Write-Host $_ }
    }
} else { Write-Host "rmswap.txt missing - see $d\rmswap.log" -ForegroundColor Red }
