# Runtime content preflight for the published Phlosion cooked bundle.
#
# The checks here validate the bundle that already exists in the working tree.
# They never cook, sync, promote, or otherwise mutate content. A missing
# catalog, promotion registry, cook manifest, cooked object, cooked dependency
# or authored scene is reported as an unqualified configuration with the exact
# missing identity so the operator can restore it deliberately.
#
# Schema validation is explicit: a manifest that omits the runtime selection,
# the environment block, an object path or a dependency path is malformed and
# fails. Deep typed-object and dependency hash integrity is Forge's job
# (PhlosionForge validate), not something existence checks can claim.
#
# Windows PowerShell 5.1 compatible: no ConvertFrom-Json -AsHashtable, no
# null-coalescing operators, no Join-Path with more than two child segments.

Set-StrictMode -Version Latest

$script:CookManifestRelativePath = 'content/phlosion/cook_manifest.json'
$script:DefaultCatalogRelativePath = 'config/assets/asset_catalog.json'
$script:CookManifestSchemaVersion = 2
$script:CookManifestKinds = @('phlosion_cook_manifest', 'phlosion-cook-manifest')
$script:ObjectSections = @('pokemon', 'staged_imports', 'runtime_auxiliary_objects')

function Get-PacContentPropertyValue {
    param(
        [AllowNull()] $InputObject,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    if ($null -eq $InputObject) { return $null }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

# Returns $null for a safe repository-relative path and a short reason
# otherwise. Rooted and traversing values are rejected before any
# normalization, because trimming a leading './' off '../x' would silently
# rewrite an escape attempt into an in-repository path.
function Get-PacContentPathProblem {
    param([Parameter(Mandatory = $true)] [string] $PathValue)

    $normalized = $PathValue.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($normalized)) { return 'the value is empty' }
    if ($normalized -match '^[A-Za-z]:') { return 'the value is drive-rooted' }
    if ($normalized.StartsWith('/')) { return 'the value is rooted at the filesystem or UNC root' }
    foreach ($segment in $normalized.Split('/')) {
        if ($segment -eq '..') { return 'the value traverses to a parent directory' }
    }
    return $null
}

function ConvertTo-PacContentProjectPath {
    param([Parameter(Mandatory = $true)] [string] $PathValue)

    $normalized = $PathValue.Replace('\', '/')
    while ($normalized.StartsWith('./')) {
        $normalized = $normalized.Substring(2)
    }
    return $normalized
}

function Resolve-PacContentPath {
    param(
        [Parameter(Mandatory = $true)] [string] $Root,
        [Parameter(Mandatory = $true)] [string] $RelativePath
    )

    $problem = Get-PacContentPathProblem -PathValue $RelativePath
    if ($null -ne $problem) {
        throw ("Content path '{0}' is not a repository-relative path: {1}." -f $RelativePath, $problem)
    }
    $relative = ConvertTo-PacContentProjectPath $RelativePath
    $full = [IO.Path]::GetFullPath((Join-Path $Root $relative))
    $rootPrefix = ([IO.Path]::GetFullPath($Root)).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw ("Content path escapes the repository root: {0}" -f $RelativePath)
    }
    return $full
}

# Non-throwing variant used by the preflight: records a readable error instead
# of aborting the whole report, so the operator still sees every problem.
function Get-PacContentPathInfo {
    param(
        [Parameter(Mandatory = $true)] [string] $Root,
        [Parameter(Mandatory = $true)] [string] $RelativePath,
        [Parameter(Mandatory = $true)] [string] $Label,
        [AllowNull()] $Errors
    )

    $problem = Get-PacContentPathProblem -PathValue $RelativePath
    if ($null -ne $problem) {
        if ($null -ne $Errors) {
            $Errors.Add(("{0} '{1}' is not a repository-relative path: {2}." -f $Label, $RelativePath, $problem))
        }
        return [pscustomobject][ordered]@{ Ok = $false; Relative = $null; Full = $null }
    }

    $relative = ConvertTo-PacContentProjectPath $RelativePath
    $full = [IO.Path]::GetFullPath((Join-Path $Root $relative))
    $rootPrefix = ([IO.Path]::GetFullPath($Root)).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        if ($null -ne $Errors) {
            $Errors.Add(("{0} '{1}' resolves outside the content root." -f $Label, $RelativePath))
        }
        return [pscustomobject][ordered]@{ Ok = $false; Relative = $null; Full = $null }
    }

    return [pscustomobject][ordered]@{ Ok = $true; Relative = $relative; Full = $full }
}

