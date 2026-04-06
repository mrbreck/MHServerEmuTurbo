@echo off

set BUILD_DIR=.src\MHServerEmu\bin\Release\net8.0\linux-x64
set OUTPUT_DIR=.\build-linux

echo ==================
echo     Building...
echo ==================

dotnet build src\MHServerEmu\MHServerEmu.csproj -c Release -r linux-x64 -p:DefineConstants="TRACE;RELEASE"

if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b %errorlevel%
)

echo ==================
echo     Copying...
echo ==================

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
robocopy "%BUILD_DIR%" "%OUTPUT_DIR%" *.* /s /xf *.pdb *.xml /np /njs /njh

echo ==================
echo   Build Complete
echo ==================

pause