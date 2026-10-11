<#
.SYNOPSIS
Waits for Unreal to exit, builds the editor target, and relaunches the project.

.DESCRIPTION
Save your work and close Unreal Editor before running this script. It never
terminates Unreal processes; it polls until the editor, game, and Live Coding
processes have exited, then builds MazeDemonsEditor and opens the project.

.EXAMPLE
.\Scripts\BuildAndRelaunchEditor.ps1

.EXAMPLE
.\Scripts\BuildAndRelaunchEditor.ps1 -EngineRoot "E:\Epic Games\UE_5.8"
#>
[CmdletBinding()]
param(
	[ValidateRange(1, 300)]
	[int]$PollSeconds = 5,

	[string]$EngineRoot = 'D:\Epic Games\UE_5.8'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'MazeDemons.uproject'

if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
	throw "Unreal project file not found: $projectFile"
}

if (-not (Test-Path -LiteralPath $EngineRoot -PathType Container)) {
	throw "Unreal Engine directory not found: $EngineRoot. Pass the installed engine directory with -EngineRoot."
}

$buildScript = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editorExecutable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'

if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
	throw "Unreal Build.bat not found: $buildScript"
}

if (-not (Test-Path -LiteralPath $editorExecutable -PathType Leaf)) {
	throw "Unreal Editor executable not found: $editorExecutable"
}

$processNames = @('UnrealEditor', 'UnrealEditor-Cmd', 'LiveCodingConsole')
Write-Host 'Waiting for Unreal Editor, game, and Live Coding processes to exit. Save your work before closing the editor.'

while ($true) {
	$runningProcesses = @(
		Get-Process -ErrorAction SilentlyContinue |
			Where-Object {
				$processNames -contains $_.ProcessName -or $_.ProcessName -like 'MazeDemons*'
			}
	)

	if ($runningProcesses.Count -eq 0) {
		break
	}

	$runningNames = ($runningProcesses | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
	Write-Host "Still running: $runningNames. Checking again in $PollSeconds seconds."
	Start-Sleep -Seconds $PollSeconds
}

Write-Host 'Unreal processes have exited. Building MazeDemonsEditor (Win64 Development)...'
& $buildScript 'MazeDemonsEditor' 'Win64' 'Development' "-Project=`"$projectFile`"" '-WaitMutex'

if ($LASTEXITCODE -ne 0) {
	throw "Unreal Editor build failed with exit code $LASTEXITCODE. The editor was not relaunched."
}

Write-Host 'Build succeeded. Relaunching MazeDemons...'
$editorProcess = Start-Process `
	-FilePath $editorExecutable `
	-ArgumentList @("`"$projectFile`"") `
	-WorkingDirectory $projectRoot `
	-PassThru

Write-Host "Unreal Editor launched (PID $($editorProcess.Id))."
