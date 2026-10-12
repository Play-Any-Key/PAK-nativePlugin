@echo off
setlocal

for /f "tokens=2" %%A in ('findstr /r /c:"return.*[0-9][0-9]*\.[0-9]" "%~dp0src\main.cpp"') do set VERSION=%%A
set VERSION=%VERSION:"=%
set VERSION=%VERSION:;=%
for /f "tokens=1-3 delims=." %%A in ("%VERSION%") do (
    set /a MAJOR=%%A
    set /a MINOR=%%B
    set /a PATCH=%%C
)

choice /c 012 /n /m "Select version increase (0: Patch, 1: Minor, 2: Major)"
if errorlevel 3 goto major
if errorlevel 2 goto minor
goto patch

:patch
set /a PATCH+=1
goto version_ready

:minor
(
    set /a MINOR+=1
    set PATCH=0
)
goto version_ready

:major
(
    set /a MAJOR+=1
    set MINOR=0
    set PATCH=0
)
goto version_ready

:version_ready

set NEW_VERSION=%MAJOR%.%MINOR%.%PATCH%
echo Version: %VERSION% ^> %NEW_VERSION%
choice /c 10 /n /m "Apply this version change (1: Apply, 0: Cancel)"
if errorlevel 2 (
    echo Version update cancelled.
    exit /b 0
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0versionUp.ps1" "%~dp0." "%NEW_VERSION%"
if errorlevel 1 (
    echo Version update failed.
    exit /b 1
)

echo Version updated to %NEW_VERSION%
