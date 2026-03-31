# uninstall-win.ps1 - Remove clap-nr.clap and runtime DLLs from %CommonProgramFiles%\CLAP\
# Automatically requests elevation if not running as Administrator.

# -- Auto-elevate if not running as Administrator ------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Requesting administrator privileges..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# -- Uninstallation ------------------------------------------------------------
$ErrorActionPreference = "Stop"

$DEST = "$env:CommonProgramFiles\CLAP"

Write-Host "Uninstalling clap-nr from $DEST..." -ForegroundColor Cyan

# Files to remove
$files = @(
    "clap-nr.clap",
    "libfftw3-3.dll",
    "libfftw3f-3.dll",
    "rnnoise.dll",
    "rnnoise_avx2.dll",
    "rnnoise_weights_small.bin",
    "rnnoise_weights_large.bin",
    "specbleach.dll"
)

$removedCount = 0
$notFoundCount = 0

foreach ($file in $files) {
    $fullPath = Join-Path $DEST $file
    
    if (Test-Path $fullPath) {
        try {
            Remove-Item $fullPath -Force
            Write-Host "Removed: $file" -ForegroundColor Gray
            $removedCount++
        }
        catch {
            Write-Host "ERROR: Failed to delete $file" -ForegroundColor Red
            Write-Host "If your CLAP host is open, close it and try again." -ForegroundColor Yellow
            Read-Host "Press Enter to exit"
            exit 1
        }
    }
    else {
        $notFoundCount++
    }
}

Write-Host ""
if ($removedCount -gt 0) {
    Write-Host "SUCCESS: Removed $removedCount file(s) from $DEST" -ForegroundColor Green
}
if ($notFoundCount -eq $files.Count) {
    Write-Host "Plugin not installed at $DEST - nothing to remove." -ForegroundColor Yellow
}
Write-Host ""
Read-Host "Press Enter to exit"
