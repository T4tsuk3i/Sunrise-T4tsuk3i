<#
.SYNOPSIS
    Streams the live Sunrise log to this terminal.

.DESCRIPTION
    Sunrise opens its log with FILE_SHARE_READ, so the file can be read while the game is
    writing it. This follows the file the way `tail -f` does, but it also survives the two
    things that happen around a game restart:

      - truncation, when a new session reopens the same path and the length drops below the
        position we had reached
      - rotation, when the previous session's file is renamed to sunrise.log.old

    Both are detected and the reader re-attaches at the start of the new content, so the
    terminal keeps streaming across a restart instead of going silent.

.PARAMETER Path
    Log file to follow. Defaults to the deployed location.

.PARAMETER Filter
    Regex applied to each line. Only matching lines are printed.

.PARAMETER Exclude
    Regex applied to each line. Matching lines are dropped. Applied after -Filter.

.PARAMETER Level
    Keep only these levels. Accepts any of error, warn, info, debug.

.PARAMETER Tail
    Lines of existing content to show before following. Default 0 (new lines only).
    Use -Tail -1 to print the whole file first.

.PARAMETER NoColor
    Print without colour, for clean copy-paste.

.EXAMPLE
    .\Tail-SunriseLog.ps1
    Follow new lines only.

.EXAMPLE
    .\Tail-SunriseLog.ps1 -Filter 'ws50[12]|roster|stage=select' -Tail 50
    Follow the roster path, showing the last 50 lines first.

.EXAMPLE
    .\Tail-SunriseLog.ps1 -Level error,warn -NoColor
    Only problems, plain text for pasting.
#>
[CmdletBinding()]
param(
    [string]   $Path    = 'C:\Users\Tatsuya\Pictures\Destiny2-Unvaulting\bin\x64\Sunrise\logs\sunrise.log',
    [string]   $Filter,
    [string]   $Exclude,
    [ValidateSet('error', 'warn', 'info', 'debug')]
    [string[]] $Level,
    [int]      $Tail    = 0,
    [switch]   $NoColor
)

$ErrorActionPreference = 'Stop'

# Sunrise prefixes every line with "<channel> level=<level> ", so the level is matched on that
# token rather than anywhere in the line: a payload containing the word "error" is not an error.
$levelPattern = if ($Level) { '\blevel=(' + ($Level -join '|') + ')\b' } else { $null }

$colors = @{ error = 'Red'; warn = 'Yellow'; info = 'Gray'; debug = 'DarkGray' }

# Kept separate from printing so -Tail can seed the last N *matching* lines. Filtering after
# taking the last N would show nothing whenever the filter does not match recent traffic,
# which is exactly when a tail is most wanted.
function Test-LogLine {
    param([string] $Line)

    if ($levelPattern -and $Line -notmatch $levelPattern) { return $false }
    if ($Filter        -and $Line -notmatch $Filter)      { return $false }
    if ($Exclude       -and $Line -match  $Exclude)       { return $false }
    return $true
}

function Write-LogLine {
    param([string] $Line)

    if (-not (Test-LogLine -Line $Line)) { return }

    if ($NoColor) { Write-Output $Line; return }

    $color = 'Gray'
    if ($Line -match '\blevel=(\w+)\b' -and $colors.ContainsKey($Matches[1])) {
        $color = $colors[$Matches[1]]
    }
    Write-Host $Line -ForegroundColor $color
}

# FileShare must include Delete: the game may rename this file out from under us on rotation,
# and without it the rename fails and the game's own logging breaks. Never hold a lock the
# writer does not expect.
function Open-LogStream {
    param([string] $LogPath)
    return [System.IO.FileStream]::new(
        $LogPath,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
}

Write-Host "Following $Path" -ForegroundColor Cyan
$described = @()
if ($Level)   { $described += "level=$($Level -join ',')" }
if ($Filter)  { $described += "filter=$Filter" }
if ($Exclude) { $described += "exclude=$Exclude" }
if ($described.Count) { Write-Host ("  " + ($described -join '  ')) -ForegroundColor DarkCyan }
Write-Host "Ctrl+C to stop.`n" -ForegroundColor DarkCyan

$position = 0L
$waitingAnnounced = $false

while ($true) {
    if (-not (Test-Path -LiteralPath $Path)) {
        if (-not $waitingAnnounced) {
            Write-Host "[waiting for $Path to appear]" -ForegroundColor DarkYellow
            $waitingAnnounced = $true
        }
        Start-Sleep -Milliseconds 500
        continue
    }
    $waitingAnnounced = $false

    try {
        $stream = Open-LogStream -LogPath $Path
    } catch {
        Start-Sleep -Milliseconds 300
        continue
    }

    try {
        if ($Tail -lt 0) {
            $position = 0L
        } elseif ($Tail -gt 0) {
            # Seed from the end by walking back over the requested number of lines.
            $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true, 4096, $true)
            $seed = [System.Collections.Generic.Queue[string]]::new()
            while (($line = $reader.ReadLine()) -ne $null) {
                if (-not (Test-LogLine -Line $line)) { continue }
                $seed.Enqueue($line)
                if ($seed.Count -gt $Tail) { [void]$seed.Dequeue() }
            }
            foreach ($line in $seed) { Write-LogLine -Line $line }
            $reader.Dispose()
            $position = $stream.Length
        } else {
            $position = $stream.Length
        }
        $Tail = 0   # only seed once, not again after a reattach

        $stream.Seek($position, [System.IO.SeekOrigin]::Begin) | Out-Null
        $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true, 4096, $true)

        while ($true) {
            $line = $reader.ReadLine()
            if ($null -ne $line) {
                Write-LogLine -Line $line
                continue
            }

            $position = $stream.Position
            Start-Sleep -Milliseconds 200

            # A new session truncates the file. Detect it by the length falling behind where we
            # already are, and restart from the beginning of the new content.
            $current = try { (Get-Item -LiteralPath $Path).Length } catch { -1 }
            if ($current -lt 0) { break }          # rotated or deleted; reattach
            if ($current -lt $position) {
                Write-Host "`n[log truncated - new session]`n" -ForegroundColor Cyan
                $position = 0L
                break
            }
        }
    } finally {
        if ($reader) { $reader.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
}
