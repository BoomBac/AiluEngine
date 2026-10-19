param(
    [string]$EditorPath,
    [string]$ProjectPath,
    [string]$OutputRoot,
    [int]$SequentialRuns = 20
)

$ErrorActionPreference = 'Stop'
$editor_full_path = [System.IO.Path]::GetFullPath($EditorPath)
$project_full_path = [System.IO.Path]::GetFullPath($ProjectPath)
$project_root = [System.IO.Path]::GetDirectoryName($project_full_path)
$output_full_path = [System.IO.Path]::GetFullPath($OutputRoot)
$report_root = Join-Path $output_full_path 'reports'
New-Item -ItemType Directory -Force -Path $output_full_path, $report_root | Out-Null

$audit_roots = @(
    $project_root,
    'C:\Users\liusongchen_e\OneDrive\AiluEngine\Editor\EngineConfig.json',
    'C:\Users\liusongchen_e\OneDrive\AiluEngine\Editor\EditorConfig.json',
    'C:\Users\liusongchen_e\OneDrive\AiluEngine\Editor\Res\UI\main_widget.json',
    'C:\Users\liusongchen_e\OneDrive\AiluEngine\Editor\dock_layout.json',
    'C:\Users\liusongchen_e\OneDrive\AiluEngine\ObjectDetailLayout.json'
)

function Get-HashManifest {
    param([string[]]$Roots)

    $files = [System.Collections.Generic.List[object]]::new()
    foreach ($root in $Roots) {
        if (Test-Path -LiteralPath $root -PathType Leaf) {
            $items = @(Get-Item -LiteralPath $root -Force)
        }
        elseif (Test-Path -LiteralPath $root -PathType Container) {
            $items = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force)
        }
        else {
            continue
        }
        foreach ($item in $items) {
            $hash = Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256
            $files.Add([pscustomobject]@{
                    path = $item.FullName
                    length = $item.Length
                    sha256 = $hash.Hash
                })
        }
    }
    return @($files | Sort-Object path)
}

function Invoke-Batch {
    param(
        [string]$ReportPath,
        [string]$Filter = 'Cpu.*'
    )
    $arguments = @(
        $project_full_path, '--batch', '--graphics', 'none', '--run-tests', $Filter,
        '--report', $ReportPath, '--timeout-seconds', '60'
    )
    $process = Start-Process -FilePath $editor_full_path -ArgumentList $arguments -Wait -PassThru
    return $process.ExitCode
}

$before = Get-HashManifest $audit_roots
$before | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output_full_path 'before.json') -Encoding UTF8
$sequential_results = [System.Collections.Generic.List[object]]::new()
for ($index = 1; $index -le $SequentialRuns; $index++) {
    $report_path = Join-Path $report_root ('sequential_{0:D2}\result.json' -f $index)
    $exit_code = Invoke-Batch $report_path
    $report = Get-Content -Raw -LiteralPath $report_path | ConvertFrom-Json
    $sequential_results.Add([pscustomobject]@{
            index = $index
            exit_code = $exit_code
            report_status = $report.status
            passed = $report.summary.passed
            failed = $report.summary.failed
        })
}

$parallel_arguments = @($project_full_path, '--batch', '--graphics', 'none', '--run-tests', 'Cpu.*',
    '--report', (Join-Path $report_root 'parallel_a\result.json'), '--timeout-seconds', '60')
$parallel_arguments_b = @($project_full_path, '--batch', '--graphics', 'none', '--run-tests', 'Cpu.*',
    '--report', (Join-Path $report_root 'parallel_b\result.json'), '--timeout-seconds', '60')
$parallel_a = Start-Process -FilePath $editor_full_path -ArgumentList $parallel_arguments -PassThru
$parallel_b = Start-Process -FilePath $editor_full_path -ArgumentList $parallel_arguments_b -PassThru
$parallel_a.WaitForExit()
$parallel_b.WaitForExit()
$parallel_reports = @(
    (Get-Content -Raw -LiteralPath (Join-Path $report_root 'parallel_a\result.json') | ConvertFrom-Json)
    (Get-Content -Raw -LiteralPath (Join-Path $report_root 'parallel_b\result.json') | ConvertFrom-Json)
)
$after = Get-HashManifest $audit_roots
$after | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output_full_path 'after.json') -Encoding UTF8

$before_map = @{}
foreach ($item in $before) { $before_map[$item.path] = $item.sha256 }
$after_map = @{}
foreach ($item in $after) { $after_map[$item.path] = $item.sha256 }
$changed = [System.Collections.Generic.List[object]]::new()
foreach ($path in @($before_map.Keys + $after_map.Keys | Sort-Object -Unique)) {
    $old_hash = if ($before_map.ContainsKey($path)) { $before_map[$path] } else { $null }
    $new_hash = if ($after_map.ContainsKey($path)) { $after_map[$path] } else { $null }
    if ($old_hash -ne $new_hash) {
        $changed.Add([pscustomobject]@{ path = $path; before = $old_hash; after = $new_hash })
    }
}

$sequential_passed = @($sequential_results | Where-Object {
        $_.exit_code -eq 0 -and $_.report_status -eq 'passed' -and $_.passed -eq 4 -and $_.failed -eq 0
    }).Count
$parallel_passed = @($parallel_reports | Where-Object {
        $_.status -eq 'passed' -and $_.summary.passed -eq 4 -and $_.summary.failed -eq 0
    }).Count
$editor_processes_remaining = @(Get-Process -Name Editor -ErrorAction SilentlyContinue).Count
$result = [pscustomobject]@{
    schema_version = 1
    project = $project_full_path
    sequential_runs = $SequentialRuns
    sequential_passed = $sequential_passed
    parallel_passed = $parallel_passed
    changed_file_count = $changed.Count
    changed_files = @($changed)
    editor_processes_remaining = $editor_processes_remaining
    status = if ($sequential_passed -eq $SequentialRuns -and $parallel_passed -eq 2 -and $changed.Count -eq 0 -and $editor_processes_remaining -eq 0) { 'passed' } else { 'failed' }
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output_full_path 'audit.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 6
if ($result.status -ne 'passed') { exit 1 }
exit 0
