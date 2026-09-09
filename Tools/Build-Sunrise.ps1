<#
.SYNOPSIS
    Builds steam_api64.dll from this repository.

.DESCRIPTION
    Wraps MSBuild so a build is one command with consistent switches. The project builds
    /W4 /WX, so any warning is already a hard failure; this script surfaces the first
    diagnostics rather than making you read the whole log.

    Note for Git Bash users: MSBuild's /switches are mangled by MSYS path conversion
    ("/t:Build" becomes a Windows path). Run this from PowerShell, or the switches will not
    reach MSBuild intact.

.PARAMETER Configuration
    Release (default) or Debug.

.PARAMETER Rebuild
    Full rebuild instead of an incremental build. Use after removing files from the project.

.PARAMETER Deploy
    Run Deploy-Sunrise.ps1 on success.

.EXAMPLE
    .\Build-Sunrise.ps1

.EXAMPLE
    .\Build-Sunrise.ps1 -Rebuild -Deploy
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [switch] $Rebuild,
    [switch] $Deploy
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $repoRoot 'Sunrise.sln'

if (-not (Test-Path -LiteralPath $solution)) {
    Write-Error "Solution not found at $solution"
    exit 1
}

# Prefer a discovered install over a pinned path so a VS update does not silently break this.
$msbuild = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                          -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild) {
    $fallback = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    if (Test-Path -LiteralPath $fallback) { $msbuild = $fallback }
}
if (-not $msbuild) {
    Write-Error 'MSBuild not found. Install the Desktop development with C++ workload.'
    exit 1
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
Write-Host "Building $Configuration x64 ($target)" -ForegroundColor Cyan

$started = Get-Date
$output = & $msbuild $solution "/t:$target" "/p:Configuration=$Configuration" '/p:Platform=x64' '/v:minimal' '/nologo'
$code = $LASTEXITCODE
$elapsed = (Get-Date) - $started

$problems = $output | Select-String -Pattern ': (error|warning) ' | Select-Object -First 20
if ($problems) {
    Write-Host ''
    foreach ($p in $problems) {
        $color = if ($p -match ': error ') { 'Red' } else { 'Yellow' }
        Write-Host $p.Line.Trim() -ForegroundColor $color
    }
}

if ($code -ne 0) {
    Write-Host "`nBUILD FAILED after $([int]$elapsed.TotalSeconds)s" -ForegroundColor Red
    exit $code
}

$dll = Join-Path $repoRoot "build\x64\$Configuration\steam_api64.dll"
if (Test-Path -LiteralPath $dll) {
    $item = Get-Item -LiteralPath $dll
    Write-Host ("`nBUILD OK  {0:N1} MB  {1}  ({2}s)" -f ($item.Length / 1MB), $item.LastWriteTime, [int]$elapsed.TotalSeconds) -ForegroundColor Green
} else {
    Write-Host "`nBUILD OK but $dll not found" -ForegroundColor Yellow
}

if ($Deploy) {
    Write-Host ''
    & (Join-Path $PSScriptRoot 'Deploy-Sunrise.ps1') -Configuration $Configuration
}
