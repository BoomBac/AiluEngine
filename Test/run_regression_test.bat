@echo off
cd /d "F:\AiluEngine\artifacts\out\test\bin\x64\debug"
echo ========================================
echo   AiluEngine T00 Script Regression
echo ========================================
ScriptRegression.exe --verbose 2>&1
set EXITCODE=%ERRORLEVEL%
echo ========================================
echo   Exit code: %EXITCODE%
echo ========================================
exit /b %EXITCODE%
