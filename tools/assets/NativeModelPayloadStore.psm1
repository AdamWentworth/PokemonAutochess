Set-StrictMode -Version Latest

$script:NativePayloadStoreRelativePath = '_payloads/sha256'
$script:Utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)

function Resolve-NativePayloadFullPath {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$RootValue,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $path = [IO.Path]::GetFullPath($PathValue)
    $root = [IO.Path]::GetFullPath($RootValue)
    $rootPrefix = $root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its allowed root: $path (root: $root)"
    }
    return $path
}

function ConvertTo-NativePayloadPortablePath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    return $PathValue.Replace('\', '/')
}

function Get-CanonicalNativePayloadRelativePath {
    param([Parameter(Mandatory = $true)][string]$Sha256)

    $hash = $Sha256.ToLowerInvariant()
    if ($hash -notmatch '^[0-9a-f]{64}$') {
        throw "Native payload SHA-256 is invalid: '$Sha256'"
    }
    return "$script:NativePayloadStoreRelativePath/$hash.bin"
}

function Test-IsCanonicalNativePayloadReference {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Sha256
    )

    $actual = (ConvertTo-NativePayloadPortablePath $RelativePath).TrimStart('./')
    $expected = Get-CanonicalNativePayloadRelativePath $Sha256
    return $actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)
}

function Get-NativeModelPayloadRecord {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$ModelsRoot,
        [switch]$SkipHashVerification
    )

    $root = [IO.Path]::GetFullPath($ModelsRoot)
    $manifest = Resolve-NativePayloadFullPath $ManifestPath $root 'Native model manifest'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Native model manifest does not exist: $manifest"
    }

    try {
        $document = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    } catch {
        throw "Could not parse native model manifest '$manifest': $($_.Exception.Message)"
    }
    if ([string]$document.schema -ne 'phlosion-native-model-ir-v1' -or
        [int]$document.schema_version -ne 1) {
        throw "Unsupported native model schema in $manifest"
    }
    if ($document.PSObject.Properties.Name -notcontains 'payload' -or $null -eq $document.payload) {
        throw "Native model manifest has no payload declaration: $manifest"
    }

    $relativePayload = [string]$document.payload.file
    if ([string]::IsNullOrWhiteSpace($relativePayload) -or
        [IO.Path]::IsPathRooted($relativePayload)) {
        throw "Native payload path must be relative in $manifest"
    }
    $payload = Resolve-NativePayloadFullPath `
        (Join-Path (Split-Path -Parent $manifest) $relativePayload) `
        $root `
        'Native model payload'
    if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) {
        throw "Native model payload does not exist: $payload"
    }

    $declaredHash = ([string]$document.payload.sha256).ToLowerInvariant()
    if ($declaredHash -notmatch '^[0-9a-f]{64}$') {
        throw "Native payload declaration has an invalid SHA-256 in $manifest"
    }
    $declaredLength = [int64]$document.payload.byte_length
    $payloadFile = Get-Item -LiteralPath $payload
    if ($declaredLength -lt 0 -or [int64]$payloadFile.Length -ne $declaredLength) {
        throw "Native payload length does not match its declaration in $manifest"
    }
    if (-not $SkipHashVerification) {
        $actualHash = (Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $declaredHash) {
            throw "Native payload SHA-256 does not match its declaration in $manifest"
        }
    }

    $canonicalRelative = Get-CanonicalNativePayloadRelativePath $declaredHash
    $canonicalPath = Resolve-NativePayloadFullPath `
        (Join-Path $root $canonicalRelative) `
        $root `
        'Content-addressed native payload'

    return [pscustomobject][ordered]@{
        manifest_path = $manifest
        manifest_sha256 = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
        payload_path = $payload
        payload_relative_path = ConvertTo-NativePayloadPortablePath $relativePayload
        payload_sha256 = $declaredHash
        payload_bytes = $declaredLength
        canonical_relative_path = $canonicalRelative
        canonical_path = $canonicalPath
        is_content_addressed = Test-IsCanonicalNativePayloadReference $relativePayload $declaredHash
        material_variant = if ($document.PSObject.Properties.Name -contains 'source' -and
            $null -ne $document.source -and
            $document.source.PSObject.Properties.Name -contains 'material_variant') {
            [string]$document.source.material_variant
        } else { '' }
        model_name = if ($document.PSObject.Properties.Name -contains 'model' -and
            $null -ne $document.model -and
            $document.model.PSObject.Properties.Name -contains 'name') {
            [string]$document.model.name
        } else { '' }
    }
}

