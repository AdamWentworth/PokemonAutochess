param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [switch]$IncludePreviewSmoke,
    [switch]$SourceScope,
    [switch]$IncludeRuntimeVisualSmoke,
    [switch]$IncludeRenderParity,
    [switch]$IncludePerfSmoke,
    [string]$PerfConfig = "Release",
    [string]$PrivateContentRoot = "",
    [switch]$ConfigureOnly,
    [string]$ReportPath = "debug/ci_qualification/full-check-report.json"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildDirAbs = if ([IO.Path]::IsPathRooted($BuildDir)) {
    [IO.Path]::GetFullPath($BuildDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}
$reportAbs = if ([IO.Path]::IsPathRooted($ReportPath)) {
    [IO.Path]::GetFullPath($ReportPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $ReportPath))
}
$privateContentRootAbs = $null
if (-not [string]::IsNullOrWhiteSpace($PrivateContentRoot)) {
    $privateContentRootAbs = [IO.Path]::GetFullPath($PrivateContentRoot)
}

# full_check is the local gate that already owns the private corpus, so it opts
# into the content suite explicitly. -SourceScope selects the asset-independent
# suite instead, and the requested scope is always applied to the build
# directory, including an existing one: without that, a build directory first
# configured for one scope would silently answer for the other.
$scope = if ($SourceScope) { "source" } else { "content" }
$privateAssetTests = if ($SourceScope) { "OFF" } else { "ON" }
$cachePath = Join-Path $buildDirAbs "CMakeCache.txt"
$hadCache = Test-Path -LiteralPath $cachePath

$steps = New-Object 'System.Collections.Generic.List[object]'
$configureArguments = @()

function Add-FullCheckStep {
    param(
        [string]$Name,
        [string]$Status,
        [string]$Detail = ""
    )

    $steps.Add([pscustomobject][ordered]@{
        name = $Name
        status = $Status
        detail = $Detail
    })
    if ([string]::IsNullOrWhiteSpace($Detail)) {
        Write-Host ("[FullCheck][{0}] {1}" -f $Status, $Name)
    } else {
        Write-Host ("[FullCheck][{0}] {1}: {2}" -f $Status, $Name, $Detail)
    }
}

function Write-FullCheckReport {
    param([string]$Status)

    $report = [pscustomobject][ordered]@{
        schema = "pac-full-check-report-v1"
        status = $Status
        repo_root = $repoRoot
        build_dir = $buildDirAbs
        config = $Config
        scope = $scope
        private_asset_tests = $privateAssetTests
        private_content_root = $privateContentRootAbs
        had_cache = $hadCache
        editor_configuration_preserved = $hadCache
        configure_only = [bool]$ConfigureOnly
        configure_arguments = @($configureArguments)
        steps = @($steps.ToArray())
        passed = ($Status -eq "passed")
    }

    $parent = Split-Path -Parent $reportAbs
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportAbs -Encoding UTF8
    Write-Host ("[FullCheck] Report: {0}" -f $reportAbs)
}

