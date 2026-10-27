# Weapon-stance pipeline: builds the ranged pose-search databases and (re)writes the stance rows of BOTH motion-matching choosers
# (CHT_PoseSearchDatabases_Dense and _Sparse). Idempotent, safe to run any time. Report: Saved\ClaudeDump\stancechoosers.txt
#
# HOW TO ADD ANIMATIONS FOR A RANGED STANCE (Pistol / Rifle / Staff):
#   1. Import the stance's animation sequences (idle, walks, runs, stops, starts... all in one folder) on the project mannequin skeleton into
#        /Game/Animations/Ranged/Pistol/     (or /Rifle/ or /Staff/)       <- the folder convention, anim sequences directly in the folder
#   2. Close the editor, then run:   powershell -ExecutionPolicy Bypass -File Scripts\ApplyStanceChoosers.ps1
#      It creates /Game/Animations/MotionMatchingData/Ranged/PSD_Ranged_<Stance> with the project schema (PSS_Idle), adds every
#      compatible anim, validates it (schema set, not empty), builds the search index and points the stance's chooser rows at it.
#   3. Re-open the editor, play, equip the gun and press Q.
# Until a stance folder has animations, its chooser rows are PLACEHOLDERS that play the unarmed databases, so the stance already works.
# Re-running with unchanged folders does nothing new (-Force rebuilds the databases anyway; -DryRun only prints the tables).
#
# Order when C++ changed: 1) close the editor  2) Scripts\BuildAndTest.ps1 -NoRelaunch (builds the editor module)  3) run this.
param([switch]$DryRun, [switch]$Force)
$p = Split-Path -Parent $PSScriptRoot
$ue = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$d = "$p\Saved\ClaudeDump"
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "Unreal Editor is open. Close it and run this again." -ForegroundColor Yellow; exit 1 }
if ($DryRun) { $env:STANCE_DRYRUN = "1" } else { Remove-Item Env:STANCE_DRYRUN -ErrorAction SilentlyContinue }
if ($Force) { $env:STANCE_FORCE = "1" } else { Remove-Item Env:STANCE_FORCE -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $d | Out-Null
Remove-Item "$d\stancechoosers.txt" -ErrorAction SilentlyContinue
Write-Host "Applying the stance pipeline (ranged databases + chooser rows)..."
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$p\Scripts\Python\stancechoosers.py" -unattended -nosplash -NullRHI *> "$d\stancechoosers.log"
Write-Host "`n== Report ==" -ForegroundColor Cyan
if (Test-Path "$d\stancechoosers.txt") { Get-Content "$d\stancechoosers.txt" } else { Write-Host "stancechoosers.txt missing - see $d\stancechoosers.log" -ForegroundColor Red; exit 1 }
