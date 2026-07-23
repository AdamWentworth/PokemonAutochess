param()

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "RenderParityContentGuard.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "RenderParitySceneManifest.psm1") -Force
Add-Type -AssemblyName System.Drawing

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Write-SyntheticContentImage {
    param(
        [string]$Path,
        [ValidateSet("textured", "black", "empty")]
        [string]$ModelMode
    )

    $bitmap = [Drawing.Bitmap]::new(100, 100, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $background = [Drawing.Color]::FromArgb(255, 232, 220, 176)
        for ($y = 0; $y -lt $bitmap.Height; ++$y) {
            for ($x = 0; $x -lt $bitmap.Width; ++$x) {
                $bitmap.SetPixel($x, $y, $background)
            }
        }

        if ($ModelMode -ne "empty") {
            for ($y = 35; $y -lt 65; ++$y) {
                for ($x = 35; $x -lt 65; ++$x) {
                    if ($ModelMode -eq "black") {
                        $color = [Drawing.Color]::Black
                    } elseif ((($x + $y) % 3) -eq 0) {
                        $color = [Drawing.Color]::FromArgb(255, 48, 132, 74)
                    } elseif ((($x + $y) % 3) -eq 1) {
                        $color = [Drawing.Color]::FromArgb(255, 200, 108, 48)
                    } else {
                        $color = [Drawing.Color]::FromArgb(255, 54, 112, 180)
                    }
                    $bitmap.SetPixel($x, $y, $color)
                }
            }
        }

        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "pokemonautochess-render-content-guard-" + [Guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $tempRoot)

try {
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    $manifest = Import-RenderParitySceneManifest `
        -Path (Join-Path $repoRoot "config/render_parity_scene_matrix.json") `
        -RepoRoot $repoRoot
    $worldScenes = @(
        $manifest.scenes |
            Where-Object { @($_.coverage) -contains "world" })
    Assert-Condition ($worldScenes.Count -gt 0) (
        "The render parity manifest should contain guarded world scenes.")
    foreach ($scene in $worldScenes) {
        Assert-Condition (@($scene.contentGuards).Count -gt 0) (
            "World scene '$($scene.name)' should define expected-content guards.")
    }

    $guard = [pscustomobject]@{
        name = "synthetic-model-texture"
        x = 0.25
        y = 0.25
        width = 0.5
        height = 0.5
        nearBlackLuminanceMaximum = 16
        midtoneLuminanceMinimum = 64
        midtoneLuminanceMaximum = 190
        maximumNearBlackPixelRatio = 0.1
        minimumMidtonePixelRatio = 0.15
    }

    $texturedPath = Join-Path $tempRoot "textured.png"
    $blackPath = Join-Path $tempRoot "black.png"
    $emptyPath = Join-Path $tempRoot "empty.png"
    Write-SyntheticContentImage -Path $texturedPath -ModelMode "textured"
    Write-SyntheticContentImage -Path $blackPath -ModelMode "black"
    Write-SyntheticContentImage -Path $emptyPath -ModelMode "empty"

    $textured = Test-RenderParityImageContent -ImagePath $texturedPath -Guard $guard
    $black = Test-RenderParityImageContent -ImagePath $blackPath -Guard $guard
    $empty = Test-RenderParityImageContent -ImagePath $emptyPath -Guard $guard

    Assert-Condition $textured.Passed (
        "A visibly textured model should pass the content guard: " +
        ($textured.FailureReasons -join "; "))
    Assert-Condition (-not $black.Passed) (
        "A black model silhouette must fail the content guard.")
    Assert-Condition (
        $black.NearBlackPixelRatio -gt $guard.maximumNearBlackPixelRatio) (
        "The black-model failure should be attributed to excess near-black pixels.")
    Assert-Condition (-not $empty.Passed) (
        "An empty model region must fail the content guard.")
    Assert-Condition (
        $empty.MidtonePixelRatio -lt $guard.minimumMidtonePixelRatio) (
        "The missing-model failure should be attributed to absent visible midtones.")

    Write-Host (
        "[RenderParityContentGuardTest] PASS " +
        ("textured(mid={0:P1}, black={1:P1}) " -f `
            $textured.MidtonePixelRatio,
            $textured.NearBlackPixelRatio) +
        ("black(mid={0:P1}, black={1:P1}) " -f `
            $black.MidtonePixelRatio,
            $black.NearBlackPixelRatio) +
        ("empty(mid={0:P1}, black={1:P1})" -f `
            $empty.MidtonePixelRatio,
            $empty.NearBlackPixelRatio))
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
