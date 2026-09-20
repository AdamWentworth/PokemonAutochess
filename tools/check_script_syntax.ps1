[CmdletBinding()]
param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Join-Path $PSScriptRoot ".."
}
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)

# --others includes newly added but not yet staged scripts, which a plain
# ls-files misses; --exclude-standard keeps ignored scratch and build output out
# of the gate. Without this a brand new script could be pushed unparsed.
$trackedPowerShell = @(
    & git -C $RepoRoot ls-files --cached --others --exclude-standard -- '*.ps1' '*.psm1'
)
if ($LASTEXITCODE -ne 0) {
    throw "Could not enumerate PowerShell files."
}

$parseFailures = [System.Collections.Generic.List[string]]::new()
foreach ($relativePath in $trackedPowerShell) {
    $tokens = $null
    $errors = $null
    $fullPath = Join-Path $RepoRoot $relativePath
    [System.Management.Automation.Language.Parser]::ParseFile(
        $fullPath,
        [ref]$tokens,
        [ref]$errors) | Out-Null
    foreach ($errorRecord in $errors) {
        $parseFailures.Add(
            "$relativePath`:$($errorRecord.Extent.StartLineNumber): $($errorRecord.Message)")
    }
}
if ($parseFailures.Count -gt 0) {
    throw "PowerShell syntax validation failed:`n$($parseFailures -join "`n")"
}

$trackedPython = @(& git -C $RepoRoot ls-files -- '*.py')
if ($LASTEXITCODE -ne 0) {
    throw "Could not enumerate tracked Python files."
}
if ($trackedPython.Count -gt 0) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($null -eq $python) {
        throw "Python is required to syntax-check tracked Python tools."
    }
    $pythonPaths = @($trackedPython | ForEach-Object { Join-Path $RepoRoot $_ })
    $pythonSyntaxProgram = @'
import ast
import pathlib
import sys

failures = []
for raw_path in sys.argv[1:]:
    path = pathlib.Path(raw_path)
    try:
        source = path.read_text(encoding='utf-8-sig')
        ast.parse(source, filename=str(path))
    except (SyntaxError, UnicodeError) as error:
        failures.append(f'{path}: {error}')

if failures:
    print('\n'.join(failures), file=sys.stderr)
    raise SystemExit(1)
'@
    & $python.Path -c $pythonSyntaxProgram @pythonPaths
    if ($LASTEXITCODE -ne 0) {
        throw "Python syntax validation failed with exit code $LASTEXITCODE."
    }
}

Write-Host (
    "Script syntax OK: $($trackedPowerShell.Count) PowerShell and " +
    "$($trackedPython.Count) Python files.")
