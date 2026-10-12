# Commits all working-tree changes and pushes.
# On main, pass -Branch to move the uncommitted work onto a new feature branch first.
#   .\CheckIn.bat -Branch feature/my-work -Message "What changed"
param(
    [string]$Message = "Work in progress",
    [string]$Branch = ""
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$current = git rev-parse --abbrev-ref HEAD
if ($current -eq 'main') {
    if (-not $Branch) { throw "On main - refusing to commit directly. Re-run with -Branch <feature/name>." }
    git checkout -b $Branch
    $current = $Branch
}
Write-Host "Branch: $current" -ForegroundColor Cyan
git status --short
git add -A -- . ':!.vscode'
$body = @"
$Message

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_014KVsW8XSbgxPJGW8Nk3MpS
"@
git commit -m $body
git push -u origin $current
Write-Host "Pushed $current" -ForegroundColor Green
