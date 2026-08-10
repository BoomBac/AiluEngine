Set-Location 'F:\AiluEngine\artifacts\out\test\bin\x64\debug'
$env:Path = 'F:\AiluEngine\artifacts\out\test\bin\x64\debug;' + $env:Path
$output = & '.\ScriptRegression.exe' --verbose 2>&1
$output
exit $LASTEXITCODE
