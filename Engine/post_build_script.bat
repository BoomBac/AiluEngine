@echo off
chcp 65001 >nul
rem [PostEngineBuild]: EngineLib has compiled successfully, will copy it to editor exe directory...
set "CONFIG=%~1"
set "ENGINE_DLL=%~2"
set "AILU_BUILD_ROOT=%~3"
set "ENGINE_DLL_NAME=%~nx2"

if "%CONFIG%"=="" exit /b 1
if "%ENGINE_DLL%"=="" exit /b 1
if "%AILU_BUILD_ROOT%"=="" exit /b 1
if "%ENGINE_DLL_NAME%"=="" exit /b 1

set "ENGINE_DLL=%ENGINE_DLL:/=\%"
set "AILU_BUILD_ROOT=%AILU_BUILD_ROOT:/=\%"

set "CONFIG_DIR=%CONFIG%"
if /I "%CONFIG%"=="Debug" set "CONFIG_DIR=debug"
if /I "%CONFIG%"=="Release" set "CONFIG_DIR=release"
if /I "%CONFIG%"=="RelWithDebInfo" set "CONFIG_DIR=relwithdebinfo"
if /I "%CONFIG%"=="MinSizeRel" set "CONFIG_DIR=minsizerel"

if not exist "%AILU_BUILD_ROOT%\out\editor\bin\x64\%CONFIG_DIR%\" mkdir "%AILU_BUILD_ROOT%\out\editor\bin\x64\%CONFIG_DIR%\"
if not exist "%AILU_BUILD_ROOT%\out\test\bin\x64\%CONFIG_DIR%\" mkdir "%AILU_BUILD_ROOT%\out\test\bin\x64\%CONFIG_DIR%\"

copy /y "%ENGINE_DLL%" "%AILU_BUILD_ROOT%\out\editor\bin\x64\%CONFIG_DIR%\%ENGINE_DLL_NAME%" >nul
copy /y "%ENGINE_DLL%" "%AILU_BUILD_ROOT%\out\test\bin\x64\%CONFIG_DIR%\%ENGINE_DLL_NAME%" >nul
