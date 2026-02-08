$ErrorActionPreference = "Stop"

$baseRef = $env:FORMAT_BASE_REF
if (-not $baseRef) { $baseRef = "origin/master" }

try {
    git fetch origin master --depth=1 | Out-Null
} catch {
    Write-Host "Warning: failed to fetch origin/master; using local refs."
}

try {
    git rev-parse --verify $baseRef | Out-Null
} catch {
    $baseRef = "HEAD~1"
}

$range = "$baseRef...HEAD"
$changed = git diff --name-only --diff-filter=ACMRTUXB $range
$files = $changed | Where-Object { $_ -match '\.(h|hpp|cpp|cc|cxx|inl)$' }

if (-not $files) {
    Write-Host "No C++ files changed; format check skipped."
    exit 0
}

$clang = Get-Command clang-format -ErrorAction SilentlyContinue
if (-not $clang) {
    Write-Error "clang-format not found. Install LLVM or add clang-format to PATH."
    exit 1
}

$failed = $false
foreach ($f in $files) {
    & $clang.Path -n --Werror -- $f
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Format issues in $f"
        $failed = $true
    }
}

if ($failed) { exit 1 }