function Get-PacContentRequiredField {
    param(
        [AllowNull()] $InputObject,
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [string] $Label,
        [AllowNull()] $Errors
    )

    $value = Get-PacContentPropertyValue $InputObject $Name
    $text = [string]$value
    if ($null -eq $value -or [string]::IsNullOrWhiteSpace($text)) {
        if ($null -ne $Errors) {
            $Errors.Add(("{0} does not declare {1}." -f $Label, $Name))
        }
        return $null
    }
    return $text
}

function Get-PacContentFileHash64 {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    $hash = [System.Numerics.BigInteger]::Parse('14695981039346656037')
    $prime = [System.Numerics.BigInteger]::Parse('1099511628211')
    $modulus = [System.Numerics.BigInteger]::Pow(2, 64)

    foreach ($byte in $bytes) {
        $hash = $hash -bxor [System.Numerics.BigInteger]::Parse($byte.ToString([Globalization.CultureInfo]::InvariantCulture))
        $hash = ($hash * $prime) % $modulus
    }

    # Format manually: .NET Framework's BigInteger.ToString('x') prefixes a
    # leading zero to positive values whose high nibble is 8-15, which would
    # produce a 17-digit identity. The published manifest uses plain lowercase
    # hexadecimal without a sign byte, so build the digits directly.
    $digits = New-Object 'System.Text.StringBuilder'
    $sixteen = [System.Numerics.BigInteger]::Parse('16')
    for ($index = 0; $index -lt 16; ++$index) {
        $nibble = [int](($hash % $sixteen) + 0)
        $null = $digits.Insert(0, '0123456789abcdef'[$nibble])
        $hash = [System.Numerics.BigInteger]::Divide($hash, $sixteen)
    }
    return $digits.ToString()
}

function Read-PacContentJson {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Label,
        [System.Collections.Generic.List[string]] $Errors
    )

    try {
        return (Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop)
    } catch {
        if ($null -ne $Errors) {
            $Errors.Add(("$Label is not readable JSON ({0}): {1}" -f $Path, $_.Exception.Message))
        }
        return $null
    }
}

function New-PacContentPreflightReport {
    return [pscustomobject][ordered]@{
        schema = 'pac-content-preflight-v1'
        repo_root = ''
        ok = $false
        catalog = $null
        promotion_registry = $null
        cook_manifest = $null
        counts = [ordered]@{
            objects = 0
            texture_dependencies = 0
            shared_dependencies = 0
        }
        missing = @()
        missing_source_inputs = @()
        errors = @()
    }
}

