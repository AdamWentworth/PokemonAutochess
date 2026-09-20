# Strict local/private content qualification entrypoint.
#
# Validation only: this script never cooks, syncs, promotes, or deletes real
# content unless -SyncDepot is passed explicitly with -DepotRoot. Every step is
# recorded under the output directory so a failure keeps its diagnostics, and a
# run reports "unqualified" (nonzero exit) when the cooked bundle, the pinned
# dependency revisions, a suitable GPU, or the optional paired editor are
# missing. Green here means the named scopes actually executed against the
# binaries this run built.

[CmdletBinding()]
param(
    [string]$BuildDir = "debug/ci_qualification/build-content",
    [string]$Config = "Debug",
    [string]$OutputDir = "debug/ci_qualification",
    [string]$DepotRoot = "",
    [switch]$SyncDepot,
    [switch]$IncludeVisual,
    [switch]$IncludeEditor,
    [string[]]$HardwareAdapter = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
Import-Module (Join-Path $PSScriptRoot "ci/CiCoverage.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "ci/ContentPreflight.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "ci/HardwarePreflight.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "ci/QualificationSupport.psm1") -Force

$hostExecutable = (Get-Process -Id $PID).Path
$outputDirAbs = Resolve-PacQualificationPath -RepoRoot $repoRoot -PathValue $OutputDir
$buildDirAbs = Resolve-PacQualificationPath -RepoRoot $repoRoot -PathValue $BuildDir
$logDir = Join-Path $outputDirAbs "logs"
$reportPath = Join-Path $outputDirAbs "content-qualification-report.json"
$coverageManifest = Join-Path $buildDirAbs "ci/coverage-manifest.json"
$editorConfigurations = @("Debug", "Release", "RelWithDebInfo")
$engineRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "../../Phlosion/PhlosionEngine"))
$startedAtUtc = [DateTime]::UtcNow
$steps = New-Object 'System.Collections.Generic.List[object]'

$contentIdentity = $null
$coverageSummary = $null
$dependencyIdentity = $null
$hardwareAssessment = $null
$hardwareAdapterSource = $null
$status = "unqualified"

New-Item -ItemType Directory -Path $logDir -Force | Out-Null

function Add-QualificationStep {
    param(
        [string]$Name,
        [string]$Status,
        [int]$ExitCode,
        [string]$Detail,
        [AllowNull()] [string]$LogPath
    )

    $steps.Add([pscustomobject][ordered]@{
        name = $Name
        status = $Status
        exit_code = $ExitCode
        detail = $Detail
        log = $LogPath
    })
    Write-Host ("[ContentQualification][{0}] {1} (exit {2})" -f $Name, $Status, $ExitCode)
    if (-not [string]::IsNullOrWhiteSpace($Detail)) {
        Write-Host ("[ContentQualification][{0}] {1}" -f $Name, $Detail)
    }
}

function Invoke-QualificationCommand {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$Arguments
    )

    $logPath = Join-Path $logDir ("{0}.log" -f $Name)
    # Children run from the repository root so relative content and config
    # paths resolve the same way from any caller directory.
    Push-Location $repoRoot
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $FilePath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
        Pop-Location
    }
    if ($null -eq $exitCode) { $exitCode = 0 }

    $lines = @($output | ForEach-Object { [string]$_ })
    $lines | Set-Content -LiteralPath $logPath -Encoding UTF8
    $lines | ForEach-Object { Write-Host $_ }

    if ($exitCode -ne 0) {
        Add-QualificationStep -Name $Name -Status "Failed" -ExitCode $exitCode -Detail ("See {0}" -f $logPath) -LogPath $logPath
        throw ("Qualification step '{0}' failed with exit code {1}." -f $Name, $exitCode)
    }
    Add-QualificationStep -Name $Name -Status "Passed" -ExitCode $exitCode -Detail "" -LogPath $logPath
    return $exitCode
}

function Invoke-QualificationScript {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
        Add-QualificationStep -Name $Name -Status "Failed" -ExitCode 1 -Detail ("Missing script {0}" -f $ScriptPath) -LogPath $null
        throw ("Qualification step '{0}' requires {1}, which does not exist." -f $Name, $ScriptPath)
    }
    $allArguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ScriptPath) + @($Arguments)
    return (Invoke-QualificationCommand -Name $Name -FilePath $hostExecutable -Arguments $allArguments)
}

