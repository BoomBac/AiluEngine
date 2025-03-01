@echo off
chcp 65001 >nul
rem [PostEngineBuild]: EngineLib has compiled successfully, will copy it to editor exe directory...
xcopy /y /d "F:\AiluBuild\out\engine\lib\x64\Debug\Engine_d.dll" "F:\AiluBuild\out\editor\bin\x64\debug\"
xcopy /y /d "F:\AiluBuild\out\engine\lib\x64\Debug\Engine_d.dll" "F:\AiluBuild\out\test\bin\x64\debug\"