function Invoke-PacContentPreflight {
    [CmdletBinding()]
    param(
        [string] $RepoRoot = '',
        [string] $CatalogPath = '',
        [switch] $RequireSourceInputs
    )

    if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
        $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    }
    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)

    if ([string]::IsNullOrWhiteSpace($CatalogPath)) {
        $CatalogPath = $script:DefaultCatalogRelativePath
    }

    $report = New-PacContentPreflightReport
    $report.repo_root = $RepoRoot

    $missing = New-Object 'System.Collections.Generic.List[string]'
    $missingSource = New-Object 'System.Collections.Generic.List[string]'
    $errors = New-Object 'System.Collections.Generic.List[string]'

    # ---------------- Asset catalog ----------------
    $catalogInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $CatalogPath -Label 'Asset catalog path' -Errors $errors
    $catalog = $null
    if ($catalogInfo.Ok) {
        $CatalogPath = $catalogInfo.Relative
        if (-not (Test-Path -LiteralPath $catalogInfo.Full -PathType Leaf)) {
            $errors.Add("Asset catalog is missing: $CatalogPath")
        } else {
            $catalog = Read-PacContentJson -Path $catalogInfo.Full -Label 'Asset catalog' -Errors $errors
        }
    }

    if ($null -ne $catalog) {
        $catalogHash = Get-PacContentFileHash64 -Path $catalogInfo.Full
        $catalogIdentity = [ordered]@{
            path = $CatalogPath
            kind = [string](Get-PacContentPropertyValue $catalog 'kind')
            schema_version = [int](Get-PacContentPropertyValue $catalog 'schema_version')
            source_fnv1a64 = $catalogHash
        }
        $report.catalog = $catalogIdentity
        if ($catalogIdentity.kind -ne 'pokemon_autochess_asset_catalog') {
            $errors.Add("Asset catalog '$CatalogPath' declares kind '$($catalogIdentity.kind)'.")
        }
        if ($catalogIdentity.schema_version -le 0) {
            $errors.Add("Asset catalog '$CatalogPath' declares no schema_version.")
        }

        $promotionRelative = Get-PacContentRequiredField -InputObject $catalog -Name 'promotion_registry' -Label "Asset catalog '$CatalogPath'" -Errors $errors
        if ($null -ne $promotionRelative) {
            $promotionInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $promotionRelative -Label 'Promotion registry path' -Errors $errors
            if ($promotionInfo.Ok) {
                if (-not (Test-Path -LiteralPath $promotionInfo.Full -PathType Leaf)) {
                    $missing.Add($promotionInfo.Relative)
                } else {
                    $promotion = Read-PacContentJson -Path $promotionInfo.Full -Label 'Promotion registry' -Errors $errors
                    $promotionCount = 0
                    if ($null -ne $promotion) {
                        $promotionCount = @(Get-PacContentPropertyValue $promotion 'promotions').Count
                    }
                    $report.promotion_registry = [ordered]@{
                        path = $promotionInfo.Relative
                        source_fnv1a64 = (Get-PacContentFileHash64 -Path $promotionInfo.Full)
                        promotion_count = $promotionCount
                    }
                }
            }
        }

        $pokemonConfigRelative = Get-PacContentRequiredField -InputObject $catalog -Name 'pokemon_config' -Label "Asset catalog '$CatalogPath'" -Errors $errors
        if ($null -ne $pokemonConfigRelative) {
            $pokemonConfigInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $pokemonConfigRelative -Label 'Pokemon config path' -Errors $errors
            if ($pokemonConfigInfo.Ok -and -not (Test-Path -LiteralPath $pokemonConfigInfo.Full -PathType Leaf)) {
                $missing.Add($pokemonConfigInfo.Relative)
            }
        }
    }

    # ---------------- Cook manifest ----------------
    $cookInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $script:CookManifestRelativePath -Label 'Cook manifest path' -Errors $errors
    $cookManifest = $null
    if ($cookInfo.Ok) {
        if (-not (Test-Path -LiteralPath $cookInfo.Full -PathType Leaf)) {
            $errors.Add("Cook manifest is missing: $($script:CookManifestRelativePath)")
        } else {
            $cookManifest = Read-PacContentJson -Path $cookInfo.Full -Label 'Cook manifest' -Errors $errors
        }
    }

    if ($null -ne $cookManifest) {
        $cookIdentity = [ordered]@{
            path = $script:CookManifestRelativePath
            schema_version = [int](Get-PacContentPropertyValue $cookManifest 'schema_version')
            kind = [string](Get-PacContentPropertyValue $cookManifest 'kind')
            source_fnv1a64 = (Get-PacContentFileHash64 -Path $cookInfo.Full)
            object_count = 0
            shared_dependency_count = 0
        }
        if ($cookIdentity.schema_version -ne $script:CookManifestSchemaVersion) {
            $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' declares schema_version $($cookIdentity.schema_version); expected $($script:CookManifestSchemaVersion).")
        }
        if ($script:CookManifestKinds -notcontains $cookIdentity.kind) {
            $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' declares kind '$($cookIdentity.kind)'; expected one of: $($script:CookManifestKinds -join ', ').")
        }

        $catalogRecord = Get-PacContentPropertyValue $cookManifest 'asset_catalog'
        if ($null -eq $catalogRecord) {
            $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' has no asset_catalog record.")
        } else {
            $recordedSource = Get-PacContentRequiredField -InputObject $catalogRecord -Name 'source' -Label ("Cook manifest '{0}' asset_catalog" -f $script:CookManifestRelativePath) -Errors $errors
            $recordedHash = Get-PacContentRequiredField -InputObject $catalogRecord -Name 'source_fnv1a64' -Label ("Cook manifest '{0}' asset_catalog" -f $script:CookManifestRelativePath) -Errors $errors
            if ($null -ne $recordedSource) {
                $recordedSourceInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $recordedSource -Label 'Cook manifest asset_catalog source' -Errors $errors
                if ($recordedSourceInfo.Ok -and $recordedSourceInfo.Relative -ne $CatalogPath) {
                    $errors.Add("Cook manifest was produced from asset catalog '$recordedSource' but '$CatalogPath' was validated.")
                }
            }
            if ($null -ne $recordedHash -and $null -ne $report.catalog) {
                if ($recordedHash -ne $report.catalog.source_fnv1a64) {
                    $errors.Add("Asset catalog '$CatalogPath' hash $($report.catalog.source_fnv1a64) does not match the cooked bundle's recorded $recordedHash; the published content is stale.")
                }
            }
        }

        $environment = Get-PacContentPropertyValue $cookManifest 'environment'
        if ($null -eq $environment) {
            $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' has no environment block.")
        } else {
            $environmentPaths = 0
            foreach ($sceneField in @('scene', 'authored_scene')) {
                $sceneRaw = [string](Get-PacContentPropertyValue $environment $sceneField)
                if ([string]::IsNullOrWhiteSpace($sceneRaw)) { continue }
                $environmentPaths += 1
                $sceneInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $sceneRaw -Label ("Cook manifest environment.{0}" -f $sceneField) -Errors $errors
                if ($sceneInfo.Ok -and -not (Test-Path -LiteralPath $sceneInfo.Full -PathType Leaf)) {
                    $missing.Add($sceneInfo.Relative)
                }
            }
            if ($environmentPaths -eq 0) {
                $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' environment block declares neither scene nor authored_scene; the bundle names no runtime scene.")
            }
        }

        $objectCount = 0
        $textureDependencyCount = 0
        foreach ($section in $script:ObjectSections) {
            $entries = @(Get-PacContentPropertyValue $cookManifest $section)
            foreach ($entry in $entries) {
                $objectCount += 1
                $objectRaw = [string](Get-PacContentPropertyValue $entry 'object')
                if ([string]::IsNullOrWhiteSpace($objectRaw)) {
                    $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' section '$section' has an entry that declares no object path.")
                    continue
                }
                $objectInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $objectRaw -Label ("Cook manifest {0} object" -f $section) -Errors $errors
                if ($objectInfo.Ok -and -not (Test-Path -LiteralPath $objectInfo.Full -PathType Leaf)) {
                    $missing.Add($objectInfo.Relative)
                }

                foreach ($textureRaw in @(Get-PacContentPropertyValue $entry 'texture_dependencies')) {
                    $textureText = [string]$textureRaw
                    if ([string]::IsNullOrWhiteSpace($textureText)) {
                        $errors.Add("Cook manifest object '$objectRaw' declares an empty texture dependency path.")
                        continue
                    }
                    $textureDependencyCount += 1
                    $textureFull = 'content/phlosion/' + (ConvertTo-PacContentProjectPath $textureText)
                    $textureInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $textureFull -Label ("Cook manifest texture dependency for '{0}'" -f $objectRaw) -Errors $errors
                    if ($textureInfo.Ok -and -not (Test-Path -LiteralPath $textureInfo.Full -PathType Leaf)) {
                        $missing.Add($textureInfo.Relative)
                    }
                }

                $sourceRaw = [string](Get-PacContentPropertyValue $entry 'source')
                if (-not [string]::IsNullOrWhiteSpace($sourceRaw)) {
                    $sourceInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $sourceRaw -Label ("Cook manifest {0} source" -f $section) -Errors $errors
                    if ($sourceInfo.Ok -and -not (Test-Path -LiteralPath $sourceInfo.Full -PathType Leaf)) {
                        $missingSource.Add($sourceInfo.Relative)
                    }
                }
            }
        }

        if ($objectCount -le 0) {
            $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' declares no cooked runtime objects; the bundle names nothing to run.")
        }

        $sharedDependencies = @(Get-PacContentPropertyValue $cookManifest 'shared_dependencies')
        foreach ($dependency in $sharedDependencies) {
            $dependencyRaw = [string](Get-PacContentPropertyValue $dependency 'path')
            if ([string]::IsNullOrWhiteSpace($dependencyRaw)) {
                $errors.Add("Cook manifest '$($script:CookManifestRelativePath)' shared_dependencies has an entry that declares no path.")
                continue
            }
            $dependencyInfo = Get-PacContentPathInfo -Root $RepoRoot -RelativePath $dependencyRaw -Label 'Cook manifest shared dependency' -Errors $errors
            if ($dependencyInfo.Ok -and -not (Test-Path -LiteralPath $dependencyInfo.Full -PathType Leaf)) {
                $missing.Add($dependencyInfo.Relative)
            }
        }

        $cookIdentity.object_count = $objectCount
        $cookIdentity.shared_dependency_count = $sharedDependencies.Count
        $report.cook_manifest = $cookIdentity
        $report.counts.objects = $objectCount
        $report.counts.texture_dependencies = $textureDependencyCount
        $report.counts.shared_dependencies = $sharedDependencies.Count
    }

    foreach ($entry in @($missing | Sort-Object -Unique)) {
        $report.missing += [pscustomobject][ordered]@{ path = $entry; category = 'cooked-runtime' }
    }
    foreach ($entry in @($missingSource | Sort-Object -Unique)) {
        $report.missing_source_inputs += [pscustomobject][ordered]@{ path = $entry; category = 'importer-source' }
    }
    foreach ($entry in $errors) {
        $report.errors += $entry
    }

    $report.ok = ($errors.Count -eq 0 -and $missing.Count -eq 0)
    if ($RequireSourceInputs -and $missingSource.Count -gt 0) {
        $report.ok = $false
    }
    return $report
}

