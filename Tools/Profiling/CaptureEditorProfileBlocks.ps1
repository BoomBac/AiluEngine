param(
    [Parameter(Mandatory = $true)]
    [string]$EditorExe,
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [int]$WarmupFrames = 60,
    [int]$SampleFrames = 300,
    [int]$CaptureTimeoutSeconds = 1800,
    [switch]$EnableToolbarTextureCache,
    [switch]$EnableWidgetTextureCache
)

$ErrorActionPreference = 'Stop'
$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$editor_path = (Resolve-Path $EditorExe).Path
$project_path = (Resolve-Path $ProjectFile).Path
$fbx_dll_dir = Join-Path $repo_root 'Engine\Ext\Packages\fbxsdk\lib\vs2019\x64\debug'
$pix_dll_dir = Join-Path $repo_root 'Engine\Ext\pix'
$ui_cache_env = 'AILU_EDITOR_UI_TEXTURE_CACHE'
$old_ui_cache_env = [Environment]::GetEnvironmentVariable($ui_cache_env)
$output_path = [IO.Path]::GetFullPath($OutputDirectory)
$csv_path = Join-Path $output_path 'profile-block-events.csv'
$summary_csv_path = Join-Path $output_path 'profile-block-summary.csv'
$ui_stats_path = Join-Path $output_path 'profile-block-events.ui-stats.csv'
$ui_stats_summary_path = Join-Path $output_path 'ui-render-stats-summary.csv'
$ui_widget_stats_path = Join-Path $output_path 'profile-block-events.ui-widgets.csv'
$ui_widget_summary_path = Join-Path $output_path 'ui-widget-draw-summary.csv'

foreach ($required_path in @($fbx_dll_dir, $pix_dll_dir)) {
    if (-not (Test-Path -LiteralPath $required_path)) { throw "Required Editor dependency was not found: $required_path" }
}
if ($WarmupFrames -lt 0 -or $WarmupFrames -gt 100000 -or $SampleFrames -lt 1 -or $SampleFrames -gt 300) {
    throw 'WarmupFrames must be 0..100000 and SampleFrames must be 1..300.'
}
if ($CaptureTimeoutSeconds -lt 1) { throw 'CaptureTimeoutSeconds must be positive.' }

New-Item -ItemType Directory -Path $output_path -Force | Out-Null
$old_path = $env:PATH
$editor_process = $null

