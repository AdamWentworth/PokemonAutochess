# Contract tests for the hosted CI workflow.
#
# These checks assert the security and coverage semantics of ci.yml. They are not
# a shape assertion: the trust boundary is read from the job-level if expression
# itself (comment text cannot satisfy it), every checkout step must opt out of
# persisted credentials individually, and every action must stay pinned. The same
# checks are then re-run against mutated copies of the workflow, so the fixture
# proves the contract actually bites when the trust gate is removed or broadened,
# when pull_request_target comes back, when an action is unpinned, when
# credentials would be persisted, when the hosted scope switches to the private
# content suite, and when the evidence upload stops running on failure.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$workflowPath = Join-Path $repoRoot ".github\workflows\ci.yml"
$manifestPath = Join-Path $repoRoot "vcpkg.json"
$ctestRunnerPath = Join-Path $repoRoot "tools\run_ctest_ci.ps1"

$checks = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)

    $script:checks += 1
    if (-not $Condition) { throw $Message }
}

function Assert-Match {
    param([string]$Value, [string]$Pattern, [string]$Message)

    $script:checks += 1
    if ($Value -notmatch $Pattern) {
        throw ("{0} (value: {1})" -f $Message, $Value)
    }
}

function Get-YamlTopLevelBlock {
    param([string]$Text, [string]$Key)

    $match = [regex]::Match($Text, ('(?ms)^' + [regex]::Escape($Key) + ':[ \t]*\r?\n(.*?)(?=^[^\s]|\z)'))
    if (-not $match.Success) { return $null }
    return $match.Groups[1].Value
}

# Job keys live at two-space indentation, which is also used by the top-level
# trigger block. Extract the jobs block first so trigger keys such as "push:"
# can never be mistaken for jobs.
function Get-YamlJobBlocks {
    param([string]$Text)

    $jobsBlock = Get-YamlTopLevelBlock -Text $Text -Key "jobs"
    if ($null -eq $jobsBlock) { return [ordered]@{} }

    $blocks = [ordered]@{}
    $matches = [regex]::Matches($jobsBlock, '(?ms)^  ([A-Za-z0-9_-]+):[ \t]*\r?\n(.*?)(?=^  [A-Za-z0-9_-]+:|\z)')
    foreach ($match in $matches) {
        $blocks[$match.Groups[1].Value] = $match.Groups[2].Value
    }
    return $blocks
}

# The job-level condition is the only line indented exactly four spaces inside a
# job block; step conditions are nested deeper, so they can never stand in for a
# job-wide trust gate.
function Get-YamlJobIfExpression {
    param([string]$JobBlock)

    $match = [regex]::Match($JobBlock, '(?m)^    if:[ \t]*([^\r\n]+?)[ \t]*$')
    if (-not $match.Success) { return $null }
    return $match.Groups[1].Value
}

function Get-YamlStepBlocks {
    param([string]$JobBlock)

    return @([regex]::Matches($JobBlock, '(?ms)^      - (.*?)(?=^      - |\z)') |
        ForEach-Object { $_.Groups[1].Value })
}

