param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [int]$AutoQuitSeconds = 2
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

$gl = Invoke-BackendRun -ExePath $exePath -Backend "opengl" -AutoQuitSeconds $AutoQuitSeconds
Write-Host "[ParityCheck] OpenGL: $($gl.Line)"

$d3d12 = Invoke-BackendRun -ExePath $exePath -Backend "d3d12" -AutoQuitSeconds $AutoQuitSeconds
Write-Host "[ParityCheck] D3D12: $($d3d12.Line)"

if ($gl.Status -ne "PASS") {
    throw "OpenGL parity contract status is '$($gl.Status)'."
}
if ($d3d12.Status -ne "PASS") {
    throw "D3D12 parity contract status is '$($d3d12.Status)'."
}
if ($gl.Signature -ne $d3d12.Signature) {
    throw "Parity contract signature mismatch: opengl=$($gl.Signature) d3d12=$($d3d12.Signature)"
}

Write-Host "[ParityCheck] PASS: signatures match ($($gl.Signature))."
