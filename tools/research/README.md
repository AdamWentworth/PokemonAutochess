# Character Capture Research Tools

This directory contains the tracked, source-agnostic workflow for private
Pokemon character GPU-capture research. Raw game content, RDC files, decoded
buffers, and private evidence stay outside the repository.

## Static material research

Static analysis is the first path when retained source packages and shader
archives are available. It requires no game, emulator, editor, or graphics
window. The SV Eevee vertical slice measures every decoded texture role,
reconstructs Trinity shader keys from material options plus metadata defaults,
fingerprints the uniquely selected offline-decompiled programs, and uses
compiled option-permutation set differences plus use-site data flow to map
anonymous samplers and directly used material constants. It also records
whether the selected BNSH reflection headers actually retain named resource
dictionaries.

Prepare the private differential programs after extracting `sss.bnsh`,
`sss.trsha.json`, `eye_clear_coat.bnsh`, and
`eye_clear_coat.trsha.json` into one shader-study directory:

```powershell
.\tools\research\extract_sv_eevee_shader_permutations.ps1 `
  -ShaderStudyRoot D:\private\sv-v3.0.1-shader-study `
  -ExporterDll D:\private\TrinityBatchExporter.dll `
  -ShaderDecoderExe D:\private\Ryujinx.ShaderTools.exe
```

This invokes file parsers and a Maxwell-to-GLSL translator only. It does not
launch Scarlet, an emulator, the Inspector, or another graphics window. Then
generate and validate the evidence report:

```powershell
python .\tools\research\analyze_sv_eevee_static_material.py `
  --game-root . `
  --shader-study D:\private\sv-v3.0.1-shader-study `
  --za-manifest D:\private\0133_Eevee_ZA.phmodel `
  --output .\artifacts\character-static-evidence\sv-eevee-modern-surface-v1.json

.\tools\research\validate_sv_eevee_static_material.ps1 `
  -ReportPath .\artifacts\character-static-evidence\sv-eevee-modern-surface-v1.json `
  -PromotedReportPath .\docs\kanto\evidence\sv_eevee_static_material_report.json
```

The promoted report stores identities and conclusions only. Static shader
evidence can prove option selection, texture arity, data flow, and equations
whose constants have been mapped. It cannot prove runtime light/environment
buffers, exposure, post-processing, selected mips, or the final framebuffer.
For the shipped SV Eevee programs, both reflection pointers are null; names for
the remaining scene resources cannot be recovered from those archives.

## Runtime capture research

Validate the first planned capture:

```powershell
.\tools\research\validate_character_capture.ps1 `
  -SpecPath .\tools\research\captures\sv-eevee-modern-surface-v1.json
```

Create or refresh its private workspace without launching an emulator:

```powershell
.\tools\research\new_character_capture_workspace.ps1 `
  -SpecPath .\tools\research\captures\sv-eevee-modern-surface-v1.json `
  -WorkspaceRoot D:\ProjectData\Games\PokemonAutochess\CharacterResearch\CaptureLab `
  -Force
```

The optional source launcher is deliberately gated. It verifies RenderDoc,
emulator, and source program/content SHA-256 identities, disables fullscreen
capture, and still requires explicit `-AllowLaunch`. It refuses to launch
while the source program hash is unrecorded in the capture specification.

After acquiring an RDC, protect it before analysis:

```powershell
.\tools\research\protect_character_capture.ps1 `
  -CapturePath D:\private\capture.rdc `
  -WorkspaceRoot D:\ProjectData\Games\PokemonAutochess\CharacterResearch\CaptureLab\sv-eevee-modern-surface-v1 `
  -StateId regular_neutral_front
```

`analyze_character_capture.py` runs inside RenderDoc's bundled Python. It
requires `CHARACTER_CAPTURE_RDC`, `CHARACTER_CAPTURE_REPORT`, and
`CHARACTER_CAPTURE_STATE_ID`; use `CHARACTER_CAPTURE_EVENT_IDS` after the
draw inventory identifies the Pokemon draws. An empty event list inventories
every draw but does not claim which draws belong to the Pokemon.

Never promote a specification from `planned` merely because the source files
or canonical model exist. It needs a protected RDC, exact source-game setup,
verified Pokemon event IDs, and the evidence types listed by that capture
specification.