function Set-NativeModelPayloadReferenceInText {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestText,
        [Parameter(Mandatory = $true)][string]$RelativePayloadPath
    )

    $payloadMarker = $ManifestText.IndexOf('"payload"', [StringComparison]::Ordinal)
    if ($payloadMarker -lt 0) {
        throw 'Native model manifest has no payload object.'
    }
    $filePattern = New-Object Text.RegularExpressions.Regex(
        '("file"\s*:\s*)"(?:\\.|[^"\\])*"',
        [Text.RegularExpressions.RegexOptions]::Singleline)
    $match = $filePattern.Match($ManifestText, $payloadMarker)
    if (-not $match.Success) {
        throw 'Native model payload has no file declaration.'
    }
    $portablePath = ConvertTo-NativePayloadPortablePath $RelativePayloadPath
    if ($portablePath -match '["\r\n]') {
        throw 'Native payload reference contains an unsafe character.'
    }
    return $ManifestText.Substring(0, $match.Index) +
        $match.Groups[1].Value + '"' + $portablePath + '"' +
        $ManifestText.Substring($match.Index + $match.Length)
}

function Write-NativePayloadTextAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$AllowedRoot
    )

    $destinationPath = Resolve-NativePayloadFullPath $Destination $AllowedRoot 'Published native model manifest'
    $parent = Split-Path -Parent $destinationPath
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $nonce = [Guid]::NewGuid().ToString('N')
    $partial = "$destinationPath.partial.$nonce"
    $backup = "$destinationPath.backup.$nonce"
    [IO.File]::WriteAllText($partial, $Text, $script:Utf8WithoutBom)
    try {
        if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
            Move-Item -LiteralPath $destinationPath -Destination $backup
        }
        Move-Item -LiteralPath $partial -Destination $destinationPath
        if (Test-Path -LiteralPath $backup -PathType Leaf) {
            Remove-Item -LiteralPath $backup -Force
        }
    } catch {
        if ((-not (Test-Path -LiteralPath $destinationPath)) -and
            (Test-Path -LiteralPath $backup -PathType Leaf)) {
            Move-Item -LiteralPath $backup -Destination $destinationPath
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $partial -PathType Leaf) {
            Remove-Item -LiteralPath $partial -Force
        }
    }
}

function Publish-VerifiedNativePayload {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePayload,
        [Parameter(Mandatory = $true)][string]$DestinationPayload,
        [Parameter(Mandatory = $true)][string]$DestinationRoot,
        [Parameter(Mandatory = $true)][string]$Sha256,
        [Parameter(Mandatory = $true)][int64]$ByteLength
    )

    $destination = Resolve-NativePayloadFullPath `
        $DestinationPayload `
        $DestinationRoot `
        'Content-addressed native payload'
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        $existing = Get-Item -LiteralPath $destination
        if ([int64]$existing.Length -ne $ByteLength -or
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha256) {
            throw "Content-addressed payload is corrupt and will not be overwritten: $destination"
        }
        return $false
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    $partial = "$destination.partial.$([Guid]::NewGuid().ToString('N'))"
    try {
        Copy-Item -LiteralPath $SourcePayload -Destination $partial
        $partialFile = Get-Item -LiteralPath $partial
        if ([int64]$partialFile.Length -ne $ByteLength -or
            (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha256) {
            throw "Copied native payload failed verification: $partial"
        }
        Move-Item -LiteralPath $partial -Destination $destination
    } finally {
        if (Test-Path -LiteralPath $partial -PathType Leaf) {
            Remove-Item -LiteralPath $partial -Force
        }
    }
    return $true
}

function Get-NativeModelPayloadReferences {
    param([Parameter(Mandatory = $true)][string]$ModelsRoot)

    $root = [IO.Path]::GetFullPath($ModelsRoot)
    $references = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($manifest in @(Get-ChildItem -LiteralPath $root -File -Filter '*.phmodel' | Sort-Object Name)) {
        $reader = New-Object IO.StreamReader($manifest.FullName)
        try {
            $buffer = New-Object 'char[]' 65536
            $read = $reader.ReadBlock($buffer, 0, $buffer.Length)
            $header = New-Object string($buffer, 0, $read)
        } finally {
            $reader.Dispose()
        }
        $match = [regex]::Match(
            $header,
            '"payload"\s*:\s*(\{[^{}]*\})',
            [Text.RegularExpressions.RegexOptions]::Singleline)
        if (-not $match.Success) {
            throw "Native model payload declaration is not present in the manifest header: $($manifest.FullName)"
        }
        $payload = $match.Groups[1].Value | ConvertFrom-Json
        $relativePayload = [string]$payload.file
        if ([string]::IsNullOrWhiteSpace($relativePayload) -or
            [IO.Path]::IsPathRooted($relativePayload)) {
            throw "Native payload path must be relative in $($manifest.FullName)"
        }
        $payloadPath = Resolve-NativePayloadFullPath `
            (Join-Path $manifest.DirectoryName $relativePayload) `
            $root `
            'Native model payload'
        [void]$references.Add($payloadPath)
    }
    return ,$references
}

