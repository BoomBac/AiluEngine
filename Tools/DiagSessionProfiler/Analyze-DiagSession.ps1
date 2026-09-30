<#
.SYNOPSIS
    Runs DiagSessionProfiler on a Visual Studio .diagsession file and prints the LLM ready outputs.

.EXAMPLE
    pwsh Tools/DiagSessionProfiler/Analyze-DiagSession.ps1 -Diagsession D:\profiles\opti_baseline.diagsession

.EXAMPLE
    pwsh Tools/DiagSessionProfiler/Analyze-DiagSession.ps1 -Diagsession .\opti.diagsession -Process Editor.exe -Top 60 -Force
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Diagsession,
    [string]$Output,
    [string]$Process,
    [int]$Top = 40,
    [string]$Cache,
    [string]$SymbolPath,
    [switch]$DeepSymbols,
    [switch]$NoSystemSymbols,
    [switch]$CommandLines,
    [switch]$Force,
    [switch]$KeepIntermediate,
    [switch]$NoBuild,
    [string[]]$AdditionalArgs = @()
)

$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'DiagSessionProfiler.csproj'
$dll = Join-Path $PSScriptRoot 'bin/Debug/net8.0/DiagSessionProfiler.dll'

$sourceFiles = Get-ChildItem -Path $PSScriptRoot -Filter *.cs -Recurse -File
$needsBuild = $NoBuild.IsPresent -eq $false -and (
    -not (Test-Path $dll) -or
    (Get-Item $dll).LastWriteTime -lt ($sourceFiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
)

if (-not (Test-Path $dll) -and $NoBuild) { throw "DiagSessionProfiler.dll is missing; run once without -NoBuild" }

if ($needsBuild) {
    Write-Host 'building DiagSessionProfiler...'
    dotnet build $project -v q --nologo | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

$inputPath = (Resolve-Path -LiteralPath $Diagsession).Path
$outputPath = if ($Output) { [System.IO.Path]::GetFullPath($Output) } else { Join-Path (Split-Path -Parent $inputPath) ([System.IO.Path]::GetFileNameWithoutExtension($inputPath) + '.profile') }

$arguments = @($dll, 'all', '-i', $inputPath, '-o', $outputPath, '--top', $Top)

if ($Cache) { $arguments += @('-c', $Cache) }
if ($Process) { $arguments += @('-p', $Process) }
if ($SymbolPath) { $arguments += @('--symbol-path', $SymbolPath) }
if ($DeepSymbols) { $arguments += '--deep-symbols' }
if ($NoSystemSymbols) { $arguments += '--no-system-symbols' }
if ($CommandLines) { $arguments += '--command-lines' }
if ($Force) { $arguments += '--force' }
if ($KeepIntermediate) { $arguments += '--keep-intermediate' }
$arguments += $AdditionalArgs

& dotnet @arguments
if ($LASTEXITCODE -ne 0) { throw "DiagSessionProfiler failed with exit code $LASTEXITCODE" }

$outputDir = $outputPath
Write-Host ''
Write-Host 'outputs:' -ForegroundColor Green
foreach ($file in 'report.md', 'profile.json', 'prompt.md', 'run.log') {
    $path = Join-Path $outputDir $file
    if (Test-Path $path) {
        Write-Host ("  {0}  ({1:N0} bytes)" -f $path, (Get-Item $path).Length)
    }
}
Write-Host ''
Write-Host 'next: paste report.md into an LLM together with the instruction from prompt.md' -ForegroundColor Cyan
