# Adds weapon-stance rows (Greatsword / DualSword) to the GASP motion-matching choosers and saves CHT_PoseSearchDatabases_Sparse.
# Order: 1) close the editor  2) rebuild (Scripts\BuildAndTest.ps1 / BuildAndRelaunchEditor.ps1)  3) close the editor again if it relaunched  4) run this.
# Needs ABP_SandboxCharacter saved with the WeaponStances variable. Idempotent. Use -DryRun to only print the tables.
param([switch]$DryRun)
$p = Split-Path -Parent $PSScriptRoot
$ue = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$d = "$p\Saved\ClaudeDump"
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "Unreal Editor is open. Close it and run this again." -ForegroundColor Yellow; exit 1 }
if ($DryRun) { $env:STANCE_DRYRUN = "1" } else { Remove-Item Env:STANCE_DRYRUN -ErrorAction SilentlyContinue }
Remove-Item "$d\stancechoosers.txt" -ErrorAction SilentlyContinue
Write-Host "Applying stance rows to the motion-matching choosers..."
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$p\Scripts\Python\stancechoosers.py" -unattended -nosplash -NullRHI *> "$d\stancechoosers.log"
Write-Host "`n== Report ==" -ForegroundColor Cyan
if (Test-Path "$d\stancechoosers.txt") { Get-Content "$d\stancechoosers.txt" } else { Write-Host "stancechoosers.txt missing - see $d\stancechoosers.log" -ForegroundColor Red; exit 1 }
