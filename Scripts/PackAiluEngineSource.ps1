$ErrorActionPreference = 'Stop'

$root_path = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$archive_path = Join-Path $root_path 'AiluEngine_Source.zip'
$temporary_archive_path = "$archive_path.tmp"

$source_paths = @(
    'Engine\Src'
    'Engine\Inc'
    'Editor\Src'
    'Editor\Inc'
    'Engine\Res\Shaders'
)

foreach ($relative_source_path in $source_paths) {
    $source_path = Join-Path $root_path $relative_source_path
    if (-not (Test-Path -LiteralPath $source_path -PathType Container)) {
        throw "Source directory does not exist: $source_path"
    }
}

if (Test-Path -LiteralPath $temporary_archive_path) {
    Remove-Item -LiteralPath $temporary_archive_path -Force
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$zip_file = $null
$zip_archive = $null
$entry_names = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$file_count = 0

try {
    $zip_file = [System.IO.File]::Open(
        $temporary_archive_path,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None)
    $zip_archive = New-Object System.IO.Compression.ZipArchive(
        $zip_file,
        [System.IO.Compression.ZipArchiveMode]::Create,
        $false)

    foreach ($relative_source_path in $source_paths) {
        $source_path = Join-Path $root_path $relative_source_path
        $directories = @((Get-Item -LiteralPath $source_path))
        $directories += Get-ChildItem -LiteralPath $source_path -Directory -Recurse -Force

        foreach ($directory in $directories) {
            $relative_path = $directory.FullName.Substring($root_path.Length).TrimStart('\')
            $entry_name = ('AiluEngine/' + ($relative_path -replace '\\', '/') + '/')
            if ($entry_names.Add($entry_name)) {
                [void]$zip_archive.CreateEntry($entry_name)
            }
        }

        foreach ($file in Get-ChildItem -LiteralPath $source_path -File -Recurse -Force) {
            $relative_path = $file.FullName.Substring($root_path.Length).TrimStart('\')
            $entry_name = 'AiluEngine/' + ($relative_path -replace '\\', '/')
            if ($entry_names.Add($entry_name)) {
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $zip_archive,
                    $file.FullName,
                    $entry_name,
                    [System.IO.Compression.CompressionLevel]::Optimal)
                $file_count++
            }
        }
    }
}
finally {
    if ($null -ne $zip_archive) {
        $zip_archive.Dispose()
    }
    elseif ($null -ne $zip_file) {
        $zip_file.Dispose()
    }
}

Move-Item -LiteralPath $temporary_archive_path -Destination $archive_path -Force
Write-Host "Created: $archive_path"
Write-Host "Files: $file_count"
