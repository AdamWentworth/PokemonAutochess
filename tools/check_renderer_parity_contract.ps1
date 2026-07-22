param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [int]$AutoQuitSeconds = 2,
    [string[]]$Backends = @("opengl", "vulkan", "d3d12")
)

$ErrorActionPreference = "Stop"

function Resolve-GameExePath {
    param(
        [string]$BuildDir,
        [string]$Config
    )

    $candidateA = Join-Path $BuildDir "$Config/PokemonAutochess.exe"
    if (Test-Path $candidateA) { return (Resolve-Path $candidateA).Path }

    $candidateB = Join-Path $BuildDir "PokemonAutochess.exe"
    if (Test-Path $candidateB) { return (Resolve-Path $candidateB).Path }

    throw "PokemonAutochess.exe not found under '$BuildDir' (config '$Config')."
}

function Invoke-BackendRun {
    param(
        [string]$ExePath,
        [string]$Backend,
        [int]$AutoQuitSeconds
    )

    $oldBackend = $env:PAC_RENDER_BACKEND
    $oldAutoQuit = $env:PAC_AUTO_QUIT_SECONDS
    $oldFatal = $env:PAC_PARITY_CONTRACT_FATAL

    try {
        $env:PAC_RENDER_BACKEND = $Backend
        $env:PAC_AUTO_QUIT_SECONDS = "$AutoQuitSeconds"
        $env:PAC_PARITY_CONTRACT_FATAL = "1"

        $quotedExe = '"' + $ExePath + '"'
        $rawLines = @(cmd /c "$quotedExe 2>&1")
        $textLines = @($rawLines | ForEach-Object { [string]$_ } | Where-Object { $_ -ne "" })
        $parity = @($textLines | Where-Object { $_ -match "^\[ParityContract\]" })
        if (-not $parity -or $parity.Count -eq 0) {
            throw "No [ParityContract] line found for backend '$Backend'."
        }
        $line = $parity | Select-Object -Last 1
        if ($line -notmatch "^\[ParityContract\]\[(?<backend>[^\]]+)\] (?<status>PASS|FAIL) signature=(?<signature>[0-9a-f]+)\b") {
            throw "Could not parse parity line for backend '$Backend': $line"
        }

        return [PSCustomObject]@{
            Backend = $Backend
            ReportedBackend = $Matches["backend"]
            Status = $Matches["status"]
            Signature = $Matches["signature"]
            Line = $line
        }
    } finally {
        if ($null -ne $oldBackend) { $env:PAC_RENDER_BACKEND = $oldBackend } else { Remove-Item Env:PAC_RENDER_BACKEND -ErrorAction SilentlyContinue }
        if ($null -ne $oldAutoQuit) { $env:PAC_AUTO_QUIT_SECONDS = $oldAutoQuit } else { Remove-Item Env:PAC_AUTO_QUIT_SECONDS -ErrorAction SilentlyContinue }
        if ($null -ne $oldFatal) { $env:PAC_PARITY_CONTRACT_FATAL = $oldFatal } else { Remove-Item Env:PAC_PARITY_CONTRACT_FATAL -ErrorAction SilentlyContinue }
    }
}

$exePath = Resolve-GameExePath -BuildDir $BuildDir -Config $Config
Write-Host "[ParityCheck] EXE: $exePath"

if (-not $Backends -or $Backends.Count -eq 0) {
    throw "At least one renderer backend must be supplied."
}

$results = @()
foreach ($backend in $Backends) {
    $result = Invoke-BackendRun -ExePath $exePath -Backend $backend -AutoQuitSeconds $AutoQuitSeconds
    Write-Host "[ParityCheck] $backend`: $($result.Line)"
    $results += $result
}

foreach ($result in $results) {
    if ($result.ReportedBackend -ine $result.Backend) {
        throw "Requested backend '$($result.Backend)' reported itself as '$($result.ReportedBackend)'."
    }
    if ($result.Status -ne "PASS") {
        throw "$($result.Backend) parity contract status is '$($result.Status)'."
    }
}

$expectedSignature = $results[0].Signature
$signatureSummary = ($results | ForEach-Object { "$($_.Backend)=$($_.Signature)" }) -join " "
foreach ($result in $results | Select-Object -Skip 1) {
    if ($result.Signature -ne $expectedSignature) {
        throw "Parity contract signature mismatch: $signatureSummary"
    }
}

Write-Host "[ParityCheck] PASS: $($results.Count) backend signatures match ($expectedSignature)."
