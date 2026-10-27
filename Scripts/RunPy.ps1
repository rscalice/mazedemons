# Runs one Python script headless in the editor commandlet (editor must be closed). Usage: RunPy.ps1 Scripts\Python\foo.py
# Script file names must not start with r, t, n, a, b, f or v (the commandlet parser eats those). Log: Saved\ClaudeDump\<name>.log
param([Parameter(Mandatory = $true)][string]$Script)
$p = Split-Path -Parent $PSScriptRoot
$ue = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { Write-Host "Unreal Editor is open. Close it first." -ForegroundColor Yellow; exit 1 }
$full = (Resolve-Path $Script).Path -replace '\\', '/'
$name = [IO.Path]::GetFileNameWithoutExtension($Script)
$log = "$p\Saved\ClaudeDump\$name.log"
New-Item -ItemType Directory -Force -Path "$p\Saved\ClaudeDump" | Out-Null
& $ue "$p\MazeDemons.uproject" -run=pythonscript -script="$full" -unattended -nosplash -NullRHI *> $log
Write-Host "Done. Log: $log"