function Remove-NativeModelPayloadIfUnreferenced {
    param(
        [Parameter(Mandatory = $true)][string]$CandidatePath,
        [Parameter(Mandatory = $true)][string]$ModelsRoot
    )

    $candidate = Resolve-NativePayloadFullPath $CandidatePath $ModelsRoot 'Legacy native payload'
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $false }
    $references = Get-NativeModelPayloadReferences $ModelsRoot
    if ($references.Contains($candidate)) { return $false }
    Remove-Item -LiteralPath $candidate -Force
    return $true
}

function Publish-NativeModelSharedPayload {
    param(
        [Parameter(Mandatory = $true)][string]$SourceManifestPath,
        [Parameter(Mandatory = $true)][string]$SourceModelsRoot,
        [Parameter(Mandatory = $true)][string]$DestinationModelsRoot,
        [Parameter(Mandatory = $true)][string]$DestinationManifestName
    )

    if ([IO.Path]::GetFileName($DestinationManifestName) -ne $DestinationManifestName -or
        [IO.Path]::GetExtension($DestinationManifestName) -ne '.phmodel') {
        throw "Destination manifest must be a .phmodel file name: $DestinationManifestName"
    }
    $source = Get-NativeModelPayloadRecord `
        -ManifestPath $SourceManifestPath `
        -ModelsRoot $SourceModelsRoot
    $destinationRoot = [IO.Path]::GetFullPath($DestinationModelsRoot)
    New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
    $destinationManifest = Join-Path $destinationRoot $DestinationManifestName

    $oldPayloadPath = $null
    if (Test-Path -LiteralPath $destinationManifest -PathType Leaf) {
        $oldRecord = Get-NativeModelPayloadRecord `
            -ManifestPath $destinationManifest `
            -ModelsRoot $destinationRoot `
            -SkipHashVerification
        $oldPayloadPath = $oldRecord.payload_path
    }

    $canonicalPath = Join-Path $destinationRoot $source.canonical_relative_path
    $createdPayload = Publish-VerifiedNativePayload `
        -SourcePayload $source.payload_path `
        -DestinationPayload $canonicalPath `
        -DestinationRoot $destinationRoot `
        -Sha256 $source.payload_sha256 `
        -ByteLength $source.payload_bytes

    $manifestText = Get-Content -LiteralPath $source.manifest_path -Raw
    $publishedText = Set-NativeModelPayloadReferenceInText `
        -ManifestText $manifestText `
        -RelativePayloadPath $source.canonical_relative_path
    $publishedDocument = $publishedText | ConvertFrom-Json
    if ([string]$publishedDocument.payload.file -ne $source.canonical_relative_path) {
        throw 'Rewritten native model payload reference failed validation.'
    }
    Write-NativePayloadTextAtomically `
        -Text $publishedText `
        -Destination $destinationManifest `
        -AllowedRoot $destinationRoot

    $removedLegacy = 0
    $legacyCandidates = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    if ($null -ne $oldPayloadPath) { [void]$legacyCandidates.Add($oldPayloadPath) }
    [void]$legacyCandidates.Add(
        (Join-Path $destinationRoot ([IO.Path]::GetFileNameWithoutExtension($DestinationManifestName) + '.bin')))
    foreach ($candidate in $legacyCandidates) {
        $portableCandidate = ConvertTo-NativePayloadPortablePath $candidate
        if ($portableCandidate.IndexOf('/_payloads/sha256/', [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            continue
        }
        if (Remove-NativeModelPayloadIfUnreferenced `
                -CandidatePath $candidate `
                -ModelsRoot $destinationRoot) {
            $removedLegacy++
        }
    }

    return [pscustomobject][ordered]@{
        manifest = $destinationManifest
        payload = $canonicalPath
        payload_sha256 = $source.payload_sha256
        payload_bytes = $source.payload_bytes
        created_payload = [bool]$createdPayload
        removed_legacy_payloads = $removedLegacy
    }
}

function Remove-UnreferencedNativeModelPayloads {
    param([Parameter(Mandatory = $true)][string]$ModelsRoot)

    $root = [IO.Path]::GetFullPath($ModelsRoot)
    $store = Join-Path $root $script:NativePayloadStoreRelativePath
    if (-not (Test-Path -LiteralPath $store -PathType Container)) {
        return [pscustomobject][ordered]@{ removed_file_count = 0; removed_bytes = [int64]0 }
    }
    $references = Get-NativeModelPayloadReferences $root
    $removedCount = 0
    $removedBytes = [int64]0
    foreach ($payload in @(Get-ChildItem -LiteralPath $store -Recurse -File -Filter '*.bin')) {
        if ($references.Contains($payload.FullName)) { continue }
        $removedBytes += [int64]$payload.Length
        Remove-Item -LiteralPath $payload.FullName -Force
        $removedCount++
    }
    return [pscustomobject][ordered]@{
        removed_file_count = $removedCount
        removed_bytes = $removedBytes
    }
}

function New-NativeModelPayloadMigrationPlan {
    param([Parameter(Mandatory = $true)][string]$ModelsRoot)

    $root = [IO.Path]::GetFullPath($ModelsRoot)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Native model root does not exist: $root"
    }
    $records = New-Object 'System.Collections.Generic.List[object]'
    $verifiedPayloads = @{}
    foreach ($manifest in @(Get-ChildItem -LiteralPath $root -File -Filter '*.phmodel' | Sort-Object Name)) {
        $record = Get-NativeModelPayloadRecord `
            -ManifestPath $manifest.FullName `
            -ModelsRoot $root `
            -SkipHashVerification
        $payloadKey = $record.payload_path.ToLowerInvariant()
        if (-not $verifiedPayloads.ContainsKey($payloadKey)) {
            $actualHash = (Get-FileHash -LiteralPath $record.payload_path -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actualHash -ne $record.payload_sha256) {
                throw "Native payload SHA-256 does not match its declaration in $($record.manifest_path)"
            }
            $verifiedPayloads[$payloadKey] = $actualHash
        } elseif ($verifiedPayloads[$payloadKey] -ne $record.payload_sha256) {
            throw "Shared native payload has conflicting declarations: $($record.payload_path)"
        }
        $records.Add($record)
    }

    $physicalPayloads = @($records | Group-Object payload_path | ForEach-Object { $_.Group[0] })
    $uniquePayloads = @($records | Group-Object payload_sha256 | ForEach-Object { $_.Group[0] })
    $currentBytes = [int64](($physicalPayloads.payload_bytes | Measure-Object -Sum).Sum)
    $targetBytes = [int64](($uniquePayloads.payload_bytes | Measure-Object -Sum).Sum)
    $store = Join-Path $root $script:NativePayloadStoreRelativePath
    $storeFiles = if (Test-Path -LiteralPath $store -PathType Container) {
        @(Get-ChildItem -LiteralPath $store -Recurse -File -Filter '*.bin')
    } else { @() }
    $references = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in $records) { [void]$references.Add($record.payload_path) }
    $orphanFiles = @($storeFiles | Where-Object { -not $references.Contains($_.FullName) })

    return [pscustomobject][ordered]@{
        schema = 'phlosion-native-payload-migration-plan-v1'
        generated_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
        models_root = $root
        manifest_count = $records.Count
        physical_payload_count = $physicalPayloads.Count
        unique_payload_count = $uniquePayloads.Count
        duplicate_group_count = @($physicalPayloads | Group-Object payload_sha256 | Where-Object Count -gt 1).Count
        shared_reference_group_count = @($records | Group-Object payload_sha256 | Where-Object Count -gt 1).Count
        content_addressed_manifest_count = @($records | Where-Object is_content_addressed).Count
        legacy_manifest_count = @($records | Where-Object { -not $_.is_content_addressed }).Count
        current_bytes = $currentBytes
        target_bytes = $targetBytes
        reclaimable_bytes = [Math]::Max([int64]0, $currentBytes - $targetBytes)
        orphan_payload_count = $orphanFiles.Count
        orphan_payload_bytes = [int64](($orphanFiles.Length | Measure-Object -Sum).Sum)
        records = @($records | ForEach-Object { $_ })
    }
}