function Assert-CiWorkflowContract {
    param(
        [Parameter(Mandatory = $true)] [string]$Workflow,
        [Parameter(Mandatory = $true)] $Manifest,
        [string]$CtestRunner = ""
    )

    # ---------------- Triggers, permissions and cancellation ----------------
    $onBlock = Get-YamlTopLevelBlock -Text $Workflow -Key "on"
    Assert-True ($null -ne $onBlock) "CI must declare triggers."
    Assert-Match $onBlock '(?m)^  push:' "CI must run on push."
    Assert-Match $onBlock '(?m)^  pull_request:' "CI must run on pull_request."
    Assert-Match $onBlock '(?m)^  workflow_dispatch:' "CI must expose manual dispatch."
    Assert-Match $onBlock '(?m)^      - master\s*$' "Push builds must be restricted to master."
    Assert-True ($Workflow -notmatch '(?m)^\s*schedule:') "The retired schedule trigger must not come back."
    Assert-True ($Workflow -notmatch 'cron:') "The retired daily cron must not come back."
    Assert-True ($Workflow -notmatch 'pull_request_target') "pull_request_target would expose secrets to untrusted forks."
    Assert-Match $Workflow '(?m)^permissions:\r?\n  contents: read\s*$' "CI must use an explicit read-only default token permission."
    Assert-True ($Workflow -notmatch 'persist-credentials:\s*true') "Checkout credentials must never be persisted."

    $concurrency = Get-YamlTopLevelBlock -Text $Workflow -Key "concurrency"
    Assert-True ($null -ne $concurrency) "CI must cancel superseded runs."
    Assert-Match $concurrency 'cancel-in-progress:\s*true' "Superseded CI runs must be cancelled."

    # ---------------- Job trust boundaries ----------------
    $jobs = Get-YamlJobBlocks -Text $Workflow
    Assert-True ($jobs.Count -ge 2) "CI must separate secret-free contract checks from the trusted full build."

    $secretBearingJobs = @()
    foreach ($name in $jobs.Keys) {
        if ($jobs[$name] -match 'secrets\.') { $secretBearingJobs += $name }
    }
    Assert-True ($secretBearingJobs.Count -eq 1) `
        "Exactly one job may own the private dependency deploy key."

    $trustedJob = [string]$secretBearingJobs[0]
    $trustedBlock = [string]$jobs[$trustedJob]
    Assert-True ($trustedBlock -match 'secrets\.PHLOSION_VFX_DEPLOY_KEY') `
        "The trusted job must own the pinned private Phlosion VFX deploy key."

    $trustCondition = Get-YamlJobIfExpression -JobBlock $trustedBlock
    Assert-True (-not [string]::IsNullOrWhiteSpace($trustCondition)) `
        "The secret-bearing job must gate itself with a job-level trust condition, not with step text."
    Assert-Match $trustCondition 'github\.event_name' `
        "The job-level trust condition must branch on the event name."
    Assert-Match $trustCondition 'pull_request' `
        "The job-level trust condition must distinguish pull requests."
    Assert-Match $trustCondition 'head\.repo\.full_name\s*==\s*github\.repository' `
        "The job-level trust condition must require a same-repository pull request."
    Assert-Match $trustCondition 'dependabot' `
        "The job-level trust condition must exclude Dependabot from secret access."

    foreach ($name in $jobs.Keys) {
        if ($name -eq $trustedJob) { continue }
        Assert-True ($jobs[$name] -notmatch 'secrets\.') `
            ("Job '{0}' must never reference a repository secret." -f $name)
        $jobCondition = Get-YamlJobIfExpression -JobBlock $jobs[$name]
        if (-not [string]::IsNullOrWhiteSpace($jobCondition)) {
            Assert-True ($jobCondition -notmatch 'event_name|pull_request') `
                ("Secret-free job '{0}' must run for every pull request." -f $name)
        }
    }

    # ---------------- Checkout credential hygiene ----------------
    $checkoutSteps = 0
    foreach ($name in $jobs.Keys) {
        foreach ($step in @(Get-YamlStepBlocks -JobBlock $jobs[$name])) {
            if ($step -notmatch 'uses:\s*actions/checkout@') { continue }
            $checkoutSteps += 1
            Assert-Match $step '(?m)^\s+persist-credentials:\s*false\s*$' `
                ("Every checkout step must set persist-credentials: false (job '{0}': {1})" -f $name, $step)
        }
    }
    Assert-True ($checkoutSteps -ge 2) `
        "CI must check out both the repository and the pinned private dependency."

    # ---------------- Checkout pinning ----------------
    $usesLines = [regex]::Matches($Workflow, '(?m)^\s+uses:\s*(\S+)\s*(#\s*\S+)?\s*$')
    Assert-True ($usesLines.Count -ge 3) "CI must use pinned GitHub Actions."
    foreach ($use in $usesLines) {
        $reference = $use.Groups[1].Value
        if ($reference.StartsWith("./")) { continue }
        $parts = $reference.Split("@")
        Assert-True ($parts.Count -eq 2) ("Action reference '{0}' must pin an immutable revision." -f $reference)
        Assert-True ($parts[1] -match '^[0-9a-f]{40}$') `
            ("Action reference '{0}' must pin a full 40-character commit SHA." -f $reference)
        Assert-True (-not [string]::IsNullOrWhiteSpace($use.Groups[2].Value)) `
            ("Action reference '{0}' must keep its release-version comment for review." -f $reference)
    }

    $privateCheckouts = @([regex]::Matches($Workflow, '(?m)^\s+repository:\s+AdamWentworth/PhlosionVFX\s*$'))
    Assert-True ($privateCheckouts.Count -ge 1) `
        "The private Phlosion VFX dependency must be checked out from its pinned repository."
    $vfxBlocks = @([regex]::Matches($Workflow, '(?ms)repository:\s+AdamWentworth/PhlosionVFX\r?\n(.*?)(?=^      - name:|\z)'))
    Assert-True ($vfxBlocks.Count -eq $privateCheckouts.Count) `
        "Every Phlosion VFX checkout must be a self-contained step."
    foreach ($block in $vfxBlocks) {
        $body = $block.Groups[1].Value
        Assert-Match $body '(?m)^\s+ref:\s+[0-9a-f]{40}\s*$' `
            "Every private Phlosion VFX checkout must use an immutable commit pin."
        Assert-True ($body.Contains('ssh-key: ${{ secrets.PHLOSION_VFX_DEPLOY_KEY }}')) `
            "Every private Phlosion VFX checkout must use the repository-scoped deploy key."
        Assert-Match $body '(?m)^\s+persist-credentials:\s*false\s*$' `
            "The deploy key must never be persisted into the checkout."
    }

    # ---------------- Dependencies and configured scope ----------------
    $baseline = [string]$Manifest.'builtin-baseline'
    Assert-True (-not [string]::IsNullOrWhiteSpace($baseline)) "vcpkg.json must declare a builtin baseline."
    Assert-Match $Workflow ("(?m)^\s+VCPKG_COMMIT:\s+" + [regex]::Escape($baseline) + "\s*$") `
        "CI's vcpkg executable commit must match the manifest builtin baseline."
    Assert-True ($Workflow -notmatch 'vcpkg.*fetch\s+--depth') `
        "vcpkg must retain registry history so manifest version overrides remain resolvable."
    Assert-Match $Workflow '-DPHLOSION_VFX_SOURCE_DIR=' `
        "The trusted configure command must use the checked-out Phlosion VFX source."
    Assert-Match $Workflow '-DPAC_ENABLE_PRIVATE_ASSET_TESTS=OFF' `
        "Hosted CI must select the explicit source suite instead of presence-based shrinking."
    Assert-Match $Workflow '-DPAC_BUILD_EDITOR=OFF' `
        "Hosted CI must disable the local-only editor package graph."

    # ---------------- Required checks and evidence ----------------
    foreach ($script in @(
            'tools/check_script_syntax.ps1',
            'tools/test_ci_coverage_contract.ps1',
            'tools/test_ci_ctest_wrapper.ps1',
            'tools/test_ci_hardware_preflight.ps1',
            'tools/test_ci_qualification_support.ps1',
            'tools/test_qualify_content_entrypoint.ps1',
            'tools/test_private_content_preflight.ps1',
            'tools/check_docs_hygiene.ps1',
            'tools/run_ctest_ci.ps1')) {
        Assert-True ($Workflow.Contains($script)) `
            ("CI must run {0} in the appropriate job." -f $script)
    }
    Assert-Match $Workflow 'tools/run_ctest_ci\.ps1[^\r\n]*-ExpectedMode\s+source' `
        "The hosted CTest wrapper must declare the configured source scope."
    Assert-True ($Workflow -notmatch 'runtime_visual_smoke\.ps1') `
        "Hosted CI must not run the GPU smoke lane until a suitable private runner exists."
    Assert-True ($Workflow -notmatch 'self-hosted') `
        "Hosted CI must not schedule GPUs onto an unprovisioned runner."
    Assert-Match $Workflow 'actions/upload-artifact@[0-9a-f]{40}' "CI must upload public evidence."
    Assert-Match $Workflow '(?ms)Upload public CI evidence.*?if:\s*always\(\)' `
        "Public CI evidence must be uploaded even when the job fails."
    Assert-True ($Workflow -match 'coverage-manifest\.json' -and $Workflow -match 'ctest\.log') `
        "Uploaded evidence must include the coverage manifest and the CTest log."

    # ---------------- CTest wrapper contract ----------------
    if (-not [string]::IsNullOrWhiteSpace($CtestRunner)) {
        Assert-Match $CtestRunner 'LastTestsFailed\.log' `
            "The CI CTest wrapper must inspect CTest's failed-test ledger."
        Assert-Match $CtestRunner '-Title\s+"CTest failure"' `
            "The CI CTest wrapper must publish failed test names as GitHub annotations."
        Assert-Match $CtestRunner '::error title=' `
            "The CI CTest wrapper must emit GitHub workflow commands."
        Assert-Match $CtestRunner '--no-tests=error' `
            "The CI CTest wrapper must treat a zero-test selection as a failure."
        Assert-Match $CtestRunner 'coverage-manifest\.json' `
            "The CI CTest wrapper must read the coverage manifest."
        Assert-Match $CtestRunner 'Get-CiRuntimeFallbackMarker' `
            "The CI CTest wrapper must reject render or asset fallback diagnostics."
        Assert-Match $CtestRunner 'LastTest\.log' `
            "The CI CTest wrapper must scan CTest's LastTest.log for passing-test diagnostics."
        Assert-Match $CtestRunner 'expected_private_content_tests' `
            "The CI CTest wrapper must record the declared private-content scope in its summary."
    }
}

function New-MutatedWorkflow {
    param([string]$Source, [string]$Old, [string]$New, [string]$Name)

    if (-not $Source.Contains($Old)) {
        throw ("Mutation '{0}' no longer applies; update the fixture so the workflow contract stays covered." -f $Name)
    }
    return $Source.Replace($Old, $New)
}

function Assert-CiWorkflowRejected {
    param(
        [string]$Name,
        [string]$Workflow,
        [string]$CtestRunner,
        [string]$Pattern,
        $Manifest
    )

    $script:checks += 1
    try {
        Assert-CiWorkflowContract -Workflow $Workflow -Manifest $Manifest -CtestRunner $CtestRunner | Out-Null
    } catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw ("Mutation '{0}' was rejected for an unexpected reason: {1}" -f $Name, $_.Exception.Message)
        }
        Write-Host ("[CiWorkflowContractTest] rejected: {0}" -f $Name)
        return
    }
    throw ("Mutation '{0}' was accepted; the workflow contract does not protect it." -f $Name)
}

$workflow = (Get-Content -LiteralPath $workflowPath -Raw) -replace "`r`n", "`n"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$ctestRunner = (Get-Content -LiteralPath $ctestRunnerPath -Raw) -replace "`r`n", "`n"

Assert-CiWorkflowContract -Workflow $workflow -Manifest $manifest -CtestRunner $ctestRunner
Write-Host "[CiWorkflowContractTest] real workflow satisfies the trust, pin, scope and evidence contract"

# ---------------- The checks must reject deliberate regressions ----------------
$jobIfMatch = [regex]::Match($workflow, '(?m)^    if:[^\n]*')
Assert-True ($jobIfMatch.Success) "The fixture must be able to locate the job-level trust gate."
$jobIfLine = $jobIfMatch.Value

Assert-CiWorkflowRejected -Name "job-level trust gate removed" `
    -Workflow (New-MutatedWorkflow -Source $workflow -Old ($jobIfLine + "`n") -New "" -Name "trust gate removed") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "trust condition"
