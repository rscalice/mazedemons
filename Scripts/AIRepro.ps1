# FABLE repro: a listen host and a client, both standalone -game, driven by Saved\ClaudeDump\host.txt / client.txt.
# Usage: powershell -ExecutionPolicy Bypass -File Saved\fable_repro.ps1 [-Seconds 70] [-NoClient]
param([int]$Seconds = 70, [switch]$NoClient)
$ErrorActionPreference = 'Continue'
$root = "C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons"
$proj = "$root\MazeDemons.uproject"
$exe = "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$dump = "$root\Saved\ClaudeDump"
New-Item -ItemType Directory -Force -Path $dump | Out-Null
Remove-Item "$dump\host.log","$dump\client.log","$dump\host.txt","$dump\client.txt" -ErrorAction SilentlyContinue

# Host script: motion probe on, compact overlay, spawn a real ghoul pack 8 m away, let it engage, then shoot the pack twice.
@"
md.AI.DebugMotion 60
md.AI.Debug 1
wait 4
md.AI.SpawnTest ghoul 6 8
wait 20
md.AI.DumpTokens
md.AI.HurtAll 5 12
wait 1.5
md.AI.HurtAll 5 12
wait 6
md.AI.DebugMotionSummary
md.Ranged.Shot host_a
wait 12
md.AI.DumpTokens
md.AI.DebugMotionSummary
md.Ranged.Shot host_b
"@ | Set-Content "$dump\host.txt"

@"
md.AI.DebugMotion 60
wait 30
md.AI.DebugMotionSummary
md.Ranged.Shot client_a
wait 16
md.AI.DebugMotionSummary
md.Ranged.Shot client_b
"@ | Set-Content "$dump\client.txt"

$hostArgs = "`"$proj`" /Game/Maps/L_MazeDemonsPrototype?listen -game -log -MDCmdFile=host -WINDOWED -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -NOSPLASH -LOG=fable_host.log"
$hostProc = Start-Process -FilePath $exe -ArgumentList $hostArgs -PassThru
"host pid $($hostProc.Id)"
if (-not $NoClient) {
  Start-Sleep 55
  $clientArgs = "`"$proj`" 127.0.0.1 -game -log -MDCmdFile=client -WINDOWED -ResX=1280 -ResY=720 -WinX=1300 -WinY=0 -NOSPLASH -LOG=fable_client.log"
  $clientProc = Start-Process -FilePath $exe -ArgumentList $clientArgs -PassThru
  "client pid $($clientProc.Id)"
}
Start-Sleep $Seconds
foreach ($p in @($hostProc, $clientProc)) { if ($p -and -not $p.HasExited) { try { $p.CloseMainWindow() | Out-Null } catch {} } }
Start-Sleep 10
foreach ($p in @($hostProc, $clientProc)) { if ($p -and -not $p.HasExited) { try { Stop-Process -Id $p.Id -Force } catch {} } }
"done"
