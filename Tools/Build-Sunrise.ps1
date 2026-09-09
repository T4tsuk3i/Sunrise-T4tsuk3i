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

.PARAMETER Jobs
    Parallel compiler processes. Defaults to physical cores minus one, so the desktop keeps a
    core to run on. The project sets MultiProcessorCompilation, which otherwise spawns one
    cl.exe per *logical* processor; on a 4-core part that is eight compilers fighting over four
    cores, which is what makes the machine stall rather than merely be busy.

.PARAMETER Priority
    Priority class for MSBuild and every compiler it spawns (children inherit it).
    BelowNormal by default: the build takes about the same wall-clock time but stops competing
    with the UI for scheduling.

.PARAMETER Fast
    Disable whole-program optimisation for this build. LTCG is the slowest, heaviest part of a
    link and exists to make the shipped binary faster, which is irrelevant while iterating.
    Produces a working DLL that is marginally slower at runtime. Do not use for a release you
    intend to keep.

.EXAMPLE
    .\Build-Sunrise.ps1

.EXAMPLE
    .\Build-Sunrise.ps1 -Fast -Deploy
    The usual iteration loop: quick link, straight onto the install.

.EXAMPLE
    .\Build-Sunrise.ps1 -Jobs 2 -Priority Idle
    Build in the background while doing something else.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [switch] $Rebuild,
    [switch] $Deploy,
    [int]    $Jobs = 0,
    [ValidateSet('Idle', 'BelowNormal', 'Normal')]
    [string] $Priority = 'BelowNormal',
    [switch] $Fast
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

if ($Jobs -le 0) {
    $physical = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum
    if (-not $physical) { $physical = 2 }
    $Jobs = [Math]::Max(1, $physical - 1)
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }

$msbuildArgs = @(
    $solution
    "/t:$target"
    "/p:Configuration=$Configuration"
    '/p:Platform=x64'
    "/m:$Jobs"            # parallel projects
    "/p:CL_MPCount=$Jobs" # parallel cl.exe within a project, the one that actually saturates
    '/v:minimal'
    '/nologo'
)
if ($Fast) { $msbuildArgs += '/p:WholeProgramOptimization=false' }

$notes = "$Jobs jobs, $Priority priority"
if ($Fast) { $notes += ', LTCG off' }
Write-Host "Building $Configuration x64 ($target) - $notes" -ForegroundColor Cyan

# The process is managed directly rather than through Start-Process: -PassThru does not
# reliably populate ExitCode, which made this script report a failure on builds that had in
# fact succeeded. Starting it this way also lets the priority class be lowered immediately
# after launch, before the compilers spawn -- children inherit it, and that is what keeps the
# desktop responsive rather than merely busy.
$quoted = $msbuildArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName               = $msbuild
$psi.Arguments              = ($quoted -join ' ')
$psi.UseShellExecute        = $false
$psi.CreateNoWindow         = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true

$proc = New-Object System.Diagnostics.Process
$proc.StartInfo = $psi

$started = Get-Date
[void]$proc.Start()
try { $proc.PriorityClass = $Priority } catch { }

# Read both pipes asynchronously. Draining one then the other deadlocks as soon as the pipe
# that is not being read fills its buffer, which a 941-file build does immediately.
$outTask = $proc.StandardOutput.ReadToEndAsync()
$errTask = $proc.StandardError.ReadToEndAsync()
$proc.WaitForExit()

$code = $proc.ExitCode
$elapsed = (Get-Date) - $started
$output = @(($outTask.Result + "`n" + $errTask.Result) -split "`r?`n")

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
