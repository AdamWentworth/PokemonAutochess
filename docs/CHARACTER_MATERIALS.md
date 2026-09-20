# Character materials

Status: Active
Type: Reference
Last updated: 2026-09-19

Autochess owns its character programs under `src/game/render/materials/character`.
They reproduce published assets' recovered material behavior. Descriptive names
identify rendering roles; source-game provenance remains here and in shader
comments. Recovery evidence and extraction tools remain in the private
`PokemonSwitchAssetResearch` workspace.

| Rendering role | Source behavior | Preserved mode |
| --- | --- | --- |
| Layered animated effect | Scarlet/Violet layered Unlit, scrolling fire, smoke and displacement | 27 |
| Layered eye coat | Scarlet/Violet EyeClearCoat and Legends: Arceus Eye qualifiers | 28, 30 |
| Animated eye UV | Clip-bound eye UV tracks | 29, 30 |
| Facial overlay | Authored facial shells, including Gastly | 31 |
| Layered character | Legends: Z-A IkCharacter lighting and color processing | 32 |
| Subsurface | Scarlet/Violet SSS; qualified fibre approximation | 33 |
| View-angle layer | Scarlet/Violet FresnelEffect and local probe | 34 |
| Refractive eye | Legends: Z-A iris parallax, coat and eyelid lighting | 35 |

Ordinary PBR can select the explicit dielectric-mask qualifier. These identities
and scalar packing rules belong to the game. Cooked mode numbers stay compatible.

`config/render/world_materials.json` is the field-and-character profile for the
game and editor. CMake composes the field branches into the character program,
copies OpenGL/HLSL vertex and fragment snippets, and builds six Vulkan variants
through `PAC_WorldMaterialShaders`. Runtime artifacts live in
`.phlosion/materials/world` and must accompany the manifest in a release bundle.
The game does not require the private research repository to build or run.

The engine retains skinning, transforms, mesh submission, texture bindings,
standard PBR and generic review diagnostics. Autochess owns character lighting,
eye and skin formulas, reflection-atlas decoding, effect vertex displacement,
and per-mode parameter packing. A standalone engine receives none of these
formulas unless a project explicitly supplies a material profile.

The obsolete engine glTF material-name heuristic that boosted materials named
`fire` is removed. Published native fire already carries its authored inputs and
does not depend on that legacy boost. Engine texture-upload tracing is selected
by `PHLOSION_TRACE_WORLD_TEXTURE_UPLOADS` (`*` or a key substring), without
knowledge of any project's texture naming.

## Verification

Eight shader contracts under `tests/materials` moved with their implementations.
D3D12 and Vulkan CPU parameter tests load the runtime project profile. The engine
separately tests its profile schema, source ownership, mapping evaluation,
defaults and rejection of invalid inputs.

`tools/check_character_materials.ps1` covers fire at two animation times, smoke,
body and eye families, fibre, view-angle layers, pale authored wings and bird
flight, with source and authored-stage review lighting. Material-specific content
guards accompany the existing cross-renderer image thresholds. The stage preset
has a darker wing response than the source preset; each checks its expected region.
Pass `-BaselineDirectory` to also compare each renderer against captures from a
previous revision, rather than relying only on cross-renderer agreement.

`tools/check_native_character_materials.ps1` captures isolated game cohorts using
the same published models. It creates a disposable config overlay and local
junctions under its output directory, without changing gameplay's species catalog.
Also run affected standard native parity scenes with character inking enabled.
Repeat Vulkan captures with `PHLOSION_VULKAN_DISABLE_DESCRIPTOR_INDEXING=1` to
exercise direct submission.

See [the test plan](TEST_PLAN.md) and [renderer parity](RENDERER_PARITY_CONTRACT.md).

## Boundary verification, 2026-09-19

Baseline: game `35bb577` with engine `4a066c0`. Local evidence is retained under
`debug/character-material-extraction`; private assets and captures are not Git
inputs. The maintained capture matrices record the exact models, animation
times, lighting and expected-content regions.

- All 15 editor cases passed on OpenGL, Vulkan and D3D12. Every one of the 45
  cropped images was pixel-identical to its same-renderer baseline.
- Both native character cohorts passed on all three APIs. All 30 individual
  model regions were pixel-identical to their same-renderer baseline.
- The standard native `static-pbr`, `transparent-vfx` and `combat` scenes passed
  all three APIs with the existing content guards and image thresholds.
- Both native cohorts and all three standard scenes also passed Vulkan direct
  submission against OpenGL. Runtime logs confirmed `descriptorIndexing=0` and
  `indirectWorld=0` for these compatibility runs.
- All 15 editor cases also passed this direct-submission configuration. Its 30
  OpenGL/Vulkan captures were pixel-identical to the baseline, with the Vulkan
  submission flags confirmed in every case's startup log.
- Eight source comparisons preserved the original shader formulas after
  composition, allowing only comments, whitespace and descriptive symbol names
  to change. These include vertex displacement and layered effects.
- The engine without a project profile rendered consistent standard PBR on
  all three APIs. This deliberately omits the game's source-specific recoloring;
  its content check expects the neutral surface. OpenGL's pipeline and draw
  readiness checks now allow unused project uniforms to be optimized away.
- The full game test run passed 281 of 282 checks initially; the remaining
  fire contract still referenced its former engine shader path. After updating
  that path, it and all 14 other affected checks passed. All five standalone
  engine tests passed. Debug developer tools and data validation also passed.
- A fresh configuration fetched the pinned Engine, Packages and VFX commits
  without local source overrides, passed all ten source-boundary/material
  contracts, and compiled all six project Vulkan shader variants. This check
  reused the installed vcpkg dependencies; it was not a full clean game build.
- Paired Debug and Release editor/plugin builds passed the ABI 34 probe.

Release benchmarks used the same starter-line snapshot, 1280x720, inking enabled,
uncapped frame rate, 35 seconds and 29 scored samples per run. OpenGL CPU frame
time changed from 15.639 to 15.289 ms; D3D12 changed from 5.204 to 5.468 ms.
These single runs establish a comparison, not statistical equivalence. Raw
benchmark files use tags `character_before_valid_20260919_175821` and
`character_after_20260919_180105`.

This qualifies the listed material families and scenes on the local GTX 1070.
It does not qualify every published model, GPU or driver.
