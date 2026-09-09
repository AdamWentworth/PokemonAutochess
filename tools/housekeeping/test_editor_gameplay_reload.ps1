[CmdletBinding()]
param(
    [string]$OutputDirectory = 'debug/editor-gameplay-reload',
    [string]$EditorPath = 'D:/Projects/Phlosion/PhlosionEngine/build/Release/PhlosionEditor.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = [IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskSource = Join-Path $taskRoot 'src/game/editor/PokemonAutochessEditorPreviewCatalog.cpp'
$taskDescriptor = Join-Path $taskRoot '.phlosion.reload-test.project.json'
$taskStdout = Join-Path $taskOutput 'stdout.log'
$taskStderr = Join-Path $taskOutput 'stderr.log'
$taskOriginal = [IO.File]::ReadAllBytes($taskSource)
$taskOriginalText = [Text.Encoding]::UTF8.GetString($taskOriginal)
$taskExpected = $taskOriginalText
$taskProcess = $null
$taskOriginalDepot = $env:PHLOSION_ASSET_DEPOT

# This test makes temporary edits to compiled source. Run it without another
# editor watching the same checkout, and restore exact original bytes on exit.
if (Get-Process PhlosionEditor -ErrorAction SilentlyContinue) {
    throw 'Close existing Phlosion Editor windows before this isolated reload test.'
}
if (Test-Path -LiteralPath $taskDescriptor) { throw "Test descriptor already exists: $taskDescriptor" }
[IO.File]::WriteAllBytes((Join-Path $taskOutput 'preview-catalog.original.cpp'), $taskOriginal)

function Set-ProbeSource([string]$Text) {
    if ([IO.File]::ReadAllText($taskSource) -cne $script:taskExpected) {
        throw 'The preview catalog changed outside this test; refusing to overwrite it.'
    }
    [IO.File]::WriteAllText($taskSource, $Text, [Text.UTF8Encoding]::new($false))
    $script:taskExpected = $Text
}
function Read-EditorLog {
    if (-not (Test-Path -LiteralPath $taskStderr)) { return '' }
    $stream = [IO.File]::Open($taskStderr, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
function Wait-EditorLog([string]$Pattern, [int]$Count = 1) {
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    do {
        $log = Read-EditorLog
        if ([regex]::Matches($log, $Pattern).Count -ge $Count) { return }
        if ($taskProcess.HasExited) { throw "Editor exited ($($taskProcess.ExitCode)) before '$Pattern'. $log" }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Timed out waiting for '$Pattern'. $log" }
        Start-Sleep -Milliseconds 500
    } while ($true)
}

try {
    if ([string]::IsNullOrEmpty($env:PHLOSION_ASSET_DEPOT)) {
        $env:PHLOSION_ASSET_DEPOT = 'D:/ProjectData/Games/PokemonAutochess/Assets'
    }
    $descriptor = Get-Content (Join-Path $taskRoot 'phlosion.project.json') -Raw | ConvertFrom-Json
    $descriptor.startup_scene.scene_id = 'routes/route1-pilot'
    $descriptor | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $taskDescriptor -Encoding UTF8
    $arguments = @("--project=$taskDescriptor", '--renderer=opengl', '--game-preview=route1-pilot-battle',
        '--hidden', '--auto-reload', '--exit-after-gameplay-reloads=2',
        "--state-directory=$taskOutput/state", "--metrics-output=$taskOutput/metrics.json")
    $taskProcess = Start-Process -FilePath $EditorPath -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
        -ArgumentList ($arguments | ForEach-Object { '"' + $_ + '"' }) `
        -RedirectStandardOutput $taskStdout -RedirectStandardError $taskStderr
    Write-Output "Reload test editor PID: $($taskProcess.Id)"
    Wait-EditorLog '\[PhlosionEditor\]\[GamePreviewWarmup\] total='

    $markerText = $taskOriginalText.Replace('Route 1 Arena Pilot - Battle', 'Route 1 Arena Pilot - Battle [reload probe]')
    if ($markerText -ceq $taskOriginalText) { throw 'The expected battle preview label was not found.' }
    Set-ProbeSource $markerText
    Write-Output 'Saved a compiled preview-label change; waiting for automatic reload.'
    Wait-EditorLog 'restored scene=routes/route1-pilot preview=route1-pilot-battle status=.*\[reload probe\].*camera_preserved=1'

    Set-ProbeSource ($markerText + "`n#error PHLOSION_RELOAD_INTENTIONAL_COMPILE_FAILURE`n")
    Write-Output 'Saved an intentional compile error; checking that the current preview survives.'
    Wait-EditorLog 'build exit=[1-9][0-9]* Gameplay build failed'
    Copy-Item -LiteralPath (Join-Path $taskRoot ".phlosion/gameplay-build-$($taskProcess.Id).log") `
        -Destination (Join-Path $taskOutput 'expected-compile-failure.log')
    if ($taskProcess.HasExited) { throw 'Compile failure closed the editor.' }

    Set-ProbeSource $taskOriginalText
    Write-Output 'Restored the original source; waiting for the second successful reload.'
    Wait-EditorLog 'restored scene=routes/route1-pilot preview=route1-pilot-battle' 2
    if (-not $taskProcess.WaitForExit(30000)) { throw 'Editor did not complete the reload test.' }
    $taskProcess.WaitForExit()
    if ($taskProcess.ExitCode -ne 0) { throw "Editor failed with exit code $($taskProcess.ExitCode)." }
    $log = [IO.File]::ReadAllText($taskStderr)
    $reloads = [regex]::Matches($log, '\[Gameplay Reload\] Gameplay reloaded\.').Count
    $failures = [regex]::Matches($log, 'build exit=[1-9][0-9]* Gameplay build failed').Count
    if ($reloads -ne 2 -or $failures -ne 1) { throw "Unexpected build/reload counts: $reloads reloads, $failures failures." }
    $metrics = Get-Content (Join-Path $taskOutput 'metrics.json') -Raw | ConvertFrom-Json
    if ($metrics.project.active_scene.id -ne 'routes/route1-pilot') { throw 'Reload changed the selected map.' }
    [ordered]@{
        passed = $true
        editor_pid = $taskProcess.Id
        successful_reloads = $reloads
        expected_compile_failures = $failures
        compiled_label_observed = $true
        restored_scene = $metrics.project.active_scene.id
        camera_preservation_checks = [regex]::Matches($log, 'camera_preserved=1').Count
        source_sha256 = (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'verification.json')
    Write-Output "Passed. Evidence: $taskOutput"
} finally {
    if ($taskProcess -and -not $taskProcess.HasExited) {
        # Terminating this test's editor closes its job object and build children.
        Stop-Process -Id $taskProcess.Id
        $taskProcess.WaitForExit()
    }
    if ([IO.File]::ReadAllText($taskSource) -ceq $taskExpected) {
        [IO.File]::WriteAllBytes($taskSource, $taskOriginal)
    } else {
        Write-Warning "Source changed outside the test. Original bytes preserved in $taskOutput/preview-catalog.original.cpp."
    }
    if (Test-Path -LiteralPath $taskDescriptor) { Remove-Item -LiteralPath $taskDescriptor }
    $env:PHLOSION_ASSET_DEPOT = $taskOriginalDepot
}
