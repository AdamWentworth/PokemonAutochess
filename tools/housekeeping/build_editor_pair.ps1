[CmdletBinding()]
param(
    [string]$GameRoot = "",
    [string]$EngineRoot = "",
    [string]$GameBuildDirectory = "",
    [string]$EngineBuildDirectory = "",
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [string]$OutputDirectory = "",
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        throw "CMake cache does not exist: $CachePath"
    }
    $match = Select-String `
        -LiteralPath $CachePath `
        -Pattern "^$([regex]::Escape($Name)):[^=]+=(.*)$" |
        Select-Object -First 1
    if (-not $match) {
        throw "CMake cache '$CachePath' has no '$Name' entry."
    }
    return $match.Matches[0].Groups[1].Value
}

function Get-TextSha256 {
    param([Parameter(Mandatory = $true)][string]$Text)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString(
            $algorithm.ComputeHash($bytes))).Replace(
                '-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Invoke-GitLines {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $lines = @(& git -C $RepositoryRoot @Arguments)
    if ($LASTEXITCODE -ne 0) {
        throw "git failed in '$RepositoryRoot': git $($Arguments -join ' ')"
    }
    return @($lines)
}

function Get-RepositorySourceState {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string[]]$PathSpecs
    )
    $listArguments = @(
        'ls-files', '--cached', '--others', '--exclude-standard', '--') +
        $PathSpecs
    $paths = @(
        Invoke-GitLines $RepositoryRoot $listArguments |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique
    )
    if ($paths.Count -eq 0) {
        throw "No source inputs were found in '$RepositoryRoot'."
    }

    $builder = [Text.StringBuilder]::new()
    foreach ($relativePath in $paths) {
        $absolutePath = Join-Path $RepositoryRoot $relativePath
        if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
            throw "Source input disappeared while fingerprinting: $absolutePath"
        }
        $hash = (Get-FileHash `
            -LiteralPath $absolutePath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        [void]$builder.Append($relativePath.Replace('\', '/'))
        [void]$builder.Append("`0")
        [void]$builder.Append($hash)
        [void]$builder.Append("`n")
    }

    $statusArguments = @(
        'status', '--porcelain=v1', '--untracked-files=all', '--') +
        $PathSpecs
    $dirtyPaths = @(
        Invoke-GitLines $RepositoryRoot $statusArguments |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
    $commit = (
        Invoke-GitLines $RepositoryRoot @('rev-parse', 'HEAD') |
        Select-Object -First 1).Trim()

    return [pscustomobject]@{
        root = $RepositoryRoot
        commit = $commit
        source_fingerprint = Get-TextSha256 $builder.ToString()
        input_file_count = $paths.Count
        dirty_source_paths = $dirtyPaths
    }
}

function Get-ArtifactRecord {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Expected paired-build artifact does not exist: $Path"
    }
    $file = Get-Item -LiteralPath $Path
    return [pscustomobject]@{
        path = $file.FullName
        bytes = $file.Length
        last_write_utc = $file.LastWriteTimeUtc.ToString('o')
        sha256 = (Get-FileHash `
            -LiteralPath $file.FullName `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$CaptureOutput
    )
    Write-Host "> $Executable $($Arguments -join ' ')"
    $lines = @(& $Executable @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    foreach ($line in $lines) {
        Write-Host $line
    }
    if ($exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $Executable"
    }
    if ($CaptureOutput) {
        return ($lines -join "`n")
    }
}

function Assert-StateUnchanged {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$After,
        [Parameter(Mandatory = $true)][string]$Owner
    )
    if ($Before.source_fingerprint -ne $After.source_fingerprint) {
        throw "$Owner source inputs changed during the paired build. Run it again from a stable workspace."
    }
}

