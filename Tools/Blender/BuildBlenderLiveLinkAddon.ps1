$ErrorActionPreference = "Stop"

$addon_dir = Join-Path $PSScriptRoot "ailu_live_link"
$dist_dir = Join-Path $PSScriptRoot "dist"
$zip_path = Join-Path $dist_dir "ailu_live_link.zip"

if (-not (Test-Path -LiteralPath (Join-Path $addon_dir "__init__.py"))) {
    throw "Blender add-on source not found: $addon_dir"
}

New-Item -ItemType Directory -Path $dist_dir -Force | Out-Null
if (Test-Path -LiteralPath $zip_path) {
    Remove-Item -LiteralPath $zip_path -Force
}
$archive = [IO.Compression.ZipFile]::Open($zip_path, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $addon_dir -File -Recurse) {
        $relative_path = [IO.Path]::GetRelativePath($addon_dir, $file.FullName).Replace('\', '/')
        if ($relative_path.Split('/') -contains '__pycache__' -or $file.Extension -in '.pyc', '.pyo') {
            continue
        }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, "ailu_live_link/$relative_path", [IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
    }
} finally {
    $archive.Dispose()
}
Write-Output "Created $zip_path"
