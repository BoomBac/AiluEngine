param(
    [Parameter(Mandatory)][string]$EditorDirectory,
    [Parameter(Mandatory)][string]$ProjectFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('--transparent', '--scene-controls', '--rg', '--cloud', '--rtgi', '--m8',
        '--m8-longrun', '--m8-performance', '--path-light-lookup', '--rtgi-reference-sky', '--rtgi-bindless')]
    [string]$Mode = '--transparent',
    [ValidateRange(1, 1800)][int]$TimeoutSeconds = 240
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$editor_directory = (Resolve-Path -LiteralPath $EditorDirectory).Path
$output_directory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output_directory.StartsWith($editor_directory + [IO.Path]::DirectorySeparatorChar)) {
    throw 'Capture output must be beside the selected Editor.exe.'
}
if (Test-Path -LiteralPath $output_directory) { throw 'Use a fresh capture directory.' }
New-Item -ItemType Directory -Path "$output_directory/backups" | Out-Null
$project_directory = Split-Path $ProjectFile -Parent
$sources = @(
    "$repo/Editor/EditorConfig.json", "$repo/Editor/EngineConfig.json", "$repo/Editor/Res/UI/main_widget.json",
    "$repo/FileAppender_log.txt", "$repo/imgui.ini", "$project_directory/.ailu/editor_session.json",
    "$repo/render_graph.dot", "$repo/render_graph_timeline.dot"
)
$backups = @()
foreach ($source in $sources) {
    $existed = Test-Path -LiteralPath $source
    $backup = "$output_directory/backups/$($backups.Count).bin"
    $hash = $null
    if ($existed) {
        Copy-Item -LiteralPath $source -Destination $backup
        $hash = (Get-FileHash -LiteralPath $source).Hash
    }
    $backups += [pscustomobject]@{source=$source; backup=$backup; existed=$existed; hash=$hash}
}
$backups | ConvertTo-Json | Set-Content "$output_directory/backups.json"
$config = "$output_directory/EngineConfig.json"
$engine_config = Get-Content -LiteralPath "$repo/Editor/EngineConfig.json" -Raw | ConvertFrom-Json
$engine_config.isMultiThreadRender = $false
$engine_config.enable_graphics_job = $false
$engine_config | ConvertTo-Json | Set-Content -LiteralPath $config
Copy-Item -LiteralPath "$repo/Test/TaaTransparentAcceptance.h" -Destination $output_directory
if ($Mode -eq '--cloud') {
    Copy-Item -LiteralPath "$repo/Test/TaaCloudAcceptance.h" -Destination $output_directory
}
if ($Mode -in @('--rtgi', '--rtgi-reference-sky', '--rtgi-bindless')) {
    Copy-Item -LiteralPath "$repo/Test/TaaRtgiAcceptance.h" -Destination $output_directory
    Copy-Item -LiteralPath "$repo/Test/TaaRtgiReconstruction.hlsl" -Destination $output_directory
}
if ($Mode -eq '--path-light-lookup') {
    Copy-Item -LiteralPath "$repo/Test/PathLightLookupSmoke.h" -Destination $output_directory
}
Copy-Item -LiteralPath "$repo/Test/TaaGpuAcceptance.cpp" -Destination $output_directory
if ($Mode -in @('--m8', '--m8-longrun', '--m8-performance')) {
    Copy-Item -LiteralPath "$repo/Test/TaaM8Acceptance.h" -Destination $output_directory
}
$test_binary = [IO.Path]::GetFullPath("$editor_directory/../../../../../out/test/bin/x64/debug/TaaGpuAcceptance.exe")
Copy-Item -LiteralPath $test_binary -Destination "$editor_directory/TaaGpuAcceptance.exe"
$process = $null
$exit_code = -1
$watch = [Diagnostics.Stopwatch]::StartNew()
try {
    $arguments = @('"' + $ProjectFile + '"', '"' + "$output_directory/run1" + '"', '"' + $config + '"', $Mode)
    $process = Start-Process -FilePath "$editor_directory/TaaGpuAcceptance.exe" -ArgumentList $arguments `
        -WorkingDirectory $repo -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$output_directory/run.log" -RedirectStandardError "$output_directory/stderr.log"
    $process.Handle | Out-Null # Retain the native process handle before it exits so ExitCode remains available.
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id
        throw "Acceptance fixture exceeded the bounded $TimeoutSeconds second runtime."
    }
    $process.Refresh()
    $exit_code = $process.ExitCode
    [pscustomobject]@{exit_code=$exit_code; elapsed_seconds=$watch.Elapsed.TotalSeconds;
        executable="$editor_directory/TaaGpuAcceptance.exe";
        project=$ProjectFile; mode=$Mode} | ConvertTo-Json | Set-Content "$output_directory/run_summary.json"
    Get-Content "$output_directory/run.log" -Tail 12
    Get-Content "$output_directory/stderr.log" -Tail 12
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id }
    if (Test-Path -LiteralPath "$repo/FileAppender_log.txt") {
        Copy-Item -LiteralPath "$repo/FileAppender_log.txt" -Destination "$output_directory/engine.log"
    }
    foreach ($graph_file in @('render_graph.dot', 'render_graph_timeline.dot')) {
        if (Test-Path -LiteralPath "$repo/$graph_file") {
            Copy-Item -LiteralPath "$repo/$graph_file" -Destination $output_directory
        }
    }
    $restoration = @()
    foreach ($entry in $backups) {
        if ($entry.existed) {
            for ($attempt = 0; $attempt -lt 6; ++$attempt) {
                try {
                    Copy-Item -LiteralPath $entry.backup -Destination $entry.source -ErrorAction Stop
                    break
                } catch {
                    if ($attempt -eq 5) { throw }
                    Start-Sleep -Milliseconds 200
                }
            }
        } elseif (Test-Path -LiteralPath $entry.source) {
            Remove-Item -LiteralPath $entry.source
        }
        $restoration += [pscustomobject]@{source=$entry.source; restored=$(if ($entry.existed) {
            (Get-FileHash -LiteralPath $entry.source).Hash -eq $entry.hash
        } else { -not (Test-Path -LiteralPath $entry.source) })}
    }
    $restoration | ConvertTo-Json | Set-Content "$output_directory/restoration.json"
    [pscustomobject]@{exit_code=$exit_code; elapsed_seconds=$watch.Elapsed.TotalSeconds;
        executable="$editor_directory/TaaGpuAcceptance.exe";
        project=$ProjectFile; mode=$Mode} | ConvertTo-Json | Set-Content "$output_directory/run_summary.json"
}
if ($exit_code -ne 0) { throw "Acceptance fixture exit code $exit_code; inspect captured logs." }