function Assert-ProofMatches {
    param(
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Expected.path -ne $Actual.path) {
        throw "$Label path changed since the proof was written."
    }
    if ($Expected.sha256 -ne $Actual.sha256 -or
        [int64]$Expected.bytes -ne [int64]$Actual.bytes) {
        throw "$Label bytes do not match the paired-build proof. Rebuild the pair."
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = Join-Path $GameRoot '..\..\Phlosion\PhlosionEngine'
}
$EngineRoot = Resolve-FullPath $EngineRoot
if ([string]::IsNullOrWhiteSpace($GameBuildDirectory)) {
    $GameBuildDirectory = Join-Path $GameRoot 'build'
}
$GameBuildDirectory = Resolve-FullPath $GameBuildDirectory
if ([string]::IsNullOrWhiteSpace($EngineBuildDirectory)) {
    $EngineBuildDirectory = Join-Path $EngineRoot 'build'
}
$EngineBuildDirectory = Resolve-FullPath $EngineBuildDirectory

$gameCache = Join-Path $GameBuildDirectory 'CMakeCache.txt'
$engineCache = Join-Path $EngineBuildDirectory 'CMakeCache.txt'
if ((Get-CMakeCacheValue $gameCache 'PAC_BUILD_EDITOR') -ne 'ON') {
    throw "The game build tree has PAC_BUILD_EDITOR disabled."
}
if ((Get-CMakeCacheValue $engineCache 'PHLOSION_BUILD_EDITOR') -ne 'ON') {
    throw "The engine build tree has PHLOSION_BUILD_EDITOR disabled."
}
$configuredEngineRoot = Resolve-FullPath (
    Get-CMakeCacheValue $gameCache 'PHLOSION_ENGINE_SOURCE_DIR')
if ($configuredEngineRoot.TrimEnd('\') -ne $EngineRoot.TrimEnd('\')) {
    throw "The game build uses engine '$configuredEngineRoot', not '$EngineRoot'."
}

$configurations = if ($Configuration -eq 'All') {
    @('Debug', 'Release')
} else {
    @($Configuration)
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ')
    $OutputDirectory = Join-Path `
        $GameRoot `
        "artifacts\housekeeping\editor-pair-$stamp"
}
$OutputDirectory = Resolve-FullPath $OutputDirectory

$gamePathSpecs = @(
    'CMakeLists.txt',
    'CMakePresets.json',
    'vcpkg.json',
    'src',
    'tools',
    'tests'
)
$enginePathSpecs = @(
    'CMakeLists.txt',
    'CMakePresets.json',
    'vcpkg.json',
    'src',
    'tests'
)
$proofs = [Collections.Generic.List[object]]::new()

foreach ($config in $configurations) {
    Write-Host "=== $config editor/plugin pair ==="
    $gameStateBefore = Get-RepositorySourceState $GameRoot $gamePathSpecs
    $engineStateBefore = Get-RepositorySourceState $EngineRoot $enginePathSpecs

    $editorPath = Join-Path $EngineBuildDirectory "$config\PhlosionEditor.exe"
    $probePath = Join-Path `
        $EngineBuildDirectory `
        "$config\PhlosionEditorPluginProbe.exe"
    $pluginPath = Join-Path `
        $GameRoot `
        ".phlosion\editor\$config\PokemonAutochessEditorProject.dll"
    $stableProofPath = Join-Path `
        $GameRoot `
        ".phlosion\editor\$config\editor_pair_proof.json"

    if (-not $VerifyOnly) {
        Invoke-NativeCommand cmake @(
            '--build', $EngineBuildDirectory,
            '--config', $config,
            '--target', 'PhlosionEditor', 'PhlosionEditorPluginProbe',
            '--parallel'
        )
        Invoke-NativeCommand cmake @(
            '--build', $GameBuildDirectory,
            '--config', $config,
            '--target', 'PokemonAutochessEditorProject',
            '--parallel'
        )
    } elseif (-not (Test-Path -LiteralPath $stableProofPath -PathType Leaf)) {
        throw "No $config paired-build proof exists: $stableProofPath"
    }

    $gameStateAfter = Get-RepositorySourceState $GameRoot $gamePathSpecs
    $engineStateAfter = Get-RepositorySourceState $EngineRoot $enginePathSpecs
    Assert-StateUnchanged $gameStateBefore $gameStateAfter 'Game'
    Assert-StateUnchanged $engineStateBefore $engineStateAfter 'Engine'

    $probeOutput = Invoke-NativeCommand `
        $probePath `
        @($pluginPath) `
        -CaptureOutput
    $artifacts = [pscustomobject]@{
        editor = Get-ArtifactRecord $editorPath
        plugin_probe = Get-ArtifactRecord $probePath
        project_plugin = Get-ArtifactRecord $pluginPath
    }

    if ($VerifyOnly) {
        $expected = Get-Content -LiteralPath $stableProofPath -Raw |
            ConvertFrom-Json
        if ([int]$expected.schema_version -ne 1 -or
            $expected.configuration -ne $config) {
            throw "$config paired-build proof has an unsupported schema or configuration."
        }
        if ($expected.repositories.game.source_fingerprint -ne
            $gameStateAfter.source_fingerprint) {
            throw "Game source inputs changed since the $config proof. Rebuild the pair."
        }
        if ($expected.repositories.engine.source_fingerprint -ne
            $engineStateAfter.source_fingerprint) {
            throw "Engine source inputs changed since the $config proof. Rebuild the pair."
        }
        Assert-ProofMatches $expected.artifacts.editor $artifacts.editor 'Editor'
        Assert-ProofMatches `
            $expected.artifacts.plugin_probe `
            $artifacts.plugin_probe `
            'Plugin probe'
        Assert-ProofMatches `
            $expected.artifacts.project_plugin `
            $artifacts.project_plugin `
            'Project plugin'
        $proof = $expected
        $proof | Add-Member `
            -NotePropertyName verified_at_utc `
            -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) `
            -Force
        $proof.probe_output = $probeOutput
    } else {
        $proof = [pscustomobject]@{
            schema_version = 1
            generated_at_utc = [DateTime]::UtcNow.ToString('o')
            configuration = $config
            cmake = [pscustomobject]@{
                game_generator = Get-CMakeCacheValue $gameCache 'CMAKE_GENERATOR'
                engine_generator = Get-CMakeCacheValue $engineCache 'CMAKE_GENERATOR'
                game_build_directory = $GameBuildDirectory
                engine_build_directory = $EngineBuildDirectory
            }
            repositories = [pscustomobject]@{
                game = $gameStateAfter
                engine = $engineStateAfter
            }
            artifacts = $artifacts
            probe_output = $probeOutput
        }
        $proof | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $stableProofPath -Encoding UTF8
    }
    $proofs.Add($proof)
    Write-Host "$config pair is compatible and source-bound."
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$report = [pscustomobject]@{
    schema_version = 1
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    mode = if ($VerifyOnly) { 'verify_only' } else { 'build_and_verify' }
    game_root = $GameRoot
    engine_root = $EngineRoot
    proofs = @($proofs)
}
$jsonPath = Join-Path $OutputDirectory 'editor_pair_proof.json'
$markdownPath = Join-Path $OutputDirectory 'editor_pair_proof.md'
$report | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $jsonPath -Encoding UTF8

$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('# Paired Editor/Plugin Proof')
[void]$builder.AppendLine()
[void]$builder.AppendLine("Generated: $($report.generated_at_utc)")
[void]$builder.AppendLine()
[void]$builder.AppendLine("Mode: ``$($report.mode)``")
[void]$builder.AppendLine()
[void]$builder.AppendLine('| Configuration | Editor | Project plugin | ABI result |')
[void]$builder.AppendLine('| --- | --- | --- | --- |')
foreach ($proof in $proofs) {
    [void]$builder.AppendLine(
        "| $($proof.configuration) | ``$($proof.artifacts.editor.sha256)`` | ``$($proof.artifacts.project_plugin.sha256)`` | Compatible |")
}
[void]$builder.AppendLine()
[void]$builder.AppendLine('Each row binds artifact hashes to exact game and engine source fingerprints. The engine-owned CLI probe verified ABI version, contract size, public layout fingerprint, compiler ABI, configuration, and required runtime callbacks without creating a window or project runtime.')
Set-Content -LiteralPath $markdownPath -Value $builder.ToString() -Encoding UTF8

Write-Host "Paired-build report: $markdownPath"
Write-Host "Machine record: $jsonPath"
[pscustomobject]@{
    ReportPath = $markdownPath
    JsonPath = $jsonPath
    Proofs = @($proofs)
}
