# Resolve paths relative to this script's location
$scriptDir    = $PSScriptRoot
$projectPath  = Join-Path $scriptDir "mh-overlay.vcxproj"
$source       = Join-Path $scriptDir "build\dinput8.dll"
$destination  = "C:\Games\Marvel Heroes Omega 2.16a Steam\UnrealEngine3\Binaries\Win64\dinput8.dll"

# Locate MSBuild via vswhere (works for any VS 2017+ installation)
$vswhere   = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuildPath = & $vswhere -latest -requires Microsoft.Component.MSBuild `
                          -find "MSBuild\**\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
if (-not $msbuildPath) {
    Write-Error "Could not locate MSBuild via vswhere. Is Visual Studio installed?"
    exit 1
}

# Run MSBuild
Write-Host "Starting Build..." -ForegroundColor Cyan
& $msbuildPath $projectPath /p:Configuration=Release /t:Rebuild /p:Platform=x64

# Copy on success
if ($LASTEXITCODE -eq 0) {
    Write-Host "Build Succeeded! Copying files..." -ForegroundColor Green
    Copy-Item $source $destination -Force
    Write-Host "Copy Complete." -ForegroundColor Green
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}
