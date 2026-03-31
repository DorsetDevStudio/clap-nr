# build-win.ps1 - Build clap-nr on Windows (Release configuration)
# Runs CMake build for the already-configured project.

$ErrorActionPreference = "Stop"

$buildDir = Join-Path $PSScriptRoot "build"

Write-Host "Building clap-nr (Release)..." -ForegroundColor Cyan
Write-Host ""

try {
    cmake --build $buildDir --config Release
    
    if ($LASTEXITCODE -eq 0) {
        $pluginPath = Join-Path $buildDir "Release\clap-nr.clap"
        Write-Host ""
        Write-Host "SUCCESS: Build complete" -ForegroundColor Green
        Write-Host "Output: $pluginPath" -ForegroundColor Green
        Write-Host ""
        Write-Host "Next steps:" -ForegroundColor Yellow
        Write-Host "  - Test: .\test\test-plugin.ps1" -ForegroundColor Gray
        Write-Host "  - Install: .\install-win.ps1" -ForegroundColor Gray
    }
    else {
        Write-Host ""
        Write-Host "ERROR: Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
        exit 1
    }
}
catch {
    Write-Host ""
    Write-Host "ERROR: Build failed - $_" -ForegroundColor Red
    exit 1
}
