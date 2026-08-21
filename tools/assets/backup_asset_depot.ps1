[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$DepotRoot = '',
    [string]$DestinationRoot = '\\TNAS-98B9\pokemon\3D Models\PhlosionAssets\PokemonAutochess\Snapshots\Kanto-151',
    [string]$SnapshotName = '',
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:progressPath = ''
$script:progressLogPath = ''

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Assert-PathUnderRoot(
    [string]$Candidate,
    [string]$Root,
    [string]$Description
) {
    $candidateFull = Resolve-FullPath $Candidate
    $rootFull = Resolve-FullPath $Root
    $rootPrefix = $rootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $candidateFull.StartsWith(
            $rootPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its allowed root: $candidateFull"
    }
    return $candidateFull
}

function Get-Sha256([string]$PathValue) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::Open(
        $PathValue,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $bytes = $algorithm.ComputeHash($stream)
        return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

function Get-GitState([string]$Repository) {
    if (-not (Test-Path -LiteralPath (Join-Path $Repository '.git'))) {
        return $null
    }
    $commit = (& git -C $Repository rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read Git commit for $Repository"
    }
    $branch = (& git -C $Repository branch --show-current 2>$null)
    $changes = @(& git -C $Repository status --short 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read Git status for $Repository"
    }
    return [ordered]@{
        path = Resolve-FullPath $Repository
        commit = ([string]$commit).Trim()
        branch = ([string]$branch).Trim()
        clean = $changes.Count -eq 0
        changed_paths = $changes
    }
}

function Add-FileRecord(
    [Collections.Generic.List[object]]$Records,
    [Collections.Generic.HashSet[string]]$RelativePaths,
    [string]$Source,
    [string]$RelativePath,
    [string]$Group,
    [IO.FileInfo]$FileInfo = $null
) {
    if ($null -eq $FileInfo) {
        $sourceFull = Resolve-FullPath $Source
        if (-not (Test-Path -LiteralPath $sourceFull -PathType Leaf)) {
            throw "Backup source file is missing: $sourceFull"
        }
        $file = Get-Item -LiteralPath $sourceFull -Force
    } else {
        $file = $FileInfo
        $sourceFull = $file.FullName
    }
    $normalizedRelative = $RelativePath.Replace('\', '/').TrimStart('/')
    if ([string]::IsNullOrWhiteSpace($normalizedRelative) -or
        $normalizedRelative.Contains("`t") -or
        $normalizedRelative.Contains("`r") -or
        $normalizedRelative.Contains("`n") -or
        -not $RelativePaths.Add($normalizedRelative)) {
        throw "Backup relative path is empty, unsafe, or duplicated: $normalizedRelative"
    }
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Backup source cannot be a reparse point: $sourceFull"
    }
    $Records.Add([pscustomobject]@{
        source = $sourceFull
        relative = $normalizedRelative
        group = $Group
        bytes = [long]$file.Length
        sha256 = ''
    })
}

function Write-Milestones([double]$Percent, [string]$Stage) {
    while ($script:milestoneIndex -lt $script:milestones.Count -and
        $Percent -ge $script:milestones[$script:milestoneIndex]) {
        $value = $script:milestones[$script:milestoneIndex]
        $line = "BACKUP_PROGRESS|$value|$Stage|$([DateTime]::UtcNow.ToString('o'))"
        Write-Output $line
        [Console]::Out.Flush()
        if (-not [string]::IsNullOrWhiteSpace($script:progressPath)) {
            [IO.File]::WriteAllText(
                $script:progressPath,
                $line + [Environment]::NewLine,
                [Text.UTF8Encoding]::new($false))
        }
        if (-not [string]::IsNullOrWhiteSpace($script:progressLogPath)) {
            [IO.File]::AppendAllText(
                $script:progressLogPath,
                $line + [Environment]::NewLine,
                [Text.UTF8Encoding]::new($false))
        }
        $script:milestoneIndex++
    }
}

if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $PSScriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($DepotRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:PHLOSION_ASSET_DEPOT)) {
        $DepotRoot = $env:PHLOSION_ASSET_DEPOT
    } else {
        $DepotRoot = 'D:\ProjectData\Games\PokemonAutochess\Assets'
    }
}
$DepotRoot = Resolve-FullPath $DepotRoot
$DestinationRoot = Resolve-FullPath $DestinationRoot