function Assert-PacContentPreflight {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] $Report,
        [switch] $RequireSourceInputs
    )

    $problems = New-Object 'System.Collections.Generic.List[string]'
    foreach ($entry in @(Get-PacContentPropertyValue $Report 'errors')) {
        $problems.Add(("  {0}" -f [string]$entry))
    }
    foreach ($entry in @(Get-PacContentPropertyValue $Report 'missing')) {
        $problems.Add(("  missing cooked runtime path: {0}" -f [string](Get-PacContentPropertyValue $entry 'path')))
    }
    if ($RequireSourceInputs) {
        foreach ($entry in @(Get-PacContentPropertyValue $Report 'missing_source_inputs')) {
            $problems.Add(("  missing importer source input: {0}" -f [string](Get-PacContentPropertyValue $entry 'path')))
        }
    }
    # A report that says it is not ok must never be accepted, even when it names
    # no individual problem: the report flag itself is the contract.
    if ($problems.Count -eq 0 -and (Get-PacContentPropertyValue $Report 'ok') -ne $true) {
        $problems.Add("  the preflight reported ok=false without naming a specific error or missing path.")
    }
    if ($problems.Count -gt 0) {
        throw (
            "Private content preflight failed for '{0}'. Missing or invalid content:{1}{2}" -f
                [string](Get-PacContentPropertyValue $Report 'repo_root'),
                [Environment]::NewLine,
                ($problems -join [Environment]::NewLine))
    }
}

