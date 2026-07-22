param(
    [string]$ClangFormatPath = $env:PAC_CLANG_FORMAT
)

$ErrorActionPreference = "Stop"

function Resolve-ClangFormatPath {
    param([string]$PreferredPath)

    if ($PreferredPath) {
        $preferred = Get-Command $PreferredPath -ErrorAction SilentlyContinue
        if ($preferred) { return $preferred.Path }
        throw "Configured clang-format was not found: $PreferredPath"
    }

    $onPath = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Path }

    $candidates = [System.Collections.Generic.List[string]]::new()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installations = @(& $vswhere -all -products * -property installationPath)
        foreach ($installation in $installations) {
            if (-not $installation) { continue }
            $candidates.Add((Join-Path $installation "VC/Tools/Llvm/x64/bin/clang-format.exe"))
            $candidates.Add((Join-Path $installation "VC/Tools/Llvm/bin/clang-format.exe"))
        }
    }

    $visualStudioRoots = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio")
    )
    foreach ($root in $visualStudioRoots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($version in @("2022", "18")) {
            foreach ($edition in @("BuildTools", "Community", "Professional", "Enterprise")) {
                $candidates.Add((Join-Path $root "$version/$edition/VC/Tools/Llvm/x64/bin/clang-format.exe"))
                $candidates.Add((Join-Path $root "$version/$edition/VC/Tools/Llvm/bin/clang-format.exe"))
            }
        }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "clang-format not found. Install LLVM, add clang-format to PATH, or set PAC_CLANG_FORMAT."
}

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

$clangFormat = Resolve-ClangFormatPath -PreferredPath $ClangFormatPath
Write-Host "Using clang-format: $clangFormat"

$failed = $false
foreach ($f in $files) {
    & $clangFormat -n --Werror -- $f
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Format issues in $f"
        $failed = $true
    }
}

if ($failed) { exit 1 }
