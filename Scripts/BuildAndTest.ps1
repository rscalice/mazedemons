<#
.SYNOPSIS
Waits for Unreal to exit, builds MazeDemonsEditor, runs the headless automation tests, then relaunches the editor.

.DESCRIPTION
Save your work and close Unreal Editor before running this script (it never kills Unreal).
Outputs go to <project>\Saved\TestResults:
  build.log        full build output
  tests.log        UnrealEditor-Cmd output of the test run
  summary.txt      PASS/FAIL line per test plus totals (also printed to the console)
  report\          Automation JSON report (index.json) from -ReportExportPath

.EXAMPLE
.\Scripts\BuildAndTest.ps1

.EXAMPLE
.\Scripts\BuildAndTest.ps1 -TestFilter "MazeDemons" -NoRelaunch
#>
[CmdletBinding()]
param(
	[ValidateRange(1, 300)]
	[int]$PollSeconds = 5,

	[string]$EngineRoot = 'D:\Epic Games\UE_5.8',

	# Automation filter passed to "Automation RunTests <filter>".
	[string]$TestFilter = 'MazeDemons.Inventory',

	[switch]$NoRelaunch,
	[switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'MazeDemons.uproject'
$outDir = Join-Path $projectRoot 'Saved\TestResults'
$reportDir = Join-Path $outDir 'report'
$buildLog = Join-Path $outDir 'build.log'
$testLog = Join-Path $outDir 'tests.log'
$summaryFile = Join-Path $outDir 'summary.txt'

if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw "Unreal project file not found: $projectFile" }
if (-not (Test-Path -LiteralPath $EngineRoot -PathType Container)) { throw "Unreal Engine directory not found: $EngineRoot. Pass -EngineRoot." }

$buildScript = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$cmdExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
foreach ($p in @($buildScript, $editorExe, $cmdExe)) {
	if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Required engine file not found: $p" }
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (Test-Path $reportDir) { Remove-Item -Recurse -Force $reportDir }
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $buildLog, $testLog, $summaryFile

$processNames = @('UnrealEditor', 'UnrealEditor-Cmd', 'LiveCodingConsole')
Write-Host 'Waiting for Unreal processes to exit. Save your work and close the editor.'
while ($true) {
	$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
		$processNames -contains $_.ProcessName -or $_.ProcessName -like 'MazeDemons*'
	})
	if ($running.Count -eq 0) { break }
	Write-Host ("Still running: " + (($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', ') + ". Checking again in $PollSeconds s.")
	Start-Sleep -Seconds $PollSeconds
}

$exitCode = 0
$result = 'NOT RUN'

try {
	if (-not $SkipBuild) {
		Write-Host 'Building MazeDemonsEditor (Win64 Development)...'
		& $buildScript 'MazeDemonsEditor' 'Win64' 'Development' "-Project=`"$projectFile`"" '-WaitMutex' 2>&1 | Tee-Object -FilePath $buildLog | Out-Host
		if ($LASTEXITCODE -ne 0) {
			$result = "BUILD FAILED (exit $LASTEXITCODE) - see $buildLog"
			$exitCode = 1
			throw $result
		}
		Write-Host 'Build succeeded.'
	}

	Write-Host "Running automation tests: $TestFilter"
	$execCmds = "Automation RunTests $TestFilter; Quit"
	$cmdArgs = @(
		"`"$projectFile`"",
		"-ExecCmds=`"$execCmds`"",
		'-unattended', '-nullrhi', '-nosplash', '-NoSound', '-stdout', '-FullStdOutLogOutput',
		"-ReportExportPath=`"$reportDir`"",
		"-abslog=`"$testLog`""
	)
	$proc = Start-Process -FilePath $cmdExe -ArgumentList $cmdArgs -WorkingDirectory $projectRoot -PassThru -Wait -NoNewWindow
	Write-Host "UnrealEditor-Cmd exited with code $($proc.ExitCode)."

	# Summarise results from the log (lines produced by the automation framework).
	$lines = @()
	if (Test-Path $testLog) {
		$lines = Select-String -Path $testLog -Pattern 'Test Completed\. Result=|Error:|LogAutomationController: .*(Success|Fail)|Automation Test Queue Empty|\*\*\* TEST COMPLETE' |
			ForEach-Object { $_.Line }
	}
	$pass = @($lines | Where-Object { $_ -match 'Result=\{?Success' }).Count
	$fail = @($lines | Where-Object { $_ -match 'Result=\{?Fail' }).Count
	$summary = @()
	$summary += "Filter: $TestFilter"
	$summary += "Passed: $pass   Failed: $fail   ExitCode: $($proc.ExitCode)"
	$summary += '--- matching log lines ---'
	$summary += $lines
	$summary | Set-Content -Path $summaryFile
	$summary | Select-Object -First 60 | Out-Host

	if ($fail -gt 0 -or $proc.ExitCode -ne 0) {
		$result = "TESTS FAILED (passed $pass, failed $fail, exit $($proc.ExitCode))"
		$exitCode = 1
	} elseif ($pass -eq 0) {
		$result = 'NO TESTS RAN - check tests.log (filter wrong or tests not registered)'
		$exitCode = 2
	} else {
		$result = "ALL TESTS PASSED ($pass)"
	}
}
catch {
	if ($exitCode -eq 0) { $exitCode = 1; $result = "ERROR: $($_.Exception.Message)" }
}

Write-Host "RESULT: $result"
"RESULT: $result" | Add-Content -Path $summaryFile

if (-not $NoRelaunch -and $result -notlike 'BUILD FAILED*') {
	Write-Host 'Relaunching MazeDemons editor...'
	Start-Process -FilePath $editorExe -ArgumentList @("`"$projectFile`"") -WorkingDirectory $projectRoot | Out-Null
}

exit $exitCode
