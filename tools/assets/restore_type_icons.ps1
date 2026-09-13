[CmdletBinding()]
param([switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$manifest = Get-Content (Join-Path $root 'config/ui/pokemon_type_icons.json') -Raw | ConvertFrom-Json
if ($manifest.icons.Count -ne 18) { throw 'The type icon manifest must contain all 18 types.' }
foreach ($icon in $manifest.icons) {
    if ($icon.path -notmatch '^assets/ui/types/home/[a-z]+\.png$') { throw 'Invalid type icon path.' }
    $path = Join-Path $root $icon.path
    if (-not (Test-Path -LiteralPath $path)) {
        if ($VerifyOnly) { throw "Missing type icon: $($icon.type)" }
        if ($icon.url -notlike 'https://www.pokepedia.fr/images/*') { throw 'Unexpected type icon source.' }
        New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
        Invoke-WebRequest -UseBasicParsing -Uri $icon.url -OutFile $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $icon.sha256) {
        throw "Type icon differs from the recorded original: $($icon.type)"
    }
}
Write-Host 'Verified all 18 original Pokemon HOME type icons.'
