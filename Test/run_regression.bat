@echo off
cd /d F:\AiluEngine\artifacts\out\test\bin\x64\debug
echo ========================================
echo   Running ScriptRegression...
echo ========================================
ScriptRegression.exe --verbose
echo.
echo EXIT_CODE=%ERRORLEVEL%
