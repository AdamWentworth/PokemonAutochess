[CmdletBinding()]
param(
    [string]$GameRoot = '',
    [string]$OutputDirectory = '',
    [string[]]$SourceTags = @('LGPE', 'PLA', 'SV', 'Sword', 'ZA'),
    [switch]$KantoOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath([string]$PathValue) {
    return [IO.Path]::GetFullPath($PathValue)
}

function Test-SimpleAtlasCoordinate([double]$Value) {
    for ($denominator = 1; $denominator -le 16; ++$denominator) {
        $scaled = $Value * $denominator
        if ([Math]::Abs($scaled - [Math]::Round($scaled)) -le 0.0015) {
            return $true
        }
    }
    $millesimal = [Math]::Round($Value * 1000.0) / 1000.0
    if ([Math]::Abs($Value - $millesimal) -gt 0.0000015) {
        return $false
    }
    for ($denominator = 1; $denominator -le 16; ++$denominator) {
        $nearest = [Math]::Round($millesimal * $denominator) / $denominator
        if ([Math]::Abs($millesimal - $nearest) -le 0.0015) {
            return $true
        }
    }
    return $false
}

function Get-EyeTrackSampling($Track) {
    $hasMeaningfulChange = $false
    foreach ($component in @('x', 'y', 'z', 'w')) {
        $values = @($Track.$component | ForEach-Object { [double]$_.value })
        if ($values.Count -eq 0) { continue }
        $minimum = [double]::PositiveInfinity
        $maximum = [double]::NegativeInfinity
        foreach ($value in $values) {
            $minimum = [Math]::Min($minimum, $value)
            $maximum = [Math]::Max($maximum, $value)
        }
        if (($maximum - $minimum) -le 0.005) { continue }
        $hasMeaningfulChange = $true
        $coordinates = [Collections.Generic.List[double]]::new()
        foreach ($value in $values) {
            if (-not (Test-SimpleAtlasCoordinate $value)) {
                return 'linear'
            }
            $present = $false
            foreach ($coordinate in $coordinates) {
                if ([Math]::Abs($coordinate - $value) -le 0.0015) {
                    $present = $true
                    break
                }
            }
            if (-not $present) {
                $coordinates.Add($value)
                if ($coordinates.Count -gt 16) { return 'linear' }
            }
        }
    }
    if ($hasMeaningfulChange) { return 'hold_source_frame' }
    return 'static'
}

function Test-EyeName([string]$Name) {
    return $Name -match '(?i)(eye|eyelid|eye_lid|iris|pupil)'
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Join-Path $scriptRoot '..\..'
}
$GameRoot = Resolve-FullPath $GameRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $GameRoot 'artifacts\eye-audit'
}
$OutputDirectory = Resolve-FullPath $OutputDirectory
$modelsRoot = Join-Path $GameRoot 'assets\models'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$sourcePattern = ($SourceTags | ForEach-Object {
    [Regex]::Escape($_)
}) -join '|'
$models = @(
    Get-ChildItem -LiteralPath $modelsRoot -Filter '*.phmodel' -File |
        Where-Object {
            $_.BaseName -match "_($sourcePattern)(?:_|$)" -and
            (-not $KantoOnly -or [int]$_.BaseName.Substring(0, 4) -le 151)
        } |
        Sort-Object Name
)
if ($models.Count -eq 0) {
    throw 'No native Pokemon models matched the eye-audit source filters.'
}

