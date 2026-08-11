param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'HousekeepingCleanup.psm1') -Force

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

function Write-FixtureFile {
    param(
        [string]$PathValue,
        [int]$Bytes
    )
    New-Item -ItemType Directory -Path (Split-Path -Parent $PathValue) -Force | Out-Null
    [IO.File]::WriteAllBytes($PathValue, [byte[]]::new($Bytes))
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-cleanup-test-' + [Guid]::NewGuid().ToString('N'))
$gameRoot = Join-Path $tempRoot 'game'
$engineRoot = Join-Path $tempRoot 'engine'
New-Item -ItemType Directory -Path $gameRoot, $engineRoot | Out-Null

try {
    Write-FixtureFile (Join-Path $gameRoot 'build-vs2022\old.bin') 4
    Write-FixtureFile (Join-Path $gameRoot 'cache\models\cache.bin') 5
    Write-FixtureFile (Join-Path $engineRoot 'cache\engine.bin') 6
    Write-FixtureFile (Join-Path $gameRoot 'build\active.bin') 7
    Write-FixtureFile (Join-Path $gameRoot 'assets\authoritative.bin') 8
    Write-FixtureFile (Join-Path $gameRoot 'artifacts\review.bin') 9

    $plan = New-WorkspaceCleanupPlan `
        -GameRoot $gameRoot `
        -EngineRoot $engineRoot
    Assert-WorkspaceCleanupPlan $plan
    Assert-Condition ($plan.target_count -eq 9) 'All-regenerable plan should contain the fixed nine-target allowlist.'
    Assert-Condition ($plan.existing_target_count -eq 3) 'Plan should find exactly three fixture targets.'
    Assert-Condition ($plan.reclaimable_bytes -eq 15) 'Plan byte total should include only regenerable fixtures.'
    Assert-Condition (-not (@($plan.targets.target) -contains (Join-Path $gameRoot 'build'))) 'Active build directory must not be a cleanup target.'
    Assert-Condition (-not (@($plan.targets.target) -contains (Join-Path $gameRoot 'assets'))) 'Authoritative assets must not be a cleanup target.'
    Assert-Condition (-not (@($plan.targets.target) -contains (Join-Path $gameRoot 'artifacts'))) 'Review artifacts must not be a cleanup target.'

    $confirmationRejected = $false
    try {
        Invoke-WorkspaceCleanupPlan -Plan $plan | Out-Null
    } catch {
        $confirmationRejected = $_.Exception.Message -like '*ConfirmDeletion*'
    }
    Assert-Condition $confirmationRejected 'Cleanup should reject deletion without explicit confirmation.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $gameRoot 'cache\models\cache.bin')) 'Rejected cleanup must preserve target files.'

    Write-FixtureFile (Join-Path $gameRoot 'cache\models\late.bin') 1
    $staleRejected = $false
    try {
        Invoke-WorkspaceCleanupPlan -Plan $plan -ConfirmDeletion -TestFixture | Out-Null
    } catch {
        $staleRejected = $_.Exception.Message -like '*plan is stale*'
    }
    Assert-Condition $staleRejected 'Cleanup should reject a plan after a target changes.'

    $freshPlan = New-WorkspaceCleanupPlan `
        -GameRoot $gameRoot `
        -EngineRoot $engineRoot
    $result = Invoke-WorkspaceCleanupPlan `
        -Plan $freshPlan `
        -ConfirmDeletion `
        -TestFixture
    Assert-Condition ($result.removed_target_count -eq 3) 'Confirmed cleanup should remove all existing allowlisted fixture targets.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'build-vs2022'))) 'Historical build fixture was not removed.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'cache'))) 'Game cache fixture was not removed.'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $engineRoot 'cache'))) 'Engine cache fixture was not removed.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $gameRoot 'build\active.bin')) 'Active build fixture must survive cleanup.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $gameRoot 'assets\authoritative.bin')) 'Authoritative asset fixture must survive cleanup.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $gameRoot 'artifacts\review.bin')) 'Review artifact fixture must survive cleanup.'

    Write-Host '[HousekeepingCleanupTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
