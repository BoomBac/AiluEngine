@echo off
:: 获取当前批处理文件所在目录
set currentDir=%~dp0

:: 获取上一级目录（去除尾部的反斜杠）
set parentDir=%currentDir:~0,-1%
set parentDir=%parentDir%\..

:: 强制用 cmd 运行 exe
cmd /c "%parentDir%\bin\x64\debug\AiluHeadTool.exe"