$rows = [Collections.Generic.List[object]]::new()
foreach ($model in $models) {
    $document = Get-Content -LiteralPath $model.FullName -Raw |
        ConvertFrom-Json
    if ($model.BaseName -notmatch
        "^(\d{4})_(.+?)_($sourcePattern)(?:_|$)") {
        throw "Could not parse native model identity: $($model.Name)"
    }
    $number = [int]$Matches[1]
    $species = $Matches[2]
    $source = $Matches[3]
    $isFemale = $model.BaseName -match '_Female(?:_|$)'
    $isShiny = $model.BaseName -match '_Shiny$'

    $eyeMaterials = @{}
    foreach ($material in @($document.materials)) {
        $name = [string]$material.name
        $family = [string]$material.shader_family
        if ((Test-EyeName $name) -or
            $family -eq 'Eye' -or
            $family -eq 'EyeClearCoat') {
            $eyeMaterials[$name] = $material
        }
    }

    $chosenParameters = @{}
    foreach ($materialName in $eyeMaterials.Keys) {
        $parameters = [Collections.Generic.List[string]]::new()
        foreach ($animation in @($document.animations)) {
            foreach ($track in @($animation.material_parameters)) {
                $parameter = [string]$track.parameter
                if ([string]$track.material -ne $materialName -or
                    $parameter -notmatch '^UVScaleOffset\d*$') {
                    continue
                }
                if (-not $parameters.Contains($parameter)) {
                    $parameters.Add($parameter)
                }
            }
        }
        if ($parameters.Contains('UVScaleOffset')) {
            $chosenParameters[$materialName] = 'UVScaleOffset'
        } elseif ($parameters.Count -gt 0) {
            $chosenParameters[$materialName] =
                @($parameters | Sort-Object)[0]
        }
    }

    $holdTracks = 0
    $linearTracks = 0
    $staticTracks = 0
    $holdExamples = [Collections.Generic.List[string]]::new()
    foreach ($animation in @($document.animations)) {
        foreach ($track in @($animation.material_parameters)) {
            $materialName = [string]$track.material
            if (-not $chosenParameters.ContainsKey($materialName) -or
                [string]$track.parameter -ne
                    $chosenParameters[$materialName]) {
                continue
            }
            $sampling = Get-EyeTrackSampling $track
            if ($sampling -eq 'hold_source_frame') {
                ++$holdTracks
                if ($holdExamples.Count -lt 3) {
                    $holdExamples.Add([string]$animation.name)
                }
            } elseif ($sampling -eq 'linear') {
                ++$linearTracks
            } else {
                ++$staticTracks
            }
        }
    }

    $eyeBoneIndices = @{}
    for ($index = 0; $index -lt @($document.skeleton.bones).Count; ++$index) {
        if (Test-EyeName ([string]$document.skeleton.bones[$index].name)) {
            $eyeBoneIndices[$index] = $true
        }
    }
    $eyeBoneTracks = 0
    foreach ($animation in @($document.animations)) {
        foreach ($track in @($animation.tracks)) {
            if ($eyeBoneIndices.ContainsKey([int]$track.bone)) {
                ++$eyeBoneTracks
            }
        }
    }

    $eyeVisibilityTracks = 0
    foreach ($animation in @($document.animations)) {
        foreach ($track in @($animation.mesh_visibility)) {
            if ((Test-EyeName ([string]$track.mesh)) -and
                @($track.values) -contains $false) {
                ++$eyeVisibilityTracks
            }
        }
    }
    $eyeSubmeshes = @(
        @($document.model.submeshes) |
            Where-Object { Test-EyeName ([string]$_.name) }
    ).Count

    $mechanism = if (($holdTracks + $linearTracks + $staticTracks) -gt 0) {
        'atlas_uv'
    } elseif ($eyeVisibilityTracks -gt 0) {
        'mesh_visibility'
    } elseif ($eyeBoneTracks -gt 0) {
        'skeletal_eye'
    } elseif ($eyeMaterials.Count -gt 0 -or $eyeSubmeshes -gt 0) {
        'static_eye_surface'
    } else {
        'embedded_or_static'
    }

    $rows.Add([pscustomobject][ordered]@{
        model = $model.BaseName
        number = $number
        species = $species
        source = $source
        female = $isFemale
        shiny = $isShiny
        animation_count = @($document.animations).Count
        mechanism = $mechanism
        eye_materials = $eyeMaterials.Count
        eye_submeshes = $eyeSubmeshes
        eye_bone_tracks = $eyeBoneTracks
        eye_visibility_tracks = $eyeVisibilityTracks
        atlas_hold_tracks = $holdTracks
        atlas_linear_tracks = $linearTracks
        atlas_static_tracks = $staticTracks
        hold_examples = @($holdExamples)
        flags = [Collections.Generic.List[string]]::new()
    })
}

foreach ($pair in @($rows | Group-Object number, species, source, female)) {
    $regular = @($pair.Group | Where-Object { -not $_.shiny })
    $shiny = @($pair.Group | Where-Object { $_.shiny })
    if ($regular.Count -ne 1 -or $shiny.Count -ne 1) { continue }
    $left = $regular[0]
    $right = $shiny[0]
    $leftSignature = @(
        $left.mechanism,
        $left.atlas_hold_tracks,
        $left.atlas_linear_tracks,
        $left.atlas_static_tracks,
        $left.eye_bone_tracks,
        $left.eye_visibility_tracks) -join ':'
    $rightSignature = @(
        $right.mechanism,
        $right.atlas_hold_tracks,
        $right.atlas_linear_tracks,
        $right.atlas_static_tracks,
        $right.eye_bone_tracks,
        $right.eye_visibility_tracks) -join ':'
    if ($leftSignature -ne $rightSignature) {
        $left.flags.Add('shiny_eye_channel_mismatch')
        $right.flags.Add('shiny_eye_channel_mismatch')
    }
}