function Get-PacContentIdentity {
    [CmdletBinding()]
    param([string] $RepoRoot = '')

    if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
        $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    }
    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)

    $report = Invoke-PacContentPreflight -RepoRoot $RepoRoot
    $gitRevision = $null
    try {
        # Capture the native exit code immediately: piping a redirected native
        # command can leave $LASTEXITCODE unset under some hosts.
        $revisionLines = @(& git -C $RepoRoot rev-parse HEAD)
        $gitExit = $LASTEXITCODE
        if ($gitExit -eq 0 -and $revisionLines.Count -gt 0) {
            $gitRevision = ([string]$revisionLines[0]).Trim()
        }
    } catch {
        $gitRevision = $null
    }

    return [pscustomobject][ordered]@{
        schema = 'pac-content-identity-v1'
        git_revision = $gitRevision
        catalog = (Get-PacContentPropertyValue $report 'catalog')
        promotion_registry = (Get-PacContentPropertyValue $report 'promotion_registry')
        cook_manifest = (Get-PacContentPropertyValue $report 'cook_manifest')
        counts = (Get-PacContentPropertyValue $report 'counts')
        ok = $report.ok
    }
}

Export-ModuleMember -Function @(
    'Get-PacContentPropertyValue',
    'Get-PacContentPathProblem',
    'Get-PacContentFileHash64',
    'Resolve-PacContentPath',
    'Invoke-PacContentPreflight',
    'Assert-PacContentPreflight',
    'Get-PacContentIdentity'
)
