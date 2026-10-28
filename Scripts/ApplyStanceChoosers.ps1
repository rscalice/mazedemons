# Weapon-stance pipeline: builds the ranged pose-search databases and (re)writes the stance rows of BOTH motion-matching choosers
# (CHT_PoseSearchDatabases_Dense and _Sparse). Idempotent, safe to run any time. Report: Saved\ClaudeDump\stancechoosers.txt
#
# WHERE THE RANGED ANIMATIONS COME FROM: RANGED_SOURCES in Scripts\Python\stancechoosers.py maps a stance to a content folder that is
# searched recursively and whose GASP-named clips (M_Neutral_<clip>_<Stance>) are sorted into roles BY NAME (nothing is moved or copied):
#   Pistol -> /Game/Animations/WeaponStances/_FixedPistol     Rifle -> /Game/Animations/WeaponStances/_FixedRifle     Staff -> none yet
# (override per run with the environment variable STANCE_SRC_PISTOL / STANCE_SRC_RIFLE / STANCE_SRC_STAFF = /Game/...).
#   1. Close the editor, then run:   powershell -ExecutionPolicy Bypass -File Scripts\ApplyStanceChoosers.ps1
#      It creates one database per role in /Game/Animations/MotionMatchingData/Ranged/ (PSD_Ranged_<Stance>_<Role>: Idles, TurnInPlace,
#      Walk/Run/Sprint stops, starts, loops, pivots, walk/run spin), with the same schemas as the stock Dense databases (PSS_Idle / PSS_Default
#      / PSS_Stop), checks every clip is on the SK_UEFN_Mannequin skeleton, builds the search indexes and points the Pistol and Rifle rows of
#      BOTH choosers (Dense and Sparse) at them. Aim-offset (HeadAim*), traversal, crouch and jump clips are not used.
#   2. Re-open the editor, play, equip the gun and press Q.
# A stance without a source set (Staff) keeps PLACEHOLDER rows that play the unarmed databases, so the stance already works.
# Re-running with unchanged folders does nothing new (-Force rebuilds the databases anyway; -DryRun only prints the clip counts and tables).
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
