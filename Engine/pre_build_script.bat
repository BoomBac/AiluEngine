@echo off
setlocal

set "AHT_EXE=%~1"
if "%AHT_EXE%"=="" set "AHT_EXE=%~dp0..\AiluHeadTool\bin\x64\debug\AiluHeadTool.exe"

if not exist "%AHT_EXE%" (
	echo [AiluHeadTool] executable not found: %AHT_EXE%
	exit /b 1
)

"%AHT_EXE%" -force
