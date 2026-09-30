param(
    [Parameter(Mandatory = $true)][string]$EditorExecutable,
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [Parameter(Mandatory = $true)][string]$SmokeLibrary,
    [Parameter(Mandatory = $true)][string]$EvidenceDirectory,
    [switch]$TestManifest,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$repository_root = Split-Path $PSScriptRoot -Parent
$editor_path = (Resolve-Path -LiteralPath $EditorExecutable).Path
$project_path = (Resolve-Path -LiteralPath $ProjectFile).Path
$smoke_path = (Resolve-Path -LiteralPath $SmokeLibrary).Path
$evidence_path = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidence_path -Force | Out-Null
$state_paths = @(
    (Join-Path $repository_root 'Editor/EngineConfig.json'),
    (Join-Path $repository_root 'Editor/EditorConfig.json'),
    (Join-Path $repository_root 'Editor/dock_layout.json'),
    (Join-Path $repository_root 'Editor/Res/UI/main_widget.json'),
    (Join-Path (Split-Path $project_path -Parent) '.ailu/editor_session.json')
)
$saved_state = @{}
$backup_path = Join-Path $evidence_path 'state-backup'
New-Item -ItemType Directory -Path $backup_path -Force | Out-Null
$plugin_root = [IO.Path]::GetFullPath((Join-Path (Split-Path $project_path -Parent) 'Plugins'))
$plugin_root_existed = Test-Path -LiteralPath $plugin_root
$plugin_fixture = [IO.Path]::GetFullPath((Join-Path $plugin_root ('LifecycleAcceptance_' + $PID)))
$fixture_created = $false
foreach ($state_path in $state_paths) {
    $saved_state[$state_path] = if (Test-Path -LiteralPath $state_path) {
        [IO.File]::ReadAllBytes($state_path)
    } else { $null }
    if ($null -ne $saved_state[$state_path]) {
        [IO.File]::WriteAllBytes((Join-Path $backup_path ($saved_state.Count.ToString() + '.bin')),
            $saved_state[$state_path])
    }
}
($state_paths | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $backup_path 'paths.json') -Encoding utf8
try {
    $case_names = @('empty', 'threaded')
    if ($TestManifest) { $case_names += @('discovery', 'autoload') }
    foreach ($case_name in $case_names) {
        $case_path = Join-Path $evidence_path $case_name
        New-Item -ItemType Directory -Path $case_path -Force | Out-Null
        $arguments = @(('"' + $project_path + '"'), '--exit-after-frames', '10')
        if ($case_name -eq 'threaded') {
            $arguments += @('--plugin-smoke', ('"' + $smoke_path + '"'))
        }
        if ($case_name -eq 'discovery' -or $case_name -eq 'autoload') {
            if (-not $fixture_created) {
                if (Test-Path -LiteralPath $plugin_fixture) { throw "Fixture directory already exists: $plugin_fixture" }
                New-Item -ItemType Directory -Path $plugin_fixture | Out-Null
                $fixture_created = $true
                Copy-Item -LiteralPath $smoke_path -Destination (Join-Path $plugin_fixture 'AcceptanceSmoke.dll')
            }
            $manifest = @{
                id = 'com.ailu.test.smoke'; name = 'Ailu Smoke Plugin'; version = '0.1.0'; api_version = 1
                type = 'editor'; library = 'AcceptanceSmoke.dll'; autoload = ($case_name -eq 'autoload')
            } | ConvertTo-Json
            [IO.File]::WriteAllText((Join-Path $plugin_fixture 'plugin.json'), $manifest)
            if ($case_name -eq 'autoload') {
                $arguments += @('--plugin-smoke-autoload', ('"' + (Join-Path $plugin_fixture 'AcceptanceSmoke.dll') + '"'))
            }
        }
        $editor_process = Start-Process -FilePath $editor_path -ArgumentList $arguments -WorkingDirectory $case_path `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $case_path 'stdout.txt') `
            -RedirectStandardError (Join-Path $case_path 'stderr.txt')
        if (-not $editor_process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $editor_process.Id -Force
            throw "$case_name Editor timed out; normal shutdown was not validated."
        }
        if ($editor_process.ExitCode -ne 0) {
            throw "$case_name Editor failed with exit code $($editor_process.ExitCode). See $case_path."
        }
        $log_path = Join-Path $case_path 'FileAppender_log.txt'
        $log_text = [IO.File]::ReadAllText($log_path)
        foreach ($marker in @('Editor host initialized', 'Editor tick unsubscribed and host shut down',
            'Editor lifecycle validation PASS')) {
            if (-not $log_text.Contains($marker)) { throw "$case_name missing marker: $marker" }
        }
        if ($log_text -match '\[ERROR\].*\[Plugin\]') { throw "$case_name plugin error in log" }
        if ($case_name -eq 'discovery') {
            if (-not $log_text.Contains('Discovered 1 plugin manifests') -or $log_text.Contains('SmokePlugin.OnLoad')) {
                throw 'Discovery-only manifest loaded a plugin or was not discovered.'
            }
        }
        if ($case_name -eq 'threaded' -or $case_name -eq 'autoload') {
            foreach ($marker in @('SmokePlugin.WorkerJoined', 'SmokePlugin.Destroy (worker joined)',
                'Unloaded com.ailu.test.smoke', 'Editor smoke activity:', 'Host registrations released:')) {
                if (-not $log_text.Contains($marker)) { throw "$case_name missing marker: $marker" }
            }
            if ($log_text.IndexOf('Host registrations released:') -gt $log_text.IndexOf('SmokePlugin.WorkerJoined')) {
                throw "$case_name registrations survived until plugin teardown."
            }
        }
        Write-Output "$case_name Editor lifecycle: PASS (exit 0, normal close, plugin host teardown verified)"
    }
} finally {
    $restore_errors = @()
    foreach ($state_path in $state_paths) {
        try {
            if ($null -ne $saved_state[$state_path]) {
                # Replace the file instead of truncating a file still mapped by a sync/indexing process.
                $restore_path = $state_path + '.plugin-restore-' + [Guid]::NewGuid().ToString('N')
                [IO.File]::WriteAllBytes($restore_path, $saved_state[$state_path])
                try { [IO.File]::Move($restore_path, $state_path, $true) }
                finally { if (Test-Path -LiteralPath $restore_path) { Remove-Item -LiteralPath $restore_path } }
            } elseif (Test-Path -LiteralPath $state_path) {
                Remove-Item -LiteralPath $state_path
            }
        } catch {
            $restore_errors += "${state_path}: $_"
        }
    }
    if ($fixture_created) {
        if (-not $plugin_fixture.StartsWith($plugin_root + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Plugin fixture cleanup path escaped its parent.' }
        Remove-Item -LiteralPath $plugin_fixture -Recurse -Force
    }
    if (-not $plugin_root_existed -and (Test-Path -LiteralPath $plugin_root) `
        -and @(Get-ChildItem -LiteralPath $plugin_root -Force).Count -eq 0) {
        Remove-Item -LiteralPath $plugin_root -Force
    }
    if ($restore_errors.Count -ne 0) { throw ($restore_errors -join "`n") }
}
