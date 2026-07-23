param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [int]$AutoQuitSeconds = 2,
    [string[]]$Backends = @("opengl", "vulkan", "d3d12"),
    [AllowEmptyString()]
    [string]$OutputPath = "",
    [switch]$ReportOnly
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

if (-not $Backends -or $Backends.Count -eq 0) {
    throw "At least one renderer backend must be supplied."
}

$startedAtUtc = [DateTime]::UtcNow
$exePath = Resolve-GameExePath -BuildDir $BuildDir -Config $Config
Write-Host "[ParityCheck] EXE: $exePath"

$results = @()
$failures = @()
foreach ($backend in $Backends) {
    try {
        $rawResult = Invoke-BackendRun `
            -ExePath $exePath `
            -Backend $backend `
            -AutoQuitSeconds $AutoQuitSeconds
        Write-Host "[ParityCheck] $backend`: $($rawResult.Line)"

        $errorMessage = $null
        if ($rawResult.ReportedBackend -ine $rawResult.Backend) {
            $errorMessage =
                "Requested backend '$($rawResult.Backend)' reported itself as '$($rawResult.ReportedBackend)'."
        } elseif ($rawResult.Status -ne "PASS") {
            $errorMessage =
                "$($rawResult.Backend) parity contract status is '$($rawResult.Status)'."
        }
        if ($null -ne $errorMessage) {
            $failures += $errorMessage
        }
        $results += [pscustomobject]@{
            Backend = $rawResult.Backend
            ReportedBackend = $rawResult.ReportedBackend
            ContractStatus = $rawResult.Status
            Signature = $rawResult.Signature
            Line = $rawResult.Line
            Passed = $null -eq $errorMessage
            ErrorMessage = $errorMessage
        }
    } catch {
        $errorMessage = $_.Exception.Message
        $failures += "$backend`: $errorMessage"
        $results += [pscustomobject]@{
            Backend = $backend
            ReportedBackend = $null
            ContractStatus = "ERROR"
            Signature = $null
            Line = $null
            Passed = $false
            ErrorMessage = $errorMessage
        }
        Write-Host "[ParityCheck] $backend`: ERROR $errorMessage"
    }
}

$validSignatures = @(
    $results |
        Where-Object {
            $_.Passed -and
            -not [string]::IsNullOrWhiteSpace([string]$_.Signature)
        })
$expectedSignature = if ($validSignatures.Count -gt 0) {
    $validSignatures[0].Signature
} else {
    $null
}
$signatureSummary = ($results | ForEach-Object { "$($_.Backend)=$($_.Signature)" }) -join " "
if ($null -ne $expectedSignature) {
    foreach ($result in $validSignatures | Select-Object -Skip 1) {
        if ($result.Signature -ne $expectedSignature) {
            $result.Passed = $false
            $result.ErrorMessage = "Parity contract signature mismatch: $signatureSummary"
            $failures += $result.ErrorMessage
        }
    }
}

$passed = $failures.Count -eq 0 -and $results.Count -eq $Backends.Count
$finishedAtUtc = [DateTime]::UtcNow
$report = [pscustomobject]@{
    SchemaVersion = 1
    StartedAtUtc = $startedAtUtc.ToString("o")
    FinishedAtUtc = $finishedAtUtc.ToString("o")
    DurationSeconds = ($finishedAtUtc - $startedAtUtc).TotalSeconds
    ExecutablePath = $exePath
    Backends = @($Backends)
    ExpectedSignature = $expectedSignature
    Passed = $passed
    Results = @($results)
    Failures = @($failures)
}

$outputPathAbs = $null
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $repoRoot = (Resolve-Path ".").Path
    $outputPathAbs = if ([IO.Path]::IsPathRooted($OutputPath)) {
        [IO.Path]::GetFullPath($OutputPath)
    } else {
        [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
    }
    $outputParent = Split-Path -Parent $outputPathAbs
    if (-not [string]::IsNullOrWhiteSpace($outputParent)) {
        New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
    }
    $report |
        ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $outputPathAbs -Encoding UTF8
    Write-Host "[ParityCheck] Report: $outputPathAbs"
}

if ($passed) {
    Write-Host "[ParityCheck] PASS: $($results.Count) backend signatures match ($expectedSignature)."
} elseif ($ReportOnly) {
    Write-Host "[ParityCheck] REPORT ONLY: one or more backend contracts failed."
} else {
    throw "Renderer parity contract failed: $($failures -join ' | ')"
}