Assert-CiWorkflowRejected -Name "job-level trust gate broadened to always true" `
    -Workflow (New-MutatedWorkflow -Source $workflow -Old $jobIfLine -New '    if: ${{ true }}' -Name "trust gate true") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "trust condition"
Assert-CiWorkflowRejected -Name "same-repository requirement dropped" `
    -Workflow (New-MutatedWorkflow -Source $workflow `
        -Old "github.event.pull_request.head.repo.full_name == github.repository" -New "true" `
        -Name "same-repo requirement dropped") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "same-repository"
Assert-CiWorkflowRejected -Name "Dependabot exclusion dropped" `
    -Workflow (New-MutatedWorkflow -Source $workflow `
        -Old "github.event.pull_request.user.login != 'dependabot[bot]' && github.actor != 'dependabot[bot]'" -New "true" `
        -Name "dependabot exclusion dropped") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "Dependabot"
Assert-CiWorkflowRejected -Name "pull_request_target re-enabled" `
    -Workflow (New-MutatedWorkflow -Source $workflow -Old "  pull_request:`n" -New "  pull_request_target:`n" -Name "pull_request_target") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "pull_request"
Assert-CiWorkflowRejected -Name "action unpinned" `
    -Workflow (New-MutatedWorkflow -Source $workflow `
        -Old "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1" -New "actions/checkout@v7" `
        -Name "action unpinned") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "commit SHA"
Assert-CiWorkflowRejected -Name "checkout credentials persisted" `
    -Workflow (New-MutatedWorkflow -Source $workflow -Old "persist-credentials: false" -New "persist-credentials: true" -Name "credentials persisted") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "credentials"