# Runs a script child under the strict cooked-asset environment and restores the
# previous process values afterwards. The C++ importer contracts declare their
# own source semantics through the CTest ENVIRONMENT property, so only the
# renderer/editor children are scoped here.
function Invoke-QualificationScriptStrict {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    $strict = Get-PacStrictCookedEnvironmentVariable
    return (Invoke-PacWithEnvironment -Variables $strict -Script {
        Invoke-QualificationScript -Name $Name -ScriptPath $ScriptPath -Arguments $Arguments
    })
}

function Get-PacEditorPairState {
    param(
        [string]$GameBuildDir,
        [string]$Configuration
    )

    $state = [ordered]@{
        configuration = $Configuration
        engine_root = $engineRoot
        engine_build_editor = (Get-PacCMakeCacheValue -BuildDir (Join-Path $engineRoot "build") -Name "PHLOSION_BUILD_EDITOR")
        game_build_editor = (Get-PacCMakeCacheValue -BuildDir $GameBuildDir -Name "PAC_BUILD_EDITOR")
        editor_exe = Join-Path $engineRoot ("build/{0}/PhlosionEditor.exe" -f $Configuration)
        project_plugin = Join-Path $repoRoot (".phlosion/editor/{0}/PokemonAutochessEditorProject.dll" -f $Configuration)
        pair_proof = Join-Path $repoRoot (".phlosion/editor/{0}/editor_pair_proof.json" -f $Configuration)
    }
    $state["editor_exe_present"] = Test-Path -LiteralPath $state.editor_exe -PathType Leaf
    $state["project_plugin_present"] = Test-Path -LiteralPath $state.project_plugin -PathType Leaf
    $state["pair_proof_present"] = Test-Path -LiteralPath $state.pair_proof -PathType Leaf
    return $state
}

function Assert-PacEditorPrerequisite {
    param(
        [Parameter(Mandatory = $true)] $State
    )

    $problems = New-Object 'System.Collections.Generic.List[string]'
    if ($State.engine_build_editor -ne "ON") {
        $problems.Add(("the engine build tree does not have PHLOSION_BUILD_EDITOR=ON ({0})" -f $State.engine_root))
    }
    if (-not $State.editor_exe_present) {
        $problems.Add(("the paired editor executable is missing: {0}" -f $State.editor_exe))
    }
    if ($State.game_build_editor -eq "OFF") {
        $problems.Add("the game build tree has PAC_BUILD_EDITOR=OFF, so the project plugin cannot be produced; reconfigure with -DPAC_BUILD_EDITOR=ON")
    }
    if ($problems.Count -gt 0) {
        Add-QualificationStep -Name "editor-preflight" -Status "Unqualified" -ExitCode 2 -Detail ($problems -join "; ") -LogPath $null
        throw ("Editor qualification prerequisites are not met: " + ($problems -join "; "))
    }
}

function Build-PacContentIdentity {
    param([AllowNull()] $Preflight)

    $gameRevision = Get-PacGitRevision -RepositoryRoot $repoRoot
    if ($null -eq $Preflight) {
        return [pscustomobject][ordered]@{
            schema = "pac-content-identity-v1"
            git_revision = $gameRevision
            catalog = $null
            promotion_registry = $null
            cook_manifest = $null
            counts = $null
            ok = $false
        }
    }
    return [pscustomobject][ordered]@{
        schema = "pac-content-identity-v1"
        git_revision = $gameRevision
        catalog = (Get-PacContentPropertyValue $Preflight "catalog")
        promotion_registry = (Get-PacContentPropertyValue $Preflight "promotion_registry")
        cook_manifest = (Get-PacContentPropertyValue $Preflight "cook_manifest")
        counts = (Get-PacContentPropertyValue $Preflight "counts")
        ok = [bool](Get-PacContentPropertyValue $Preflight "ok")
    }
}