$summary = [pscustomobject][ordered]@{
    model_count = $rows.Count
    species_count = @($rows | Select-Object number -Unique).Count
    atlas_models = @($rows | Where-Object mechanism -eq 'atlas_uv').Count
    skeletal_eye_models = @(
        $rows | Where-Object mechanism -eq 'skeletal_eye').Count
    visibility_eye_models = @(
        $rows | Where-Object mechanism -eq 'mesh_visibility').Count
    static_or_embedded_models = @(
        $rows | Where-Object {
            $_.mechanism -in @('static_eye_surface', 'embedded_or_static')
        }).Count
    hold_source_frame_tracks =
        ($rows | Measure-Object atlas_hold_tracks -Sum).Sum
    linear_tracks = ($rows | Measure-Object atlas_linear_tracks -Sum).Sum
    static_atlas_tracks =
        ($rows | Measure-Object atlas_static_tracks -Sum).Sum
    flagged_models = @($rows | Where-Object { $_.flags.Count -gt 0 }).Count
}

$jsonPath = Join-Path $OutputDirectory 'kanto_eye_audit.json'
[pscustomobject][ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    summary = $summary
    models = @($rows)
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding utf8

$markdown = [Collections.Generic.List[string]]::new()
$markdown.Add('# Kanto Eye Handling Audit')
$markdown.Add('')
$markdown.Add(('Generated: `{0}`' -f [DateTime]::UtcNow.ToString('o')))
$markdown.Add('')
$markdown.Add('This report distinguishes discrete eye-atlas cell selection from continuous pupil motion. Exact rational atlas coordinates with meaningful movement use `hold_source_frame`; ordinary floating-point curves remain `linear`. Tiny exporter noise below 0.005 is treated as static.')
$markdown.Add('')
$markdown.Add('## Summary')
$markdown.Add('')
$markdown.Add('| Models | Species | Atlas models | Hold tracks | Linear tracks | Static/embedded models | Flags |')
$markdown.Add('| ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
$markdown.Add(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} |' -f
    $summary.model_count,
    $summary.species_count,
    $summary.atlas_models,
    $summary.hold_source_frame_tracks,
    $summary.linear_tracks,
    $summary.static_or_embedded_models,
    $summary.flagged_models))
$markdown.Add('')
$markdown.Add('## Species Coverage')
$markdown.Add('')
$markdown.Add('| # | Species | Source | Variants | Mechanisms | Hold | Linear | Eye bones | Eye visibility | Flags |')
$markdown.Add('| ---: | --- | --- | ---: | --- | ---: | ---: | ---: | ---: | --- |')
foreach ($group in @($rows | Group-Object number, species, source)) {
    $first = $group.Group[0]
    $mechanisms = @($group.Group.mechanism | Sort-Object -Unique) -join ', '
    $flags = @(
        $group.Group |
            ForEach-Object { @($_.flags) } |
            ForEach-Object { $_ } |
            Sort-Object -Unique
    ) -join ', '
    $hold = ($group.Group | Measure-Object atlas_hold_tracks -Sum).Sum
    $linear = ($group.Group | Measure-Object atlas_linear_tracks -Sum).Sum
    $bones = ($group.Group | Measure-Object eye_bone_tracks -Sum).Sum
    $visibility =
        ($group.Group | Measure-Object eye_visibility_tracks -Sum).Sum
    $markdown.Add(('| {0:000} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} |' -f
        $first.number,
        $first.species,
        $first.source,
        $group.Count,
        $mechanisms,
        $hold,
        $linear,
        $bones,
        $visibility,
        $flags))
}
$markdownPath = Join-Path $OutputDirectory 'kanto_eye_audit.md'
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8

Write-Host (
    'Eye audit complete: {0} models, {1} species, {2} hold tracks, {3} linear tracks, {4} flags.' -f
        $summary.model_count,
        $summary.species_count,
        $summary.hold_source_frame_tracks,
        $summary.linear_tracks,
        $summary.flagged_models)
Write-Host "JSON: $jsonPath"
Write-Host "Markdown: $markdownPath"

if ($summary.flagged_models -gt 0) {
    throw (
        'Eye audit found {0} model variants with inconsistent regular/shiny channels. See {1}' -f
            $summary.flagged_models,
            $markdownPath)
}

[pscustomobject]@{
    JsonPath = $jsonPath
    MarkdownPath = $markdownPath
    Summary = $summary
}
