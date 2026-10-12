# Switches to main, pulls latest, and creates a new feature branch.
param([Parameter(Mandatory=$true)][string]$Branch)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (git status --porcelain -- . ':!.vscode') { throw "Uncommitted changes - run CheckIn first." }
git checkout main
git pull origin main
git checkout -b $Branch
Write-Host "Now on $Branch (from latest main)" -ForegroundColor Green