Assert-CiWorkflowRejected -Name "hosted scope switched to the private content suite" `
    -Workflow (New-MutatedWorkflow -Source $workflow `
        -Old "-DPAC_ENABLE_PRIVATE_ASSET_TESTS=OFF" -New "-DPAC_ENABLE_PRIVATE_ASSET_TESTS=ON" `
        -Name "private content suite hosted") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "explicit source suite"
Assert-CiWorkflowRejected -Name "retired daily cron restored" `
    -Workflow (New-MutatedWorkflow -Source $workflow -Old "  workflow_dispatch:`n" -New "  workflow_dispatch:`n  schedule:`n    - cron: '0 8 * * *'`n" -Name "cron restored") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "cron|schedule"
Assert-CiWorkflowRejected -Name "evidence upload no longer runs on failure" `
    -Workflow (New-MutatedWorkflow -Source $workflow -Old "if: always()" -New "if: success()" -Name "evidence upload gated") `
    -CtestRunner $ctestRunner -Manifest $manifest -Pattern "even when the job fails"
Assert-CiWorkflowRejected -Name "zero-test guard dropped from the CTest wrapper" `
    -Workflow $workflow `
    -CtestRunner (New-MutatedWorkflow -Source $ctestRunner -Old "--no-tests=error" -New "--output-on-failure" -Name "no-tests guard dropped") `
    -Manifest $manifest -Pattern "zero-test"

Write-Host ("[CiWorkflowContractTest] PASS ({0} checks)" -f $checks)
