@echo off
setlocal

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0PackAiluEngineSource.ps1"
set "exit_code=%ERRORLEVEL%"

if not "%exit_code%" == "0" (
    echo.
    echo Packaging failed. Exit code: %exit_code%
) else (
    echo.
    echo Packaging completed.
)

pause
exit /b %exit_code%
