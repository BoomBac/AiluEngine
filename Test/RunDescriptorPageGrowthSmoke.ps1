param([Parameter(Mandatory)][string]$EditorDirectory, [Parameter(Mandatory)][string]$ProjectFile,
      [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$editor_directory = (Resolve-Path -LiteralPath $EditorDirectory).Path
$output_directory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output_directory.StartsWith($editor_directory + [IO.Path]::DirectorySeparatorChar)) {
    throw 'Use an evidence directory beside the selected Editor.exe.'
}
if (Test-Path -LiteralPath $output_directory) { throw 'Use a fresh evidence directory.' }
New-Item -ItemType Directory -Path "$output_directory/backups" | Out-Null
$project_directory = Split-Path $ProjectFile -Parent
$sources = @("$repo/Editor/EditorConfig.json", "$repo/Editor/EngineConfig.json", "$repo/Editor/Res/UI/main_widget.json",
    "$repo/Editor/dock_layout.json", "$repo/FileAppender_log.txt", "$repo/imgui.ini",
    "$project_directory/.ailu/editor_session.json", "$repo/render_graph.dot", "$repo/render_graph_timeline.dot")
$backups = @()
foreach ($source in $sources) {
    $existed = Test-Path -LiteralPath $source
    $backup = "$output_directory/backups/$($backups.Count).bin"
    $hash = $null
    if ($existed) { Copy-Item -LiteralPath $source -Destination $backup; $hash = (Get-FileHash $source).Hash }
    $backups += [pscustomobject]@{source=$source; backup=$backup; existed=$existed; hash=$hash}
}
$config = Get-Content -LiteralPath "$repo/Editor/EngineConfig.json" -Raw | ConvertFrom-Json
$config._enable_rdc = $false
$config._enable_d3d12_debug_layer = $true
$config | ConvertTo-Json | Set-Content -LiteralPath "$output_directory/EngineConfig.json"
$test_binary = [IO.Path]::GetFullPath("$editor_directory/../../../../test/bin/x64/debug/DescriptorPageGrowthSmoke.exe")
Copy-Item -LiteralPath $test_binary -Destination "$editor_directory/DescriptorPageGrowthSmoke.exe"
$process = $null
$exit_code = -1
try {
    $arguments = @('"' + $ProjectFile + '"', '"' + $output_directory + '"',
                   '"' + "$output_directory/EngineConfig.json" + '"')
    $process = Start-Process -FilePath "$editor_directory/DescriptorPageGrowthSmoke.exe" -ArgumentList $arguments `
        -WorkingDirectory $repo -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$output_directory/run.log" -RedirectStandardError "$output_directory/stderr.log"
    $process.Handle | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(240)
    while (-not $process.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Descriptor regression exceeded 240 seconds.' }
    }
    $process.Refresh()
    $exit_code = $process.ExitCode
    [pscustomobject]@{exit_code=$exit_code; pid=$process.Id; executable="$editor_directory/DescriptorPageGrowthSmoke.exe"} |
        ConvertTo-Json | Set-Content "$output_directory/run_summary.json"
    Get-Content "$output_directory/run.log" -Tail 8
    Get-Content "$output_directory/stderr.log" -Tail 8
} finally {
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
}
if ($exit_code -ne 0) { throw "Descriptor regression exit code $exit_code" }