function Assert-LastExitCode {
    param([string]$Step)

    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

$status = "failed"

try {
    $toolchain = $null
    if ($env:VCPKG_ROOT) {
        $toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
    }
    if (-not $toolchain -or -not (Test-Path $toolchain)) {
        throw "VCPKG_ROOT is not set or toolchain not found. Set VCPKG_ROOT and retry."
    }

    $configureArguments = @(
        "-S", $repoRoot,
        "-B", $buildDirAbs,
        ("-DCMAKE_TOOLCHAIN_FILE={0}" -f $toolchain),
        "-DPAC_BUILD_TOOLS=ON",
        "-DBUILD_TESTING=ON",
        ("-DPAC_ENABLE_PRIVATE_ASSET_TESTS={0}" -f $privateAssetTests)
    )
    if ($null -ne $privateContentRootAbs) {
        $configureArguments += ("-DPAC_PRIVATE_CONTENT_ROOT={0}" -f $privateContentRootAbs)
    }
    if ($hadCache) {
        # Never override an existing editor configuration: the local build
        # directory owns that choice.
        Add-FullCheckStep -Name "configure" -Status "reconfigure" -Detail (
            "existing cache; applying PAC_ENABLE_PRIVATE_ASSET_TESTS={0} and preserving PAC_BUILD_EDITOR" -f $privateAssetTests)
    } else {
        $configureArguments += "-DPAC_BUILD_EDITOR=ON"
    }

    & cmake @configureArguments
    Assert-LastExitCode "Configure"
    Add-FullCheckStep -Name "configure" -Status "ran" -Detail ("scope={0}" -f $scope)

    # Run script gates in a child shell so their explicit nonzero exit codes cannot
    # be overwritten by the later successful build or test command.
    $taskValidationShell = (Get-Process -Id $PID).Path

    & $taskValidationShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "check_docs_hygiene.ps1") -BuildDir $buildDirAbs
    Assert-LastExitCode "Docs hygiene"
    Add-FullCheckStep -Name "docs-hygiene" -Status "ran"

    # The promotion validator resolves native sources and the published cook
    # manifest, so it only belongs to the content scope. The source scope is
    # explicitly asset-independent and must not reach for private content.
    if ($SourceScope) {
        Add-FullCheckStep -Name "kanto-model-promotions" -Status "skipped" -Detail "source scope is asset-independent"
    } else {
        & $taskValidationShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "assets\validate_kanto_model_promotions.ps1")
        Assert-LastExitCode "Kanto model promotions"
        Add-FullCheckStep -Name "kanto-model-promotions" -Status "ran"
    }

    $runPreviewSmoke = $IncludePreviewSmoke.IsPresent
    if (-not $runPreviewSmoke) {
        $runPreviewSmoke = $env:PAC_ENABLE_PREVIEW_SMOKE_TESTS -eq "1"
    }

    $runRuntimeVisualSmoke = $IncludeRuntimeVisualSmoke.IsPresent
    if (-not $runRuntimeVisualSmoke) {
        $runRuntimeVisualSmoke = $env:PAC_ENABLE_RUNTIME_VISUAL_SMOKE_TESTS -eq "1"
    }

    $runRenderParity = $IncludeRenderParity.IsPresent
    if (-not $runRenderParity) {
        $runRenderParity = $env:PAC_ENABLE_RENDER_PARITY_TESTS -eq "1"
    }

    $runPerfSmoke = $IncludePerfSmoke.IsPresent
    if (-not $runPerfSmoke) {
        $runPerfSmoke = $env:PAC_ENABLE_PERF_SMOKE_TESTS -eq "1"
    }

    if ($ConfigureOnly) {
        foreach ($skippedStep in @("build", "ctest", "validate-data", "preview-smoke", "runtime-visual-smoke", "render-parity", "perf-smoke")) {
            Add-FullCheckStep -Name $skippedStep -Status "skipped" -Detail "-ConfigureOnly was requested"
        }
    } else {
        if ($runPerfSmoke) {
            # Build the Release perf target before the long Debug test pass so the
            # later perf smoke measures a settled binary instead of a just-built
            # hot run.
            cmake --build $buildDirAbs --config $PerfConfig --target PokemonAutochess
            Assert-LastExitCode "Perf smoke prebuild"
            Add-FullCheckStep -Name "perf-smoke-prebuild" -Status "ran"
        }

        cmake --build $buildDirAbs --config $Config
        Assert-LastExitCode "Build"
        Add-FullCheckStep -Name "build" -Status "ran"

        # The wrapper owns the coverage manifest guard, the zero-test guard and
        # the requested-scope assertion, so a local run reports exactly what it
        # selected.
        & $taskValidationShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run_ctest_ci.ps1") `
            -BuildDir $buildDirAbs -Config $Config -ExpectedMode $scope
        Assert-LastExitCode "CTest"
        Add-FullCheckStep -Name "ctest" -Status "ran" -Detail ("scope={0}" -f $scope)

        cmake --build $buildDirAbs --config $Config --target PAC_ValidateData
        Assert-LastExitCode "PAC_ValidateData"
        Add-FullCheckStep -Name "validate-data" -Status "ran"

        if ($runPreviewSmoke) {
            & (Join-Path $PSScriptRoot "vfx_preview_visual_smoke.ps1") -BuildDir $buildDirAbs -Config $Config
            Assert-LastExitCode "Preview visual smoke"
            Add-FullCheckStep -Name "preview-smoke" -Status "ran"
        } else {
            Add-FullCheckStep -Name "preview-smoke" -Status "skipped" -Detail "not requested"
        }

        if ($runRuntimeVisualSmoke) {
            & (Join-Path $PSScriptRoot "runtime_visual_smoke.ps1") -BuildDir $buildDirAbs -Config $Config -RepoRoot $repoRoot
            Assert-LastExitCode "Runtime visual smoke"
            Add-FullCheckStep -Name "runtime-visual-smoke" -Status "ran"
        } else {
            Add-FullCheckStep -Name "runtime-visual-smoke" -Status "skipped" -Detail "not requested"
        }

        if ($runRenderParity) {
            & (Join-Path $PSScriptRoot "render_parity_matrix.ps1") -BuildDir $buildDirAbs -Config $Config
            Assert-LastExitCode "Render parity matrix"
            Add-FullCheckStep -Name "render-parity" -Status "ran"
        } else {
            Add-FullCheckStep -Name "render-parity" -Status "skipped" -Detail "not requested"
        }

        if ($runPerfSmoke) {
            & (Join-Path $PSScriptRoot "perf_smoke_guard.ps1") -BuildDir $buildDirAbs -Config $PerfConfig -NoBuild
            Assert-LastExitCode "Perf smoke"
            Add-FullCheckStep -Name "perf-smoke" -Status "ran"
        } else {
            Add-FullCheckStep -Name "perf-smoke" -Status "skipped" -Detail "not requested"
        }
    }

    $status = "passed"
} catch {
    Write-Host ("[FullCheck] FAILED: {0}" -f $_.Exception.Message) -ForegroundColor Red
    Add-FullCheckStep -Name "failure" -Status "failed" -Detail ([string]$_.Exception.Message)
    $status = "failed"
} finally {
    Write-FullCheckReport -Status $status
}

if ($status -ne "passed") {
    exit 1
}
exit 0