if (-not (Test-Path -LiteralPath $GameRoot -PathType Container)) {
    throw "Game root does not exist: $GameRoot"
}
if (-not (Test-Path -LiteralPath $DepotRoot -PathType Container)) {
    throw "Private asset depot does not exist: $DepotRoot"
}
if ($DestinationRoot.StartsWith(
        $DepotRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    $DepotRoot.StartsWith(
        $DestinationRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Backup source and destination must not contain one another.'
}

$gameGit = Get-GitState $GameRoot
if ([string]::IsNullOrWhiteSpace($SnapshotName)) {
    $SnapshotName = "$(Get-Date -Format 'yyyy-MM-dd')_$($gameGit.commit.Substring(0, 8))"
}
if ($SnapshotName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "Snapshot name contains unsupported characters: $SnapshotName"
}
$snapshotPath = Assert-PathUnderRoot (
    Join-Path $DestinationRoot $SnapshotName) $DestinationRoot 'Snapshot path'
if (Test-Path -LiteralPath $snapshotPath) {
    throw "Snapshot already exists and will not be overwritten: $snapshotPath"
}

$sections = @('source', 'derived', 'runtime', 'evidence', 'legacy')
$records = New-Object 'Collections.Generic.List[object]'
$relativePaths = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
$depotProjectRoot = Join-Path $DepotRoot 'pokemon-autochess'
foreach ($section in $sections) {
    $sectionRoot = Join-Path $depotProjectRoot $section
    if (-not (Test-Path -LiteralPath $sectionRoot -PathType Container)) {
        throw "Authoritative depot section is missing: $sectionRoot"
    }
    $sectionFull = Resolve-FullPath $sectionRoot
    $sectionPrefix = $sectionFull + [IO.Path]::DirectorySeparatorChar
    foreach ($file in Get-ChildItem -LiteralPath $sectionRoot -Recurse -File -Force) {
        $fileFull = $file.FullName
        if (-not $fileFull.StartsWith(
                $sectionPrefix,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Depot source file escapes its section root: $fileFull"
        }
        $relativeWithinSection = $fileFull.Substring($sectionPrefix.Length)
        $relativePath = (
            "depot/pokemon-autochess/$section/$relativeWithinSection"
        ).Replace('\', '/')
        if ($relativePath.Contains("`t") -or
            $relativePath.Contains("`r") -or
            $relativePath.Contains("`n") -or
            -not $relativePaths.Add($relativePath)) {
            throw "Backup relative path is unsafe or duplicated: $relativePath"
        }
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Backup source cannot be a reparse point: $fileFull"
        }
        $records.Add([pscustomobject]@{
            source = $fileFull
            relative = $relativePath
            group = "depot:$section"
            bytes = [long]$file.Length
            sha256 = ''
        })
    }
}

$depotReadme = Join-Path $DepotRoot 'README.md'
if (Test-Path -LiteralPath $depotReadme -PathType Leaf) {
    Add-FileRecord $records $relativePaths $depotReadme 'depot/README.md' 'depot:root'
}

$controlPlaneFiles = @(
    'README.md',
    'phlosion.project.json',
    'config/assets/asset_catalog.json',
    'config/assets/kanto_native_model_package.json',
    'config/assets/kanto_model_promotions.json',
    'content/phlosion/cook_manifest.json',
    'docs/PHLOSION_ASSET_MIGRATION.md'
)
foreach ($relativePath in $controlPlaneFiles) {
    Add-FileRecord `
        $records `
        $relativePaths `
        (Join-Path $GameRoot $relativePath) `
        "control-plane/$relativePath" `
        'control-plane'
}

$payloadBytes = [long](($records | Measure-Object -Property bytes -Sum).Sum)
$payloadCount = $records.Count
$temporaryParent = Resolve-FullPath ([IO.Path]::GetTempPath())
$temporaryRoot = Join-Path $temporaryParent (
    'pokemonautochess-asset-backup-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporaryRoot)

try {
    $engineRoot = 'D:\Projects\Phlosion\PhlosionEngine'
    $vfxRoot = 'D:\Projects\Phlosion\PhlosionVFX'
    $snapshotMetadata = [ordered]@{
        schema = 'pokemon-autochess-asset-snapshot-v1'
        created_utc = [DateTime]::UtcNow.ToString('o')
        snapshot_name = $SnapshotName
        qualification = 'kanto-151-accepted-for-vertical-slice'
        source_depot = $DepotRoot
        authoritative_sections = $sections
        excluded_transient_sections = @('artifacts', 'debug', 'scratch')
        payload_file_count = $payloadCount
        payload_bytes = $payloadBytes
        game = $gameGit
        engine = Get-GitState $engineRoot
        vfx = Get-GitState $vfxRoot
        promotion_registry = 'control-plane/config/assets/kanto_model_promotions.json'
        inventory = 'metadata/inventory.sha256.tsv'
    }
    $metadataSource = Join-Path $temporaryRoot 'snapshot.json'
    [IO.File]::WriteAllText(
        $metadataSource,
        ($snapshotMetadata | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    Add-FileRecord `
        $records `
        $relativePaths `
        $metadataSource `
        'metadata/snapshot.json' `
        'metadata'

    $totalBytes = [long](($records | Measure-Object -Property bytes -Sum).Sum)
    $totalGiB = [math]::Round($totalBytes / 1GB, 2)
    Write-Output "Backup plan: $($records.Count) files, $totalGiB GiB"
    Write-Output "Source: $DepotRoot"
    Write-Output "Destination: $snapshotPath"
    Write-Output 'Excluded transient depot sections: artifacts, debug, scratch'
    if (-not $Apply.IsPresent) {
        Write-Output 'Plan only. Re-run with -Apply to hash, copy, verify, and publish.'
        return
    }

    [void][IO.Directory]::CreateDirectory($DestinationRoot)
    $partialPath = Assert-PathUnderRoot (
        "$snapshotPath.partial.$([Guid]::NewGuid().ToString('N'))") `
        $DestinationRoot `
        'Partial snapshot path'
    [void][IO.Directory]::CreateDirectory($partialPath)
    [IO.File]::WriteAllText(
        (Join-Path $partialPath 'INCOMPLETE'),
        "Snapshot publication is incomplete.$([Environment]::NewLine)",
        [Text.UTF8Encoding]::new($false))
    [void][IO.Directory]::CreateDirectory((Join-Path $partialPath 'metadata'))
    $script:progressPath = Join-Path $partialPath 'BACKUP_PROGRESS'
    $script:progressLogPath = Join-Path $partialPath 'metadata\progress.log'

    $script:milestones = @(1) + @(5..100 | Where-Object { $_ % 5 -eq 0 })
    $script:milestoneIndex = 0
    $hashedBytes = [long]0
    foreach ($record in $records) {
        $record.sha256 = Get-Sha256 $record.source
        $hashedBytes += [long]$record.bytes
        $fraction = if ($totalBytes -gt 0) { $hashedBytes / $totalBytes } else { 1.0 }
        Write-Milestones ([math]::Min(25.0, $fraction * 25.0)) 'source hashing'
    }

    $inventorySource = Join-Path $temporaryRoot 'inventory.sha256.tsv'
    $writer = [IO.StreamWriter]::new(
        $inventorySource,
        $false,
        [Text.UTF8Encoding]::new($false))
    try {
        $writer.WriteLine("sha256`tbytes`trelative_path")
        foreach ($record in $records | Sort-Object relative) {
            $writer.WriteLine("$($record.sha256)`t$($record.bytes)`t$($record.relative)")
        }
    } finally {
        $writer.Dispose()
    }

    $copiedBytes = [long]0
    foreach ($section in $sections) {
        $sourceSection = Join-Path $depotProjectRoot $section
        $destinationSection = Join-Path $partialPath "depot\pokemon-autochess\$section"
        [void][IO.Directory]::CreateDirectory($destinationSection)
        $sectionBytes = [long](($records | Where-Object {
            $_.group -eq "depot:$section"
        } | Measure-Object -Property bytes -Sum).Sum)
        $sectionStartBytes = $copiedBytes
        & robocopy.exe `
            $sourceSection `
            $destinationSection `
            /E /COPY:DAT /DCOPY:DAT /R:2 /W:2 /MT:16 /XJ `
            /BYTES /FP /NJH /NJS /NDL /NP 2>&1 |
            ForEach-Object {
                $line = [string]$_
                if ($line -match '^\s*(?:New File|Newer|Older|Changed)\s+(\d+)\s+') {
                    $copiedBytes += [long]$Matches[1]
                    $fraction = if ($totalBytes -gt 0) { $copiedBytes / $totalBytes } else { 1.0 }
                    Write-Milestones (
                        [math]::Min(75.0, 25.0 + ($fraction * 50.0))) `
                        "copying $section"
                }
            }
        $robocopyExit = $LASTEXITCODE
        if ($robocopyExit -ge 8) {
            throw "Robocopy failed for depot section '$section' with exit code $robocopyExit."
        }
        $copiedBytes = $sectionStartBytes + $sectionBytes
        $fraction = if ($totalBytes -gt 0) { $copiedBytes / $totalBytes } else { 1.0 }
        Write-Milestones ([math]::Min(75.0, 25.0 + ($fraction * 50.0))) "copied $section"
    }

    foreach ($record in $records | Where-Object { $_.group -notlike 'depot:*' }) {
        $destination = Assert-PathUnderRoot (
            (Join-Path $partialPath $record.relative)) `
            $partialPath `
            'Backup file destination'
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::Copy($record.source, $destination, $false)
        $copiedBytes += [long]$record.bytes
        $fraction = if ($totalBytes -gt 0) { $copiedBytes / $totalBytes } else { 1.0 }
        Write-Milestones ([math]::Min(75.0, 25.0 + ($fraction * 50.0))) 'copying control plane'
    }
    $rootReadmeRecord = @($records | Where-Object { $_.group -eq 'depot:root' })
    foreach ($record in $rootReadmeRecord) {
        $destination = Assert-PathUnderRoot (
            (Join-Path $partialPath $record.relative)) `
            $partialPath `
            'Backup file destination'
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::Copy($record.source, $destination, $false)
        $copiedBytes += [long]$record.bytes
    }
    Write-Milestones 75.0 'copy complete'

    $inventoryDestination = Join-Path $partialPath 'metadata\inventory.sha256.tsv'
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($inventoryDestination))
    [IO.File]::Copy($inventorySource, $inventoryDestination, $false)

    $verifiedBytes = [long]0
    foreach ($record in $records) {
        $destination = Assert-PathUnderRoot (
            (Join-Path $partialPath $record.relative)) `
            $partialPath `
            'Verification path'
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            throw "Backup verification found a missing file: $($record.relative)"
        }
        $destinationFile = Get-Item -LiteralPath $destination -Force
        if ([long]$destinationFile.Length -ne [long]$record.bytes) {
            throw "Backup verification found a size mismatch: $($record.relative)"
        }
        $destinationHash = Get-Sha256 $destination
        if ($destinationHash -cne $record.sha256) {
            throw "Backup verification found a SHA-256 mismatch: $($record.relative)"
        }
        $verifiedBytes += [long]$record.bytes
        $fraction = if ($totalBytes -gt 0) { $verifiedBytes / $totalBytes } else { 1.0 }
        Write-Milestones ([math]::Min(99.0, 75.0 + ($fraction * 24.0))) 'destination verification'
    }

    $verification = [ordered]@{
        schema = 'pokemon-autochess-asset-snapshot-verification-v1'
        verified_utc = [DateTime]::UtcNow.ToString('o')
        file_count = $records.Count
        payload_bytes = $totalBytes
        algorithm = 'SHA-256'
        result = 'passed'
    }
    [IO.File]::WriteAllText(
        (Join-Path $partialPath 'metadata\verification.json'),
        ($verification | ConvertTo-Json -Depth 4) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    Remove-Item -LiteralPath (Join-Path $partialPath 'INCOMPLETE') -Force

    if (Test-Path -LiteralPath $snapshotPath) {
        throw "Final snapshot appeared during publication and will not be overwritten: $snapshotPath"
    }
    Move-Item -LiteralPath $partialPath -Destination $snapshotPath
    $script:progressPath = Join-Path $snapshotPath 'BACKUP_PROGRESS'
    $script:progressLogPath = Join-Path $snapshotPath 'metadata\progress.log'
    Write-Milestones 100.0 'published and verified'
    Write-Output "Backup complete: $snapshotPath"
} finally {
    $temporaryFull = Resolve-FullPath $temporaryRoot
    $temporaryPrefix = $temporaryParent + [IO.Path]::DirectorySeparatorChar
    if ((Test-Path -LiteralPath $temporaryFull) -and
        $temporaryFull.StartsWith(
            $temporaryPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $temporaryFull -Recurse -Force
    }
}
