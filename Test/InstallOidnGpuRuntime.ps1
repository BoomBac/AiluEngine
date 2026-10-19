param([string]$InstallRoot = (Join-Path $PSScriptRoot '../Engine/Ext/Packages/oidn-2.5.1'))
$ErrorActionPreference = 'Stop'
$install_path = [IO.Path]::GetFullPath($InstallRoot)
$archive_path = Join-Path $install_path 'oidn-2.5.1.x64.windows.zip'
$package_path = Join-Path $install_path 'package'
$expected_hash = 'f11f91bc072a5e3a564515724cb72ab8fcfbc445c84a84c197ff9b16cc01396f'
New-Item -ItemType Directory -Path $install_path -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive_path)) {
    Invoke-WebRequest -Uri 'https://github.com/RenderKit/oidn/releases/download/v2.5.1/oidn-2.5.1.x64.windows.zip' `
        -OutFile $archive_path -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $archive_path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected_hash) {
    throw 'OIDN archive SHA256 differs from the official v2.5.1 Windows release asset.'
}
if (-not (Test-Path -LiteralPath $package_path)) {
    Expand-Archive -LiteralPath $archive_path -DestinationPath $package_path
}
$runtime_dll = Get-ChildItem -LiteralPath $package_path -Recurse -Filter OpenImageDenoise.dll | Select-Object -First 1
if (-not $runtime_dll) { throw 'OpenImageDenoise.dll missing from extracted package.' }
Write-Output $runtime_dll.Directory.FullName
