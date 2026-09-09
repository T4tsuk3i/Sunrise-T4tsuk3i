<#
.SYNOPSIS
    Copies the built steam_api64.dll into the Destiny2-Unvaulting install.

.DESCRIPTION
    The DLL is locked while the game runs, so this refuses to deploy rather than failing
    halfway. Backups are opt-in: they are 58 MB each and accumulated to over a gigabyte
    before, so -Backup is there for a risky change rather than being the default.

    The only valid deploy target is Destiny2-Unvaulting. The Steam install is never written to.

.PARAMETER Configuration
    Which build to deploy. Release (default) or Debug.

.PARAMETER Backup
    Keep a timestamped copy of the DLL being replaced.

.PARAMETER Label
    Short tag for the backup name, e.g. -Backup -Label roster.

.PARAMETER BackupState
    Also snapshot state.json before deploying. Worth it when the change touches persistence
    or the roster, since a bad build can rewrite the save.

.EXAMPLE
    .\Deploy-Sunrise.ps1

.EXAMPLE
    .\Deploy-Sunrise.ps1 -Backup -Label roster -BackupState
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [switch] $Backup,
    [string] $Label = 'deploy',
    [switch] $BackupState
)

$ErrorActionPreference = 'Stop'

$repoRoot  = Split-Path -Parent $PSScriptRoot
$source    = Join-Path $repoRoot "build\x64\$Configuration\steam_api64.dll"
$installRoot = 'C:\Users\Tatsuya\Pictures\Destiny2-Unvaulting'
$target    = Join-Path $installRoot 'bin\x64\steam_api64.dll'
$stateFile = Join-Path $installRoot 'bin\x64\Sunrise\state.json'

if (-not (Test-Path -LiteralPath $source)) {
    Write-Error "No build at $source. Run Build-Sunrise.ps1 first."
    exit 1
}
if (-not (Test-Path -LiteralPath (Split-Path -Parent $target))) {
    Write-Error "Install not found at $installRoot"
    exit 1
}

$game = Get-Process -Name 'destiny2' -ErrorAction SilentlyContinue
if ($game) {
    Write-Host "Destiny 2 is running (pid $($game.Id)). The DLL is locked; close it first." -ForegroundColor Red
    exit 1
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

if ($Backup -and (Test-Path -LiteralPath $target)) {
    $dllBackup = "$target.bak-$Label-$stamp"
    Copy-Item -LiteralPath $target -Destination $dllBackup -Force
    Write-Host "backup  $(Split-Path -Leaf $dllBackup)" -ForegroundColor DarkGray
}

if ($BackupState -and (Test-Path -LiteralPath $stateFile)) {
    $stateBackup = "$stateFile.bak-$Label-$stamp"
    Copy-Item -LiteralPath $stateFile -Destination $stateBackup -Force
    Write-Host "backup  $(Split-Path -Leaf $stateBackup)" -ForegroundColor DarkGray
}

Copy-Item -LiteralPath $source -Destination $target -Force

$item = Get-Item -LiteralPath $target
Write-Host ("deployed {0:N1} MB  {1}" -f ($item.Length / 1MB), $item.LastWriteTime) -ForegroundColor Green
Write-Host "         $target" -ForegroundColor DarkGray