# Preserve diagnostics on failure and require a persisted report for success.
function Write-QualificationReport {
    param([string]$Status)

    try {
        $dirty = Get-PacGitDirtyState -RepositoryRoot $repoRoot
        $dependencies = $dependencyIdentity
        if ($null -eq $dependencies) {
            try {
                $dependencies = Get-PacBuildDependencyIdentity -RepoRoot $repoRoot -BuildDir $buildDirAbs
            } catch {
                $dependencies = [pscustomobject][ordered]@{
                    schema = "pac-build-dependency-identity-v1"
                    build_dir = $buildDirAbs
                    ok = $false
                    entries = @()
                    problems = @([string]$_.Exception.Message)
                }
            }
        }

        $report = [pscustomobject][ordered]@{
            schema = "pac-content-qualification-report-v1"
            status = $Status
            started_at_utc = $startedAtUtc.ToString("o")
            finished_at_utc = ([DateTime]::UtcNow).ToString("o")
            game = [ordered]@{
                repo_root = $repoRoot
                git_revision = (Get-PacGitRevision -RepositoryRoot $repoRoot)
                dirty_state = $dirty.state
                dirty = $dirty.dirty
                changed_paths = @($dirty.changed_paths)
                build_dir = $buildDirAbs
                config = $Config
            }
            dependencies = $dependencies
            content = $contentIdentity
            coverage = $coverageSummary
            hardware = [ordered]@{
                adapter_source = $hardwareAdapterSource
                assessment = $hardwareAssessment
            }
            requested_scopes = [ordered]@{
                content = $true
                visual = [bool]$IncludeVisual
                editor = [bool]$IncludeEditor
            }
            steps = @($steps.ToArray())
        }

        $parent = Split-Path -Parent $reportPath
        if (-not [string]::IsNullOrWhiteSpace($parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        ($report | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $reportPath -Encoding UTF8
        Write-Host ("[ContentQualification] Report: {0}" -f $reportPath)

        if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
            Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("### Content qualification: {0}" -f $Status)
            foreach ($step in @($steps.ToArray())) {
                Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ("- {0}: {1} (exit {2})" -f $step.name, $step.status, $step.exit_code)
            }
        }
        return $true
    } catch {
        Write-Host ("[ContentQualification] Could not write the report at {0}: {1}" -f $reportPath, $_.Exception.Message) -ForegroundColor Red
        return $false
    }
}

$preflight = $null
$editorState = $null

try {
    Push-Location $repoRoot
    try {
        Write-Host ("[ContentQualification] Repo: {0}" -f $repoRoot)
        Write-Host ("[ContentQualification] Build: {0} ({1})" -f $buildDirAbs, $Config)

        # ---------------- Option validation before any long step ----------------
        if ($SyncDepot -and [string]::IsNullOrWhiteSpace($DepotRoot)) {
            Add-QualificationStep -Name "validate-options" -Status "Failed" -ExitCode 2 -Detail "-SyncDepot requires an explicit -DepotRoot" -LogPath $null
            throw "-SyncDepot requires an explicit -DepotRoot. Content qualification never syncs a guessed depot."
        }
        if ($IncludeEditor -and $editorConfigurations -notcontains $Config) {
            Add-QualificationStep -Name "validate-options" -Status "Failed" -ExitCode 2 -Detail ("editor configuration '{0}' is not one of {1}" -f $Config, ($editorConfigurations -join ", ")) -LogPath $null
            throw ("Editor qualification supports only {0}; '{1}' was requested." -f ($editorConfigurations -join ", "), $Config)
        }
        $cachePresent = Test-Path -LiteralPath (Join-Path $buildDirAbs "CMakeCache.txt") -PathType Leaf
        if ($IncludeEditor) {
            $editorState = Get-PacEditorPairState -GameBuildDir $buildDirAbs -Configuration $Config
            Assert-PacEditorPrerequisite -State $editorState
            Add-QualificationStep -Name "editor-preflight" -Status "Passed" -ExitCode 0 -Detail ("editor={0}" -f $editorState.editor_exe) -LogPath $null
        }
        Add-QualificationStep -Name "validate-options" -Status "Passed" -ExitCode 0 -Detail ("existingCache={0}" -f $cachePresent) -LogPath $null

        if ($SyncDepot) {
            Invoke-QualificationScript -Name "sync-depot" -ScriptPath (Join-Path $PSScriptRoot "assets/sync_asset_depot.ps1") -Arguments @("-DepotRoot", $DepotRoot) | Out-Null
        }

        # ---------------- GPU preflight for the requested render scopes ----------------
        if ($IncludeVisual -or $IncludeEditor) {
            if ($HardwareAdapter.Count -gt 0) {
                $hardwareAdapterSource = "override"
                $hardwareAssessment = Get-PacHardwareAssessment -Adapters $HardwareAdapter
            } else {
                $hardwareAdapterSource = "enumerated"
                $hardwareAssessment = Get-PacHardwareAssessment
            }
            Write-Host ("[ContentQualification] Video adapters: {0}" -f ($hardwareAssessment.adapters -join ", "))
            if (-not $hardwareAssessment.ok) {
                Add-QualificationStep -Name "gpu-preflight" -Status "Unqualified" -ExitCode 2 -Detail ($hardwareAssessment.problems -join "; ") -LogPath $null
                throw ("A render qualification needs a real GPU: " + ($hardwareAssessment.problems -join "; "))
            }
            if ($HardwareAdapter.Count -gt 0) {
                Add-QualificationStep -Name "gpu-preflight" -Status "Unqualified" -ExitCode 2 -Detail "Adapter overrides are diagnostic fixtures; qualification requires enumerated hardware." -LogPath $null
                throw "Remove -HardwareAdapter to qualify the actual machine."
            }
            Add-QualificationStep -Name "gpu-preflight" -Status "Passed" -ExitCode 0 -Detail (
                "adapters={0} source={1}" -f ($hardwareAssessment.hardware_adapters -join ", "), $hardwareAdapterSource) -LogPath $null
        }

        # ---------------- Published cooked bundle ----------------
        Write-Host "[ContentQualification] Preflight: validating the published cooked bundle."
        $preflight = Invoke-PacContentPreflight -RepoRoot $repoRoot -RequireSourceInputs
        $contentIdentity = Build-PacContentIdentity -Preflight $preflight
        if (-not $preflight.ok) {
            $detail = @()
            foreach ($entry in @($preflight.errors)) { $detail += [string]$entry }
            foreach ($entry in @($preflight.missing)) { $detail += ("missing cooked runtime path: " + [string]$entry.path) }
            foreach ($entry in @($preflight.missing_source_inputs)) { $detail += ("missing importer source input: " + [string]$entry.path) }
            Add-QualificationStep -Name "content-preflight" -Status "Unqualified" -ExitCode 2 -Detail ($detail -join "; ") -LogPath $null
            throw ("Private content is missing or stale; the qualified corpus is required. " + ($detail -join "; "))
        }
        Add-QualificationStep -Name "content-preflight" -Status "Passed" -ExitCode 0 -Detail (
            "objects={0} textureDependencies={1} sharedDependencies={2}" -f `
                $preflight.counts.objects, $preflight.counts.texture_dependencies, $preflight.counts.shared_dependencies) -LogPath $null

        if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT) -and (Test-Path (Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"))) {
            $toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
        } else {
            throw "VCPKG_ROOT is not set to a vcpkg checkout; content qualification cannot configure the build."
        }

        $configureArguments = @(
            "-S", $repoRoot,
            "-B", $buildDirAbs,
            ("-DCMAKE_TOOLCHAIN_FILE={0}" -f $toolchain),
            "-DPAC_BUILD_TOOLS=ON",
            "-DBUILD_TESTING=ON",
            "-DPAC_ENABLE_PRIVATE_ASSET_TESTS=ON",
            ("-DPAC_PRIVATE_CONTENT_ROOT={0}" -f $repoRoot)
        )
        if ($cachePresent) {
            # Never override an existing editor configuration: it is the build
            # directory's choice and reconfiguring it would silently invalidate
            # the paired plugin.
            Write-Host "[ContentQualification] Reusing existing build directory; preserving its editor configuration."
        } else {
            $configureArguments += ("-DPAC_BUILD_EDITOR={0}" -f $(if ($IncludeEditor) { "ON" } else { "OFF" }))
        }
        Invoke-QualificationCommand -Name "configure" -FilePath "cmake" -Arguments $configureArguments | Out-Null

        $manifest = Read-CiCoverageManifest -Path $coverageManifest
        $null = Assert-CiCtestScope -BuildDir $buildDirAbs -Config $Config -Manifest $manifest -ExpectedMode "content"
        foreach ($line in @(Get-CiCoverageSummaryLines -Manifest $manifest)) { Write-Host $line }
        $coverageSummary = [ordered]@{
            manifest = $coverageManifest
            mode = $manifest.mode
            counts = $manifest.counts
            suites = @($manifest.suites | ForEach-Object {
                [pscustomobject][ordered]@{
                    name = [string]$_.name
                    status = [string]$_.status
                    tests = @($_.tests).Count
                    prerequisites = @($_.prerequisites).Count
                }
            })
        }

        # ---------------- Resolved dependency revisions ----------------
        $dependencyIdentity = Get-PacBuildDependencyIdentity -RepoRoot $repoRoot -BuildDir $buildDirAbs
        if (-not $dependencyIdentity.ok) {
            Add-QualificationStep -Name "dependency-identity" -Status "Unqualified" -ExitCode 2 -Detail ($dependencyIdentity.problems -join "; ") -LogPath $null
            throw ("The build resolved dependency revisions that do not match the pinned commits: " + ($dependencyIdentity.problems -join "; "))
        }
        Add-QualificationStep -Name "dependency-identity" -Status "Passed" -ExitCode 0 -Detail (
            (@($dependencyIdentity.entries | ForEach-Object { "{0}={1}" -f $_.name, $_.resolved_head }) -join ", ")) -LogPath $null

        # ---------------- Build the full selected graph ----------------
        # Build every default target, exactly like the hosted CI build, so the
        # contract binaries, the arena-logic suite (a real CTest entry point the
        # earlier hand-picked target list silently omitted), the game executable
        # used by the render matrix, the Forge validator and, when configured,
        # the editor plugin are all rebuilt. A qualification never grades a
        # stale binary.
        Invoke-QualificationCommand -Name "build" -FilePath "cmake" -Arguments @(
            "--build", $buildDirAbs, "--config", $Config) | Out-Null

        # ---------------- Forge cooked validation ----------------
        $forgeExe = Get-PacBuildExecutablePath -BuildDir $buildDirAbs -Config $Config -Name "PhlosionForge"
        if ($null -eq $forgeExe) {
            Add-QualificationStep -Name "forge-validate" -Status "Failed" -ExitCode 1 -Detail "PhlosionForge.exe was not produced by the build" -LogPath $null
            throw "PhlosionForge.exe was not produced by the build; cooked typed-object and dependency hash integrity cannot be validated."
        }
        Invoke-QualificationCommand -Name "forge-validate" -FilePath $forgeExe -Arguments @("validate") | Out-Null
        Invoke-QualificationScript -Name "kanto-model-promotions" -ScriptPath (Join-Path $PSScriptRoot "assets/validate_kanto_model_promotions.ps1") | Out-Null
        Invoke-QualificationScript -Name "native-model-payloads" -ScriptPath (Join-Path $PSScriptRoot "assets/validate_native_model_payloads.ps1") | Out-Null
        Invoke-QualificationCommand -Name "validate-data" -FilePath "cmake" -Arguments @(
            "--build", $buildDirAbs, "--config", $Config, "--target", "PAC_ValidateData") | Out-Null

        # ---------------- Content CTest scope ----------------
        Invoke-QualificationScript -Name "ctest-content" -ScriptPath (Join-Path $PSScriptRoot "run_ctest_ci.ps1") -Arguments @(
            "-BuildDir", $buildDirAbs,
            "-Config", $Config,
            "-ExpectedMode", "content",
            "-RejectRuntimeFallback",
            "-CoverageManifest", $coverageManifest) | Out-Null

        # ---------------- Optional renderer qualification ----------------
        if ($IncludeVisual) {
            Invoke-QualificationScriptStrict -Name "visual-qualification" -ScriptPath (Join-Path $PSScriptRoot "renderer_qualification.ps1") -Arguments @(
                "-BuildDir", $buildDirAbs,
                "-Config", $Config,
                "-NoBuild") | Out-Null
        }

        # ---------------- Optional paired editor qualification ----------------
        if ($IncludeEditor) {
            $editorState = Get-PacEditorPairState -GameBuildDir $buildDirAbs -Configuration $Config
            $problems = New-Object 'System.Collections.Generic.List[string]'
            if (-not $editorState.project_plugin_present) {
                $problems.Add(("the paired project plugin is missing: {0}" -f $editorState.project_plugin))
            }
            if (-not $editorState.pair_proof_present) {
                $problems.Add(("the editor pair proof is missing: {0}" -f $editorState.pair_proof))
            }
            if ($problems.Count -gt 0) {
                Add-QualificationStep -Name "editor-qualification" -Status "Unqualified" -ExitCode 2 -Detail ($problems -join "; ") -LogPath $null
                throw ("Editor qualification requires a built paired editor/plugin, not only the host executable: " + ($problems -join "; "))
            }
            Invoke-QualificationScriptStrict -Name "editor-qualification" -ScriptPath (Join-Path $PSScriptRoot "housekeeping/check_editor_workflow.ps1") -Arguments @(
                "-Configuration", $Config) | Out-Null
        }

        $status = "qualified"
    } finally {
        Pop-Location
    }
} catch {
    Write-Host ("[ContentQualification] FAILED: {0}" -f $_.Exception.Message) -ForegroundColor Red
    $status = "unqualified"
} finally {
    if (-not (Write-QualificationReport -Status $status)) { $status = "unqualified" }
}

if ($status -ne "qualified") { exit 1 }
exit 0
