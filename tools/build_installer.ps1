param(
    [string]$IssPath = "tools/PokemonAutochessInstaller.iss",
    [switch]$Bundle,
    [string]$ISCCPath
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

if ($Bundle) {
    & "$PSScriptRoot\\release_bundle.ps1"
}

if (-not $ISCCPath) {
    $candidates = @(
        "$env:ProgramFiles(x86)\\Inno Setup 6\\ISCC.exe",
        "$env:ProgramFiles\\Inno Setup 6\\ISCC.exe",
        "$env:LOCALAPPDATA\\Programs\\Inno Setup 6\\ISCC.exe"
    )
    $ISCCPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $ISCCPath -or -not (Test-Path $ISCCPath)) {
    Write-Error "ISCC.exe not found. Install Inno Setup 6 or pass -ISCCPath."
    exit 1
}

if (-not (Test-Path $IssPath)) {
    Write-Error "Installer script not found: $IssPath"
    exit 1
}

& $ISCCPath $IssPath
