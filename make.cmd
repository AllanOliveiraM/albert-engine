@echo off
rem Started from PowerShell 7, Windows PowerShell would inherit its module path
rem and miss its own script modules (Get-FileHash, Expand-Archive).
set "PSModulePath="
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup-windows.ps1" -Task render-build
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup-windows.ps1" -Task %*
)
exit /b %ERRORLEVEL%
