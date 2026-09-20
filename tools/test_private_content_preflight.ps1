# Contract tests for the runtime content preflight.
#
# The fixture is a small synthetic repository that mirrors the published
# Phlosion bundle shape: an asset catalog, a promotion registry, a schema-2
# cook manifest and the cooked objects/dependencies it references. Each case
# removes or corrupts one thing and asserts the preflight names it instead of
# silently shrinking coverage.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "ci/ContentPreflight.psm1") -Force

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("pac-content-preflight-" + [Guid]::NewGuid().ToString("N"))

$checks = 0
function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks += 1
    if (-not $Condition) { throw $Message }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Pattern, [string]$Message)
    $script:checks += 1
    $threw = $false
    try {
        & $Action | Out-Null
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch $Pattern) {
            throw ("{0} (threw: {1})" -f $Message, $_.Exception.Message)
        }
    }
    if (-not $threw) { throw ("{0} (no exception was raised)" -f $Message) }
}

function Write-FixtureFile {
    param([string]$RelativePath, [string]$Text)
    $full = Join-Path $fixtureRoot $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $full) -Force | Out-Null
    Set-Content -LiteralPath $full -Value $Text -Encoding UTF8
    return $full
}

function New-FixtureRepository {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

    $null = Write-FixtureFile -RelativePath "config/pokemon_config.json" -Text '{"schema_version": 1}'
    $null = Write-FixtureFile -RelativePath "config/assets/kanto_model_promotions.json" -Text '{"schema": "kanto_model_promotions", "promotions": []}'
    $null = Write-FixtureFile -RelativePath "assets/models/0001_Bulbasaur_SV.phmodel" -Text "synthetic-source"
    $null = Write-FixtureFile -RelativePath "assets/models/0001_Bulbasaur_SV.meta.json" -Text '{"synthetic": true}'
    $null = Write-FixtureFile -RelativePath "content/phlosion/objects/0001_Bulbasaur/0001_Bulbasaur.phlo" -Text "synthetic-cooked-object"
    $null = Write-FixtureFile -RelativePath "content/phlosion/dependencies/ktx2/0001_Bulbasaur.ktx2" -Text "synthetic-texture-dependency"
    $null = Write-FixtureFile -RelativePath "content/phlosion/dependencies/ktx2/shared.ktx2" -Text "synthetic-shared-dependency"
    $null = Write-FixtureFile -RelativePath "content/phlosion/scenes/route1.phscene" -Text "synthetic-scene"
    $null = Write-FixtureFile -RelativePath "content/phlosion/scenes/route1-authored.phscene" -Text "synthetic-authored-scene"

    $catalog = [ordered]@{
        schema_version = 1
        kind = "pokemon_autochess_asset_catalog"
        pokemon_config = "config/pokemon_config.json"
        promotion_registry = "config/assets/kanto_model_promotions.json"
        explicit_native_models = @()
        environment_resources = @()
    }
    $catalogPath = Write-FixtureFile -RelativePath "config/assets/asset_catalog.json" -Text ($catalog | ConvertTo-Json -Depth 5)
    $catalogHash = Get-PacContentFileHash64 -Path $catalogPath

    $cookManifest = [ordered]@{
        schema_version = 2
        kind = "phlosion_cook_manifest"
        asset_catalog = [ordered]@{
            source = "config/assets/asset_catalog.json"
            source_fnv1a64 = $catalogHash
        }
        environment = [ordered]@{
            scene = "content/phlosion/scenes/route1.phscene"
            authored_scene = "content/phlosion/scenes/route1-authored.phscene"
        }
        pokemon = @(
            [ordered]@{
                object = "content/phlosion/objects/0001_Bulbasaur/0001_Bulbasaur.phlo"
                source = "assets/models/0001_Bulbasaur_SV.phmodel"
                texture_dependencies = @("dependencies/ktx2/0001_Bulbasaur.ktx2")
            }
        )
        staged_imports = @()
        runtime_auxiliary_objects = @()
        shared_dependencies = @(
            [ordered]@{
                asset_id = "dependencies/ktx2/shared.ktx2"
                path = "content/phlosion/dependencies/ktx2/shared.ktx2"
            }
        )
    }
    $null = Write-FixtureFile -RelativePath "content/phlosion/cook_manifest.json" -Text ($cookManifest | ConvertTo-Json -Depth 6)
    return $catalogPath
}

