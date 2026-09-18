# Authored character surfaces

Status: Active
Type: Runbook
Last updated: 2026-09-14

## Approved models and gameplay defaults

`0016_Pidgey_SurfaceStudyMarkingPreserved` is the retained Pidgey benchmark.
Its approved feather-shaped brown/cream marking and all dependent asset bytes
are unchanged. Other Pidgey surface studies have been removed from the active
catalog and archived privately with their dependencies and review evidence.
Do not republish the rejected contour-reconstruction experiments.

Five additional approved models exercise reusable surface recipes:

| Model | Audited SV surface donors |
| --- | --- |
| `0017_Pidgeotto_SVSurface` | Starly feathers, beak and feet |
| `0018_Pidgeot_SVSurface` | Starly feathers, beak and feet |
| `0015_Beedrill_SVSurface` | Scyther exoskeleton; Houndoom bone-like armour for pale drills |
| `0104_Cubone_SVSurface` | Squirtle skin roughness; Houndoom bone-like armour for skull, club and claws |
| `0095_Onix_SVSurface` | Golem stone |

All six now have a matching `_Shiny` package using their original ZA shiny
palette. The promotion registry approves the authored regular/shiny pair for
each species, leaving imported packages available for comparison.

Pidgey, Pidgeotto and Beedrill use the authored regular as their gameplay default.
Their explicit shiny variants resolve to the matching authored shiny. Pidgeot,
Cubone and Onix have approved model pairs but no production gameplay roster
entries yet. This pass adds no balance data, encounters or random shiny spawns.
Default model resolution remains regular; the existing preload list follows
the updated species configuration.

## Preservation and material contract

Recipes resolve each recipient's original color layers and UV transforms.
Donor color does not replace markings. Feather recipes permit only bounded fine
luminance variation; other recipes retain resolved source color. Mesh payloads,
UVs, rig, animation and eye materials remain original. Beedrill's wing membrane
material is also protected. Material names alone are insufficient evidence of
anatomy: selection is checked against actual mesh and bone coverage.
Cubone's smooth skin retains its original normals: transferring the donor's
rounded normal features created unwanted belly bumps during the pilot review.

Native model materials explicitly select shared SSS surface transport through
`runtime_translation.authored_surface_model: "sss"`. Each must declare the
`SSS` family and provide `RoughnessMap` and `SSSMaskMap`. Incomplete or unknown
selections fail cooking. Other materials retain their original source behavior.
The opt-in uses the neutral surface profile; no backend-specific shader is added.

New packages declare `coordinate_system.triangle_winding: "counter_clockwise"`
after checking triangle normals against source vertex normals. The native cooker
converts triangle order to the shared clockwise world convention. This prevents
outward surfaces being shaded as backsides without altering source payloads.
`"clockwise"` passes through; omitted metadata preserves legacy behavior;
unknown declarations fail. The retained Pidgey package is unchanged.

The editor's ZA Source Stage preset attaches its stage probes according to
compatible material modes. It must not infer this requirement from `_ZA` in a
filename: authored surfaces retain native eye and membrane materials under
different names. This preset uses transient indexed batches so all three APIs
receive the stage probes, including Vulkan's scene registration path.

Shinies reuse approved geometry, winding metadata, normals, roughness and SSS
maps. Only authored base color is transferred using the per-texel linear ratio
of the original shiny and regular palettes. This retains accepted fine color
detail and marking edges. Original shiny eye and wing membrane materials stay
intact. Zero-channel transfers and clipping fail authoring rather than silently
discarding detail. Regular source packages and dependencies are SHA-256 checked.

## Authoring and review

The companion research workspace owns tools/research/surface_library_recipes.json,
build_surface_library.py, publish_surface_library.py and the capture tools.
Private inputs, smooth controls, donor audits, preservation hashes, matched
captures and reports live in the depot authoring/surface-library-v1 directory.
The game can load and cook published packages without the research workspace.
The paired shiny workflow and its review evidence live in the adjacent
authoring/surface-library-shiny-v1 directory. The research scripts
build_surface_shinies.py and promote_surface_library.py own that publication.

Explicit catalog models require matching species, name, variant and appearance
metadata before promotion. The Kanto promotion validator accepts these authored
pairs while rejecting unlabelled previews, mismatched identities, missing shiny
pairs and gameplay references to unpromoted models. Its synthetic contract is
asset-independent and runs in CTest as PAC_Tools.kanto_model_promotions_contract.

Open a Character Prefab in the editor Assets panel. Compare matching camera,
animation, quality and Source Bridge lighting. Smooth controls use the same
material transport and source orientation to isolate the fine texture
contribution. Check both sides and flight before judging bird markings.

Qualification covers editor previews and native game rendering on OpenGL,
Vulkan and D3D12, using unchanged image thresholds and expected-content guards.
Native model, asset catalog and editor catalog CPU contracts also apply.
Crowded-roster benchmarks use a private configuration overlay; they must not
change gameplay configuration or be presented as a full 151-species budget.

## Library qualification, 2026-09-14

The retained Pidgey and five pilots passed 25 editor cases (75 captures) plus
the six-unit native game scene on all three APIs. Expected-content checks,
unchanged image thresholds, three native CPU contracts and three authoring
contracts passed. Strict cook validation and source/dependency preservation
checks also passed.

A Release smoke comparison used 36 idle units at 1280x720, uncapped with inking,
on the local GTX 1070. Each API/cohort had 29 scored samples after five warmup
samples. GPU frame times were:

| API | Original (ms) | Authored (ms) |
| --- | ---: | ---: |
| OpenGL | 28.45 | 21.06 |
| Vulkan | 6.37 | 5.71 |
| D3D12 | 5.02 | 4.14 |

The deduplicated cooked texture footprint increased from 169.0 to 243.5 MiB
across these six models. This measures RGBA8 KTX2 files, not live GPU residency.
The single-run timing comparison is a smoke check; memory budgeting and art
review remain necessary before expanding to the full roster. Private reports
and unmodified native captures are packaged in the study's review.html.

## Shiny completion and promotion, 2026-09-14

All six shiny packages passed both editor lighting presets and the three bird
flight views. The six regular models also passed the corrected ZA Source Stage
path: 21 cases and 63 captures across OpenGL, Vulkan and D3D12. Two six-unit
native game scenes passed all three APIs, testing regular default resolution
and explicit shiny selection separately. Expected-content and image-difference
checks passed without changing the game's parity thresholds. Beedrill's new
wing guard rejects the saved black-membrane regression captures.

Seven relevant native CPU contracts, the synthetic and private promotion checks,
four shiny authoring/cleanup contracts and the three existing surface authoring
contracts passed. Strict cooking validated 54 active and 516 staged native
packages. All 167 approved regular source/dependency files retain their hashes.

The additional shiny palettes occupy 76.4 MiB in the deduplicated cooked texture
store; regular and shiny detail maps remain shared. No new performance timing
claim is made for this palette-only addition. The prior regular benchmark above
remains the available performance evidence. Matched regular/shiny views, game
captures and reports are in authoring/surface-library-shiny-v1/review.html.
