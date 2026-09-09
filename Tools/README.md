# Tools

Development helpers for this fork. PowerShell, because MSBuild's `/switches` are mangled by
Git Bash's MSYS path conversion — run all of these from PowerShell.

## Tail-SunriseLog.ps1

Streams the live Sunrise log to the terminal, so a log can be read and copied without
restarting the game. Sunrise opens its log with `FILE_SHARE_READ`, so following it is safe
while the game writes.

Survives a game restart: truncation (a new session reopening the same path) and rotation
(the file being renamed to `sunrise.log.old`) are both detected, and the reader re-attaches
to the new content instead of going silent.

```powershell
.\Tail-SunriseLog.ps1                                              # new lines only
.\Tail-SunriseLog.ps1 -Tail 50                                     # last 50 first, then follow
.\Tail-SunriseLog.ps1 -Filter 'ws50[12]|stage=select' -Tail 20     # roster path
.\Tail-SunriseLog.ps1 -Level error,warn -NoColor                   # problems, plain for pasting
.\Tail-SunriseLog.ps1 -Exclude 'ev=core|ev=nettick'                # drop routine noise
```

`-Tail N` keeps the last N lines that **match** the filter, not the last N lines overall, so a
filter still shows history when recent traffic is something else.

## Build-Sunrise.ps1

```powershell
.\Build-Sunrise.ps1                      # incremental Release x64
.\Build-Sunrise.ps1 -Rebuild             # full rebuild; use after removing files
.\Build-Sunrise.ps1 -Rebuild -Deploy     # build then deploy
```

Finds MSBuild via `vswhere` rather than a pinned path, so a Visual Studio update does not
silently break it. The project builds `/W4 /WX`, so any warning already fails the build; the
script surfaces the first diagnostics rather than making you read the whole log.

## Deploy-Sunrise.ps1

```powershell
.\Deploy-Sunrise.ps1                                    # copy the build in
.\Deploy-Sunrise.ps1 -Backup -Label roster -BackupState # snapshot DLL and save first
```

Refuses to deploy while Destiny 2 is running, because the DLL is locked and a partial copy is
worse than no copy.

Backups are **opt-in**. Each DLL backup is ~58 MB; they were previously made on every deploy
and reached 1.37 GB across 66 files. Use `-Backup` for a change you might need to walk back,
not by habit. `-BackupState` additionally snapshots `state.json`, which is worth it whenever a
change touches persistence or the roster — a bad build can rewrite the save.

The only deploy target is `Destiny2-Unvaulting`. The Steam install is never written to.
