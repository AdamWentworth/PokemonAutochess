param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Write-FixtureFile {
    param([string]$PathValue, [int]$Bytes)
    New-Item -ItemType Directory -Path (Split-Path -Parent $PathValue) -Force |
        Out-Null
    [IO.File]::WriteAllBytes($PathValue, [byte[]]::new($Bytes))
}

$retireScript = Join-Path $PSScriptRoot 'retire_game_editor_artifacts.ps1'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'pokemonautochess-editor-owner-test-' +
    [Guid]::NewGuid().ToString('N'))
$gameRoot = Join-Path $tempRoot 'game'

try {
    New-Item -ItemType Directory -Path (Join-Path $gameRoot 'build') -Force |
        Out-Null
    Set-Content `
        -LiteralPath (Join-Path $gameRoot 'build\CMakeCache.txt') `
        -Value 'PHLOSION_BUILD_EDITOR:BOOL=OFF' `
        -Encoding ASCII
    Write-FixtureFile (Join-Path $gameRoot 'build\Debug\PhlosionEditor.exe') 4
    Write-FixtureFile (Join-Path $gameRoot 'build\PhlosionEditor.dir\old.obj') 5
    Write-FixtureFile (Join-Path $gameRoot 'build\Debug\PokemonAutochess.exe') 6
    Write-FixtureFile (Join-Path $gameRoot 'build\unrelated\keep.bin') 7

    $plan = & $retireScript -GameRoot $gameRoot
    Assert-Condition ($plan.mode -eq 'plan-only') 'Default mode must be plan-only.'
    Assert-Condition ($plan.target_count -eq 2) 'Plan should find only the two editor fixtures.'
    Assert-Condition ($plan.target_bytes -eq 9) 'Plan should total only editor fixture bytes.'
    Assert-Condition (
        (Test-Path -LiteralPath (Join-Path $gameRoot 'build\Debug\PhlosionEditor.exe'))) `
        'Plan-only mode removed the editor fixture.'

    $confirmationRejected = $false
    try {
        & $retireScript -GameRoot $gameRoot -Execute | Out-Null
    } catch {
        $confirmationRejected = $_.Exception.Message -like '*ConfirmDeletion*'
    }
    Assert-Condition $confirmationRejected 'Execution must require explicit confirmation.'

    $result = & $retireScript `
        -GameRoot $gameRoot `
        -Execute `
        -ConfirmDeletion
    Assert-Condition ($result.target_count -eq 2) 'Execution should remove the planned editor targets.'
    Assert-Condition (
        -not (Test-Path -LiteralPath (Join-Path $gameRoot 'build\Debug\PhlosionEditor.exe'))) `
        'Editor executable fixture survived retirement.'
    Assert-Condition (
        -not (Test-Path -LiteralPath (Join-Path $gameRoot 'build\PhlosionEditor.dir'))) `
        'Editor intermediate fixture survived retirement.'
    Assert-Condition (
        (Test-Path -LiteralPath (Join-Path $gameRoot 'build\Debug\PokemonAutochess.exe'))) `
        'Active game executable must survive editor retirement.'
    Assert-Condition (
        (Test-Path -LiteralPath (Join-Path $gameRoot 'build\unrelated\keep.bin'))) `
        'Unrelated active-build content must survive editor retirement.'

    Set-Content `
        -LiteralPath (Join-Path $gameRoot 'build\CMakeCache.txt') `
        -Value 'PHLOSION_BUILD_EDITOR:BOOL=ON' `
        -Encoding ASCII
    $enabledRejected = $false
    try {
        & $retireScript -GameRoot $gameRoot | Out-Null
    } catch {
        $enabledRejected = $_.Exception.Message -like '*not proven editor-disabled*'
    }
    Assert-Condition $enabledRejected 'An editor-enabled game cache must be rejected.'

    Write-Host '[GameEditorRetirementTest] PASS'
} finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
    $resolvedSystemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTempRoot.StartsWith(
            $resolvedSystemTemp,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item `
            -LiteralPath $resolvedTempRoot `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
    }
}
