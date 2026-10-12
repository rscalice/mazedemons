# Commits all working-tree changes on the current branch and pushes it.
param([string]$Message = "Phase 2 close-out + Phase 3 Step 0 multiplayer foundation")
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$branch = git rev-parse --abbrev-ref HEAD
Write-Host "Branch: $branch" -ForegroundColor Cyan
if ($branch -eq 'main') { throw "On main - refusing to commit directly. Switch to a feature branch first." }
git status --short
git add -A -- . ':!.vscode'
$body = @"
$Message

- GAS health/damage fully replaces BPC_DamageSystem; death via ASC OnDeath
- ASC + attributes moved to AMDPlayerState; AMDGameState with friendly fire (md.FriendlyFire)
- Server-authoritative damage via UMDDamageEffect (SetByCaller Data.Damage); fixes #9
- Test dummy: armorable UE5 Manny with replicated armor slots
- BP_Player/AC_Combat wired to PlayerState ASC; HUD local-only; death multicast
- Docs, build script fixes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VUnox5nc28Bq4rf8ujfbe1
"@
git commit -m $body
git push -u origin $branch
Write-Host "Pushed $branch" -ForegroundColor Green
