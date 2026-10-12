param(
    [Parameter(Mandatory)][string]$EditorDirectory,
    [Parameter(Mandatory)][string]$ProjectFile,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$DriverFile = 'Test/TaaCombinationEditorAcceptance.mjs',
    [ValidateRange(1, 900)][int]$StartupTimeoutSeconds = 120,
    [ValidateRange(1, 1800)][int]$DriverTimeoutSeconds = 360
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
$process = $null
$driver = $null
$exit_code = -1
try {
    $config_path = "$repo/Editor/EngineConfig.json"
    $config = Get-Content -LiteralPath $config_path -Raw | ConvertFrom-Json
    $config.isMultiThreadRender = $false
    $config.enable_graphics_job = $false
    $config | ConvertTo-Json | Set-Content -LiteralPath $config_path
    Copy-Item -LiteralPath "$repo/$DriverFile" -Destination "$output_directory/driver.mjs"
    Copy-Item -LiteralPath "$repo/Test/TaaEditorAcceptanceClient.mjs" -Destination $output_directory
    $process = Start-Process -FilePath "$editor_directory/Editor.exe" -ArgumentList ('"' + $ProjectFile + '"') `
        -WorkingDirectory $repo -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$output_directory/stdout.log" -RedirectStandardError "$output_directory/stderr.log"
    $ready = $false
    for ($attempt = 0; $attempt -lt $StartupTimeoutSeconds; ++$attempt) {
        Start-Sleep -Seconds 1
        if ($process.HasExited) { break }
        $session_path = "$project_directory/.ailu/editor_session.json"
        if (Test-Path -LiteralPath $session_path) {
            $session = Get-Content -LiteralPath $session_path -Raw | ConvertFrom-Json
            if ($session.pid -eq $process.Id) { $ready = $true; break }
        }
    }
    [pscustomobject]@{pid=$process.Id; ready=$ready; exited=$process.HasExited;
        executable="$editor_directory/Editor.exe"} | ConvertTo-Json | Set-Content "$output_directory/startup.json"
    if (-not $ready) { throw "Editor did not publish its automation session within $StartupTimeoutSeconds seconds." }
    $arguments = @('"' + "$output_directory/driver.mjs" + '"', '"' + $project_directory + '"',
        '"' + $output_directory + '"')
    $driver = Start-Process -FilePath 'node' -ArgumentList $arguments -WorkingDirectory $repo -WindowStyle Hidden `
        -PassThru -RedirectStandardOutput "$output_directory/driver.log" `
        -RedirectStandardError "$output_directory/driver_error.log"
    if (-not $driver.WaitForExit($DriverTimeoutSeconds * 1000)) {
        throw "Editor acceptance driver exceeded $DriverTimeoutSeconds seconds."
    }
    $driver.Refresh()
    $exit_code = $driver.ExitCode
    Get-Content "$output_directory/driver.log" -Tail 12
    Get-Content "$output_directory/driver_error.log" -Tail 12
} finally {
    if ($driver -and -not $driver.HasExited) { Stop-Process -Id $driver.Id; $driver.WaitForExit() }
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    if (Test-Path -LiteralPath "$repo/FileAppender_log.txt") {
        Copy-Item -LiteralPath "$repo/FileAppender_log.txt" -Destination "$output_directory/engine.log"
    }
    $restoration = @()
    foreach ($entry in $backups) {
        if ($entry.existed) { Copy-Item -LiteralPath $entry.backup -Destination $entry.source }
        elseif (Test-Path -LiteralPath $entry.source) { Remove-Item -LiteralPath $entry.source }
        $restoration += [pscustomobject]@{source=$entry.source; restored=$(if ($entry.existed) {
            (Get-FileHash -LiteralPath $entry.source).Hash -eq $entry.hash
        } else { -not (Test-Path -LiteralPath $entry.source) })}
    }
    $restoration | ConvertTo-Json | Set-Content "$output_directory/restoration.json"
    [pscustomobject]@{exit_code=$exit_code; executable="$editor_directory/Editor.exe";
        project=$ProjectFile; driver=$DriverFile} | ConvertTo-Json | Set-Content "$output_directory/run_summary.json"
}
if ($exit_code -ne 0) { throw "Editor driver exit code $exit_code; inspect captured logs." }