try {
    $env:PATH = "$fbx_dll_dir;$pix_dll_dir;$old_path"
    if ($EnableToolbarTextureCache) { $env:AILU_EDITOR_UI_TEXTURE_CACHE = '1' }
    if ($EnableWidgetTextureCache) { $env:AILU_EDITOR_UI_TEXTURE_CACHE = '2' }
    $editor_arguments = '"{0}" --profile-capture {1} {2} "{3}"' -f `
        $project_path, $WarmupFrames, $SampleFrames, $csv_path
    $editor_process = Start-Process -FilePath $editor_path -ArgumentList $editor_arguments `
        -WorkingDirectory $repo_root -WindowStyle Hidden -PassThru
    $capture_deadline = [DateTime]::UtcNow.AddSeconds($CaptureTimeoutSeconds)

    while ([DateTime]::UtcNow -lt $capture_deadline) {
        Start-Sleep -Seconds 2
        $editor_process.Refresh()
        if ($editor_process.HasExited) { break }
    }
    $editor_process.Refresh()
    if (-not $editor_process.HasExited) {
        Stop-Process -Id $editor_process.Id -Force -ErrorAction SilentlyContinue
        throw "Editor profile capture exceeded $CaptureTimeoutSeconds seconds."
    }
    if ($editor_process.ExitCode -ne 0) { throw "Editor profile capture failed with exit code $($editor_process.ExitCode)." }
    if (-not (Test-Path -LiteralPath $csv_path)) { throw 'Editor exited without writing the Profile Block CSV.' }
    if (-not (Test-Path -LiteralPath $ui_stats_path)) { throw 'Editor exited without writing the UI render stats CSV.' }
    if (-not (Test-Path -LiteralPath $ui_widget_stats_path)) { throw 'Editor exited without writing the UI widget draw stats CSV.' }

    $events = Import-Csv -LiteralPath $csv_path
    $captured_frames = @($events | Select-Object -ExpandProperty frame_index -Unique).Count
    if ($captured_frames -lt $SampleFrames) {
        throw "CSV contains $captured_frames frames; expected at least $SampleFrames."
    }
    if ($events.Count -eq 0) { throw 'CSV contains no engine Profile Block events.' }

    $ui_stats = Import-Csv -LiteralPath $ui_stats_path
    if ($ui_stats.Count -ne $SampleFrames) {
        throw "UI render stats CSV contains $($ui_stats.Count) samples; expected $SampleFrames."
    }
    $ui_summary = foreach ($metric in @('ui_widget_update_executed_count', 'ui_widget_update_skipped_count', 'ui_texture_cache_hit_count', 'ui_texture_cache_miss_count', 'ui_texture_cache_dirty_count', 'ui_texture_cache_composite_draw_count', 'ui_texture_cache_resident_bytes', 'ui_texture_cache_repaint_time_ms', 'ui_cache_hit_count', 'ui_cache_miss_count', 'ui_draw_call_count', 'ui_uploaded_bytes')) {
        $values = @($ui_stats | ForEach-Object {
            [double]::Parse($_.$metric, [Globalization.CultureInfo]::InvariantCulture)
        } | Sort-Object)
        $p50_index = [Math]::Max(0, [Math]::Ceiling($values.Count * 0.50) - 1)
        $p95_index = [Math]::Max(0, [Math]::Ceiling($values.Count * 0.95) - 1)
        [PSCustomObject]@{
            metric = $metric
            frame_samples = $values.Count
            mean = ($values | Measure-Object -Average).Average
            p50 = $values[$p50_index]
            p95 = $values[$p95_index]
            max = $values[-1]
        }
    }
    $ui_summary | Export-Csv -LiteralPath $ui_stats_summary_path -NoTypeInformation -Encoding utf8

    $ui_widget_stats = Import-Csv -LiteralPath $ui_widget_stats_path
    if ($ui_widget_stats.Count -eq 0) { throw 'UI widget draw stats CSV contains no visible widgets.' }
    $widget_summary = foreach ($widget_group in ($ui_widget_stats | Group-Object widget_name)) {
        $values = @($widget_group.Group | ForEach-Object {
            [double]::Parse($_.draw_node_count, [Globalization.CultureInfo]::InvariantCulture)
        } | Sort-Object)
        $p95_index = [Math]::Max(0, [Math]::Ceiling($values.Count * 0.95) - 1)
        [PSCustomObject]@{
            widget_name = $widget_group.Name
            sample_count = $values.Count
            mean_draw_nodes = ($values | Measure-Object -Average).Average
            p95_draw_nodes = $values[$p95_index]
            max_draw_nodes = $values[-1]
        }
    }
    $widget_summary | Export-Csv -LiteralPath $ui_widget_summary_path -NoTypeInformation -Encoding utf8

    $summary = foreach ($block_group in ($events | Group-Object block_name)) {
        $frame_values = @($block_group.Group | Group-Object frame_index | ForEach-Object {
            $frame_total = 0.0
            foreach ($event in $_.Group) {
                $frame_total += [double]::Parse($event.duration_ms, [Globalization.CultureInfo]::InvariantCulture)
            }
            $frame_total
        } | Sort-Object)
        $p50_index = [Math]::Max(0, [Math]::Ceiling($frame_values.Count * 0.50) - 1)
        $p95_index = [Math]::Max(0, [Math]::Ceiling($frame_values.Count * 0.95) - 1)
        [PSCustomObject]@{
            block_name = $block_group.Name
            frame_samples = $frame_values.Count
            mean_ms = ($frame_values | Measure-Object -Average).Average
            p50_ms = $frame_values[$p50_index]
            p95_ms = $frame_values[$p95_index]
            max_ms = $frame_values[-1]
        }
    }
    $summary | Export-Csv -LiteralPath $summary_csv_path -NoTypeInformation -Encoding utf8

    [PSCustomObject]@{
        editor_pid = $editor_process.Id
        warmup_frames = $WarmupFrames
        captured_frames = $captured_frames
        profile_block_events = $events.Count
        ui_stats_frames = $ui_stats.Count
        csv_file = $csv_path
        summary_csv = $summary_csv_path
        ui_stats_csv = $ui_stats_path
        ui_stats_summary_csv = $ui_stats_summary_path
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output_path 'capture-report.json') -Encoding utf8
}
finally {
    $env:PATH = $old_path
    $env:AILU_EDITOR_UI_TEXTURE_CACHE = $old_ui_cache_env
    if ($editor_process -and -not $editor_process.HasExited) {
        Stop-Process -Id $editor_process.Id -Force -ErrorAction SilentlyContinue
    }
}
