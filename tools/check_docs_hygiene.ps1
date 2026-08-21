param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$docsRoot = Join-Path $repoRoot "docs"
$archiveRoot = Join-Path $docsRoot "archive"
$readmePath = Join-Path $docsRoot "README.md"
$validDocTypes = @(
    "Assessment",
    "Architecture",
    "Contract",
    "Goal",
    "Index",
    "Journal",
    "Reference",
    "Roadmap",
    "Rule",
    "Runbook",
    "Tracker"
)
$validDocTypePattern = ($validDocTypes | ForEach-Object { [regex]::Escape($_) }) -join "|"

if (-not (Test-Path $docsRoot)) {
    throw "Docs root not found: $docsRoot"
}

$errors = New-Object System.Collections.Generic.List[string]

$activeDocs = Get-ChildItem $docsRoot -File -Filter *.md | Sort-Object Name
foreach ($doc in $activeDocs) {
    $content = Get-Content $doc.FullName -Raw
    if ($content -notmatch '(?m)^Status:\s+Active\s*$') {
        $errors.Add("Missing or invalid Status header in docs/$($doc.Name)")
    }
    if ($content -notmatch "(?m)^Type:\s+($validDocTypePattern)\s*$") {
        $errors.Add("Missing or invalid Type header in docs/$($doc.Name)")
    }
    if ($content -notmatch '(?m)^Last updated:\s+\d{4}-\d{2}-\d{2}\s*$') {
        $errors.Add("Missing or invalid Last updated header in docs/$($doc.Name)")
    }
}

if (-not (Test-Path $readmePath)) {
    $errors.Add("Missing docs/README.md")
} else {
    $readme = Get-Content $readmePath -Raw
    $docRefs = [regex]::Matches($readme, '`([A-Za-z0-9_./-]+\.md)`')
    foreach ($match in $docRefs) {
        $ref = $match.Groups[1].Value
        if ($ref -like "archive/*") {
            $errors.Add("docs/README.md references archived doc as active: $ref")
            continue
        }
        $candidate = Join-Path $docsRoot $ref
        if (-not (Test-Path $candidate)) {
            $errors.Add("docs/README.md references missing doc: $ref")
        }
    }
}

$pathPrefixes = @(
    "src/",
    "tests/",
    "tools/",
    "docs/",
    "assets/",
    "content/",
    "config/",
    "scripts/",
    "benchmark/",
    "CMakeLists.txt",
    "README.md",
    "vcpkg.json"
)
$runtimeGeneratedRepoPaths = @(
    "config/user/video_settings.json",
    "config/user/debug_state_snapshot.json"
)

$resolvedDocsBuildDir = if ([IO.Path]::IsPathRooted($BuildDir)) {
    [IO.Path]::GetFullPath($BuildDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}

function Get-CMakeSourceRoot {
    param([string]$Name)

    $cachePath = Join-Path $resolvedDocsBuildDir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        return $null
    }
    $match = Select-String `
        -LiteralPath $cachePath `
        -Pattern "^$([regex]::Escape($Name)):[^=]+=(.*)$" |
        Select-Object -First 1
    if (-not $match) {
        return $null
    }
    $path = $match.Matches[0].Groups[1].Value
    if ([string]::IsNullOrWhiteSpace($path)) {
        return $null
    }
    return [IO.Path]::GetFullPath($path)
}

$engineRoot = Get-CMakeSourceRoot "PHLOSION_ENGINE_SOURCE_DIR"
$vfxRoot = Get-CMakeSourceRoot "PHLOSION_VFX_SOURCE_DIR"
if (-not $engineRoot) {
    $engineCandidates = @(
        (Join-Path $resolvedDocsBuildDir "_deps/phlosionengine-src"),
        (Join-Path $repoRoot "../../Phlosion/PhlosionEngine")
    )
    foreach ($engineCandidate in $engineCandidates) {
        if (Test-Path -LiteralPath $engineCandidate -PathType Container) {
            $engineRoot = [IO.Path]::GetFullPath($engineCandidate)
            break
        }
    }
}
if (-not $vfxRoot) {
    $vfxCandidates = @(
        (Join-Path $resolvedDocsBuildDir "_deps/phlosionvfx-src"),
        (Join-Path $repoRoot "../../Phlosion/PhlosionVFX")
    )
    foreach ($vfxCandidate in $vfxCandidates) {
        if (Test-Path -LiteralPath $vfxCandidate -PathType Container) {
            $vfxRoot = [IO.Path]::GetFullPath($vfxCandidate)
            break
        }
    }
}

function Test-IsUnavailablePrivatePath {
    param([string]$Token)

    if ($Token.StartsWith("assets/", [System.StringComparison]::Ordinal) -and
        -not (Test-Path -LiteralPath (Join-Path $repoRoot "assets") -PathType Container)) {
        return $true
    }
    if ($Token.StartsWith("content/phlosion/", [System.StringComparison]::Ordinal) -and
        -not (Test-Path -LiteralPath (Join-Path $repoRoot "content/phlosion") -PathType Container)) {
        return $true
    }
    return $false
}

function Test-DocumentedRepoPath {
    param([string]$Token)

    if (Test-Path (Join-Path $repoRoot $Token)) {
        return $true
    }
    if ($Token.StartsWith("src/engine/", [System.StringComparison]::Ordinal) -and
        $engineRoot -and
        (Test-Path (Join-Path $engineRoot $Token))) {
        return $true
    }
    if ($Token.StartsWith("src/vfx/", [System.StringComparison]::Ordinal) -and
        $vfxRoot -and
        (Test-Path (Join-Path $vfxRoot $Token))) {
        return $true
    }
    return $false
}

foreach ($doc in $activeDocs) {
    $content = Get-Content $doc.FullName -Raw
    $matches = [regex]::Matches($content, '`([^`\r\n]+)`')
    foreach ($match in $matches) {
        $token = $match.Groups[1].Value
        if ($token -match '\s') {
            continue
        }

        $isRepoPath = $false
        foreach ($prefix in $pathPrefixes) {
            if ($token.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
                $isRepoPath = $true
                break
            }
        }
        if (-not $isRepoPath) {
            continue
        }
        if (Test-IsUnavailablePrivatePath $token) {
            continue
        }

        if (-not (Test-DocumentedRepoPath $token) -and
            $runtimeGeneratedRepoPaths -notcontains $token) {
            $errors.Add("Broken repo path in docs/$($doc.Name): $token")
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host "Docs hygiene OK."
