param(
    [string]$OutDir = "dist/Release",
    [string[]]$Folders = @("assets", "config", "scripts"),
    [switch]$VerboseCopy
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

function Invoke-RobocopyMirror {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $args = @(
        $Source,
        $Destination,
        "/MIR",
        "/FFT",
        "/R:1",
        "/W:1"
    )

    if (-not $VerboseCopy) {
        $args += @("/NFL", "/NDL", "/NJH", "/NJS", "/NP")
    }

    & robocopy @args | Out-Null
    if ($LASTEXITCODE -gt 7) {
        throw "robocopy failed ($LASTEXITCODE): $Source -> $Destination"
    }
}

foreach ($folder in $Folders) {
    if (-not $folder -or $folder.Trim().Length -eq 0) {
        continue
    }

    $src = Join-Path $repoRoot $folder
    if (-not (Test-Path $src)) {
        Write-Warning "Skipped missing folder: $folder"
        continue
    }

    $dst = Join-Path $OutDir $folder
    Invoke-RobocopyMirror -Source $src -Destination $dst
    Write-Host "Synced $folder -> $dst"
}
