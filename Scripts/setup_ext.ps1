[CmdletBinding()]
param(
    [Alias('SourceRepository')][string]$source_repository,
    [Alias('FbxSdkRoot')][string]$fbx_sdk_root,
    [switch]$force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script_root = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$repo_root = (Resolve-Path -LiteralPath (Join-Path $script_root '..')).Path
$manifest = Get-Content -LiteralPath (Join-Path $script_root 'packages.json') -Raw | ConvertFrom-Json
$packages_root = [IO.Path]::GetFullPath((Join-Path $repo_root 'Engine\Ext\Packages'))

if (-not $source_repository) {
    $candidate = Join-Path (Split-Path -Parent $repo_root) 'AiluEngineOldd'
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        $source_repository = $candidate
    }
}
if ($source_repository) {
    $source_repository = (Resolve-Path -LiteralPath $source_repository).Path
}

function Get-TreeSha256([string]$path) {
    $base_path = (Resolve-Path -LiteralPath $path).Path.TrimEnd('\')
    $files = Get-ChildItem -LiteralPath $base_path -File -Recurse -Force
    $records = @(
        foreach ($file in $files) {
            $relative_path = $file.FullName.Substring($base_path.Length).TrimStart('\').Replace('\', '/').ToLowerInvariant()
            $file_hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$relative_path`t$($file.Length)`t$file_hash"
        }
    )
    [string[]]$sorted_records = $records
    [Array]::Sort($sorted_records, [StringComparer]::Ordinal)
    $canonical = [string]::Join("`n", $sorted_records)
    $bytes = [Text.Encoding]::UTF8.GetBytes($canonical)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Install-PackageTree($package, [string]$source_path) {
    if (-not (Test-Path -LiteralPath $source_path -PathType Container)) {
        throw "Package source folder not found: $source_path"
    }

    $destination = [IO.Path]::GetFullPath((Join-Path $repo_root ($package.installPath -replace '/', '\')))
    if (-not $destination.StartsWith($packages_root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package destination is outside Engine/Ext/Packages: $destination"
    }
    if (Test-Path -LiteralPath $destination -PathType Container) {
        $current_hash = Get-TreeSha256 $destination
        if ($current_hash -eq $package.treeSha256) {
            Write-Host "$($package.name) $($package.version): already installed and verified."
            return
        }
        if (-not $force) {
            throw "Existing package differs from the lock manifest: $destination. Use -Force to replace this package directory."
        }
        Remove-Item -LiteralPath $destination -Recurse -Force
    }

    $parent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $stage = Join-Path $parent ('.' + $package.name + '.setup-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        Get-ChildItem -LiteralPath $source_path -Force | Copy-Item -Destination $stage -Recurse -Force
        $actual_hash = Get-TreeSha256 $stage
        $stage_files = @(Get-ChildItem -LiteralPath $stage -File -Recurse -Force)
        $size_bytes = ($stage_files | Measure-Object -Property Length -Sum).Sum
        if ($actual_hash -ne $package.treeSha256 -or $stage_files.Count -ne $package.fileCount -or $size_bytes -ne $package.sizeBytes) {
            throw "$($package.name) verification failed. Expected $($package.treeSha256), received $actual_hash."
        }
        Move-Item -LiteralPath $stage -Destination $destination
        Write-Host "$($package.name) $($package.version): installed and verified."
    }
    catch {
        if (Test-Path -LiteralPath $stage) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
        throw
    }
}

function Get-DxcSource($package) {
    if ($source_repository) {
        $local_source = Join-Path $source_repository 'Engine\Ext\dxc'
        if (Test-Path -LiteralPath $local_source -PathType Container) {
            return $local_source
        }
    }

    $temporary_root = Join-Path ([IO.Path]::GetTempPath()) ('ailu-dxc-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary_root | Out-Null
    try {
        $archive = Join-Path $temporary_root 'dxc.zip'
        Invoke-WebRequest -Uri $package.sourceUrl -OutFile $archive
        $extract_path = Join-Path $temporary_root 'unpacked'
        Expand-Archive -LiteralPath $archive -DestinationPath $extract_path
        $compiler = Get-ChildItem -LiteralPath $extract_path -Filter 'dxc.exe' -File -Recurse | Where-Object {
            $_.FullName -match '[\\/]bin[\\/]x64[\\/]dxc\.exe$'
        } | Select-Object -First 1
        if (-not $compiler) {
            throw 'The pinned DXC archive did not contain bin/x64/dxc.exe.'
        }
        $source_path = $compiler.Directory.Parent.Parent.FullName
        $stage_source = Join-Path $temporary_root 'dxc-package'
        New-Item -ItemType Directory -Path $stage_source | Out-Null
        Get-ChildItem -LiteralPath $source_path -Force | Copy-Item -Destination $stage_source -Recurse -Force
        return $stage_source
    }
    catch {
        Remove-Item -LiteralPath $temporary_root -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }
}

$dxc = $manifest.packages | Where-Object name -eq 'dxc' | Select-Object -First 1
$dxc_source = Get-DxcSource $dxc
Install-PackageTree $dxc $dxc_source
if ($dxc_source -match '[\\/]ailu-dxc-[^\\/]+[\\/]dxc-package$') {
    Remove-Item -LiteralPath (Split-Path -Parent $dxc_source) -Recurse -Force
}

$fbx = $manifest.packages | Where-Object name -eq 'fbxsdk' | Select-Object -First 1
$fbx_source = $null
if ($fbx_sdk_root) {
    $fbx_source = (Resolve-Path -LiteralPath $fbx_sdk_root).Path
}
elseif ($source_repository) {
    $candidate = Join-Path $source_repository 'Engine\Ext\fbxsdk'
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        $fbx_source = $candidate
    }
}
if (-not $fbx_source) {
    throw "FBX SDK $($fbx.version) is not installed. Install it from $($fbx.sourceUrl), then rerun with -FbxSdkRoot <SDK folder>. The SDK is license-controlled and is not downloaded or redistributed by this script."
}
$version_header = Join-Path $fbx_source 'include\fbxsdk\fbxsdk_version.h'
if (-not (Test-Path -LiteralPath $version_header -PathType Leaf)) {
    throw "FBX SDK root must contain include\fbxsdk\fbxsdk_version.h: $fbx_source"
}
$version_text = Get-Content -LiteralPath $version_header -Raw
if ($version_text -notmatch 'FBXSDK_VERSION_MAJOR\s+2020' -or $version_text -notmatch 'FBXSDK_VERSION_MINOR\s+2' -or $version_text -notmatch 'FBXSDK_VERSION_POINT\s+1') {
    throw "Expected FBX SDK $($fbx.version), but the version header differs: $version_header"
}
Install-PackageTree $fbx $fbx_source
