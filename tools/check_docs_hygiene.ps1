param()

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

        $candidate = Join-Path $repoRoot $token
        if (-not (Test-Path $candidate) -and $runtimeGeneratedRepoPaths -notcontains $token) {
            $errors.Add("Broken repo path in docs/$($doc.Name): $token")
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Docs hygiene OK."