function Invoke-NativeModelPayloadMigration {
    param(
        [Parameter(Mandatory = $true)][object]$Plan,
        [switch]$ConfirmMigration
    )

    if ([string]$Plan.schema -ne 'phlosion-native-payload-migration-plan-v1') {
        throw "Unsupported native payload migration plan: $($Plan.schema)"
    }
    if (-not $ConfirmMigration) {
        throw 'Native payload migration requires explicit -ConfirmMigration.'
    }
    $root = [IO.Path]::GetFullPath([string]$Plan.models_root)

    $verifiedPayloadSnapshots = @{}
    foreach ($record in @($Plan.records)) {
        $manifestHash = (Get-FileHash -LiteralPath $record.manifest_path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($manifestHash -ne [string]$record.manifest_sha256) {
            throw "Native payload migration plan is stale; manifest changed: $($record.manifest_path)"
        }
        $payloadKey = ([string]$record.payload_path).ToLowerInvariant()
        if (-not $verifiedPayloadSnapshots.ContainsKey($payloadKey)) {
            $payload = Get-Item -LiteralPath $record.payload_path
            $verifiedPayloadSnapshots[$payloadKey] = [pscustomobject]@{
                bytes = [int64]$payload.Length
                sha256 = (Get-FileHash -LiteralPath $record.payload_path -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        $snapshot = $verifiedPayloadSnapshots[$payloadKey]
        if ([int64]$snapshot.bytes -ne [int64]$record.payload_bytes -or
            [string]$snapshot.sha256 -ne [string]$record.payload_sha256) {
            throw "Native payload migration plan is stale; payload changed: $($record.payload_path)"
        }
    }

    $createdPayloads = 0
    foreach ($record in @($Plan.records | Group-Object payload_sha256 | ForEach-Object { $_.Group[0] })) {
        if (Publish-VerifiedNativePayload `
                -SourcePayload $record.payload_path `
                -DestinationPayload $record.canonical_path `
                -DestinationRoot $root `
                -Sha256 $record.payload_sha256 `
                -ByteLength $record.payload_bytes) {
            $createdPayloads++
        }
    }

    $rewrittenManifests = 0
    foreach ($record in @($Plan.records)) {
        if ($record.is_content_addressed) { continue }
        $manifestText = Get-Content -LiteralPath $record.manifest_path -Raw
        $rewrittenText = Set-NativeModelPayloadReferenceInText `
            -ManifestText $manifestText `
            -RelativePayloadPath $record.canonical_relative_path
        Write-NativePayloadTextAtomically `
            -Text $rewrittenText `
            -Destination $record.manifest_path `
            -AllowedRoot $root
        $rewrittenManifests++
    }

    foreach ($record in @($Plan.records)) {
        $verified = Get-NativeModelPayloadRecord `
            -ManifestPath $record.manifest_path `
            -ModelsRoot $root `
            -SkipHashVerification
        if (-not $verified.is_content_addressed -or
            $verified.payload_sha256 -ne $record.payload_sha256) {
            throw "Native payload migration postcondition failed: $($record.manifest_path)"
        }
    }

    $removedLegacyCount = 0
    $removedLegacyBytes = [int64]0
    $finalReferences = Get-NativeModelPayloadReferences $root
    foreach ($record in @($Plan.records | Group-Object payload_path | ForEach-Object { $_.Group[0] })) {
        if ($record.is_content_addressed -or
            -not (Test-Path -LiteralPath $record.payload_path -PathType Leaf)) {
            continue
        }
        if ($finalReferences.Contains([string]$record.payload_path)) {
            continue
        }
        $bytes = [int64](Get-Item -LiteralPath $record.payload_path).Length
        Remove-Item -LiteralPath $record.payload_path -Force
        $removedLegacyCount++
        $removedLegacyBytes += $bytes
    }
    $garbage = Remove-UnreferencedNativeModelPayloads -ModelsRoot $root
    $finalPlan = New-NativeModelPayloadMigrationPlan -ModelsRoot $root

    return [pscustomobject][ordered]@{
        schema = 'phlosion-native-payload-migration-result-v1'
        models_root = $root
        created_payload_count = $createdPayloads
        rewritten_manifest_count = $rewrittenManifests
        removed_legacy_payload_count = $removedLegacyCount
        removed_legacy_payload_bytes = $removedLegacyBytes
        removed_orphan_payload_count = $garbage.removed_file_count
        removed_orphan_payload_bytes = $garbage.removed_bytes
        final_manifest_count = $finalPlan.manifest_count
        final_payload_count = $finalPlan.physical_payload_count
        final_legacy_manifest_count = $finalPlan.legacy_manifest_count
        final_reclaimable_bytes = $finalPlan.reclaimable_bytes
    }
}

Export-ModuleMember -Function `
    Get-CanonicalNativePayloadRelativePath, `
    Get-NativeModelPayloadRecord, `
    Publish-NativeModelSharedPayload, `
    Remove-UnreferencedNativeModelPayloads, `
    New-NativeModelPayloadMigrationPlan, `
    Invoke-NativeModelPayloadMigration