try {
    $catalogPath = New-FixtureRepository
    $report = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True ($report.ok) ("A complete synthetic bundle must qualify. errors: " + (@($report.errors) -join "; ") + " missing: " + (@($report.missing | ForEach-Object { $_.path }) -join "; "))
    Assert-True ($report.counts.objects -eq 1) "The preflight must count cooked objects."
    Assert-True ($report.counts.texture_dependencies -eq 1) "The preflight must count texture dependencies."
    Assert-True ($report.counts.shared_dependencies -eq 1) "The preflight must count shared dependencies."
    Assert-PacContentPreflight -Report $report -RequireSourceInputs
    Assert-True $true "A complete bundle passes the strict assertion."

    $hash = Get-PacContentFileHash64 -Path $catalogPath
    Assert-True ($hash -eq $report.catalog.source_fnv1a64) "The reported catalog hash must match a direct hash."
    Assert-True ((Get-PacContentFileHash64 -Path (Join-Path $fixtureRoot "assets/models/0001_Bulbasaur_SV.meta.json")).Length -eq 16) "Hashes must be fixed-width lowercase hexadecimal."
    Assert-True ($report.cook_manifest.source_fnv1a64.Length -eq 16) "Cook manifest identities must be fixed-width hexadecimal."

    Remove-Item -LiteralPath (Join-Path $fixtureRoot "content/phlosion/dependencies/ktx2/0001_Bulbasaur.ktx2") -Force
    $missingDependency = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $missingDependency.ok) "A missing cooked dependency must not qualify."
    $missingPaths = @($missingDependency.missing | ForEach-Object { $_.path })
    Assert-True ($missingPaths -contains "content/phlosion/dependencies/ktx2/0001_Bulbasaur.ktx2") "The preflight must name the exact missing dependent object."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $missingDependency } `
        -Pattern "0001_Bulbasaur\.ktx2" -Message "The strict assertion must surface the missing dependency identity."

    $null = New-FixtureRepository
    Remove-Item -LiteralPath (Join-Path $fixtureRoot "content/phlosion/dependencies/ktx2/shared.ktx2") -Force
    $missingShared = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $missingShared.ok) "A missing shared dependency must not qualify."
    Assert-True (@($missingShared.missing | ForEach-Object { $_.path }) -contains "content/phlosion/dependencies/ktx2/shared.ktx2") `
        "The preflight must name the missing shared dependency."

    $null = New-FixtureRepository
    Remove-Item -LiteralPath (Join-Path $fixtureRoot "content/phlosion/objects/0001_Bulbasaur/0001_Bulbasaur.phlo") -Force
    $missingObject = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $missingObject.ok) "A missing cooked object must not qualify."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $missingObject } `
        -Pattern "0001_Bulbasaur\.phlo" -Message "The strict assertion must surface the missing cooked object."

    $null = New-FixtureRepository
    Remove-Item -LiteralPath (Join-Path $fixtureRoot "content/phlosion/cook_manifest.json") -Force
    $missingManifest = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $missingManifest.ok) "A missing cook manifest must not qualify."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $missingManifest } `
        -Pattern "Cook manifest is missing" -Message "The strict assertion must report the missing cook manifest."

    $null = New-FixtureRepository
    Remove-Item -LiteralPath (Join-Path $fixtureRoot "config/assets/asset_catalog.json") -Force
    $missingCatalog = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $missingCatalog.ok) "A missing asset catalog must not qualify."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $missingCatalog } `
        -Pattern "Asset catalog is missing" -Message "The strict assertion must report the missing catalog."

    $null = New-FixtureRepository
    Remove-Item -LiteralPath (Join-Path $fixtureRoot "config/assets/kanto_model_promotions.json") -Force
    $missingPromotion = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $missingPromotion.ok) "A missing promotion registry must not qualify."
    Assert-True (@($missingPromotion.missing | ForEach-Object { $_.path }) -contains "config/assets/kanto_model_promotions.json") `
        "The preflight must name the missing promotion registry."

    $null = New-FixtureRepository
    Add-Content -LiteralPath (Join-Path $fixtureRoot "config/assets/asset_catalog.json") -Value " " -Encoding UTF8
    $stale = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $stale.ok) "A catalog that no longer matches the cooked bundle must not qualify."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $stale } `
        -Pattern "published content is stale" -Message "The strict assertion must explain the stale content identity."

    $null = New-FixtureRepository
    Remove-Item -LiteralPath (Join-Path $fixtureRoot "assets/models/0001_Bulbasaur_SV.phmodel") -Force
    $missingSource = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True ($missingSource.ok) "Runtime cooked validation must not fail solely on importer source inputs."
    Assert-True ($missingSource.missing_source_inputs.Count -eq 1) "Missing importer source inputs must still be reported."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $missingSource -RequireSourceInputs } `
        -Pattern "importer source input" -Message "Content qualification must require importer source inputs."

    $null = New-FixtureRepository
    $identity = Get-PacContentIdentity -RepoRoot $fixtureRoot
    Assert-True ($identity.ok) "Content identity must report a qualified bundle."
    Assert-True ($identity.catalog.source_fnv1a64.Length -eq 16) "Content identity must include the catalog hash."
    Assert-True ($null -ne $identity.cook_manifest.object_count) "Content identity must include the cooked object count."

    # ---------------- Malformed and empty manifests must fail ----------------
    $cookManifestPath = Join-Path $fixtureRoot "content/phlosion/cook_manifest.json"

    $null = New-FixtureRepository
    Set-Content -LiteralPath $cookManifestPath -Value "{ not json" -Encoding UTF8
    $malformed = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $malformed.ok) "An unparseable cook manifest must not qualify."
    Assert-Throws -Action { Assert-PacContentPreflight -Report $malformed } `
        -Pattern "not readable JSON" -Message "A malformed cook manifest must be named as unreadable JSON."

    # environment = {} declares no runtime scene even though the section exists.
    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $manifest.environment = [pscustomobject]@{}
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $emptyEnvironment = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $emptyEnvironment.ok) "An empty environment block must not qualify."
    Assert-True ((@($emptyEnvironment.errors) -join ' ') -match 'neither scene nor authored_scene') `
        "An empty environment block must be reported as naming no runtime scene."

    # A manifest that omits the environment key entirely.
    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $null = $manifest.PSObject.Properties.Remove('environment')
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $noEnvironment = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $noEnvironment.ok) "A cook manifest with no environment block must not qualify."
    Assert-True ((@($noEnvironment.errors) -join ' ') -match 'no environment block') `
        "The omitted environment block must be named."

    # Every object section empty: the bundle names nothing to run.
    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $manifest.pokemon = @()
    $manifest.staged_imports = @()
    $manifest.runtime_auxiliary_objects = @()
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $noObjects = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $noObjects.ok) "A bundle with no cooked runtime objects must not qualify."
    Assert-True ((@($noObjects.errors) -join ' ') -match 'no cooked runtime objects') `
        "An empty runtime selection must be reported as naming nothing to run."

    # An entry without an object path.
    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $manifest.pokemon[0].object = ""
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $emptyObjectPath = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $emptyObjectPath.ok) "A section entry without an object path must not qualify."
    Assert-True ((@($emptyObjectPath.errors) -join ' ') -match 'declares no object path') `
        "The missing object path must be named."

    # A shared dependency entry without a declared path is not silently skipped.
    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $manifest.shared_dependencies[0].path = ""
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $emptyDependencyPath = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $emptyDependencyPath.ok) "A shared dependency without a path must not qualify."
    Assert-True ((@($emptyDependencyPath.errors) -join ' ') -match 'declares no path') `
        "The dependency entry with no path must be named instead of skipped."

    # Rooted paths are rejected before any './' trimming could rewrite them.
    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $manifest.pokemon[0].object = "C:/outside/0001_Bulbasaur.phlo"
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $rooted = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $rooted.ok) "A drive-rooted object path must not qualify."
    Assert-True ((@($rooted.errors) -join ' ') -match 'drive-rooted') "A drive-rooted path must be named."

    $null = New-FixtureRepository
    $manifest = Get-Content -LiteralPath $cookManifestPath -Raw | ConvertFrom-Json
    $manifest.pokemon[0].object = "./../../outside/0001_Bulbasaur.phlo"
    ($manifest | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $cookManifestPath -Encoding UTF8
    $traversing = Invoke-PacContentPreflight -RepoRoot $fixtureRoot
    Assert-True (-not $traversing.ok) "A traversing object path must not qualify."
    Assert-True ((@($traversing.errors) -join ' ') -match 'traverses to a parent directory') `
        "A ../ path must be rejected before './' trimming could rewrite it into the repository."

    # The strict assertion trusts the report flag, not only its listed problems.
    Assert-Throws -Action {
        Assert-PacContentPreflight -Report ([pscustomobject][ordered]@{
            schema = 'pac-content-preflight-v1'
            repo_root = $fixtureRoot
            ok = $false
            errors = @()
            missing = @()
            missing_source_inputs = @()
        })
    } -Pattern "ok=false" -Message "A report that says ok=false with no listed problem must still be rejected."

    Write-Host ("[PrivateContentPreflightContractTest] PASS ({0} checks)" -f $checks)
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
