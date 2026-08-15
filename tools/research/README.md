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

The corpus workflow uses Eevee's proven option decoder across every selected
SV Kanto manifest. Extract/decode all nine required source families directly
from a retained Scarlet RomFS, then require unique resolution for the entire
corpus:

```powershell
.\tools\research\extract_sv_kanto_shader_sources.ps1 `
  -RomfsRoot D:\private\Pokemon_Scarlet_v3.0.1_Merged_RomFS `
  -ShaderStudyRoot D:\private\sv-v3.0.1-shader-study `
  -ExporterDll D:\private\TrinityBatchExporter.dll `
  -OodleDecoder D:\private\ooz.exe
```

The optional decoder is needed when the retained Oodle DLL cannot decode a
source stream. Extraction is atomic; partial outputs are removed. The wrapper
validates decoded family/archive identities, understands multi-word Trinity
option tables, runs `analyze_sv_kanto_shader_permutations.py` with complete
source and exact-resolution requirements, and writes the full private corpus
inventory outside Git. The checked-in compact evidence contains hashes, ABI
widths, packed keys, and variation identities only.

Extract and translate the 22 uniquely selected programs, then create the
compiled ABI ledger:

```powershell
.\tools\research\extract_sv_kanto_selected_programs.ps1 `
  -ShaderStudyRoot D:\private\sv-v3.0.1-shader-study `
  -ExporterDll D:\private\TrinityBatchExporter.dll `
  -ShaderDecoderExe D:\private\Ryujinx.ShaderTools.exe

python .\tools\research\analyze_sv_kanto_selected_program_abi.py `
  --program-root D:\private\sv-v3.0.1-shader-study\selected-programs `
  --output .\artifacts\sv-kanto-selected-program-abi.json
```

The extractor verifies promoted archive hashes and selected variation counts
before retaining output. The ABI analyzer verifies every translated stage hash
and records anonymous sampler/buffer use sites. It intentionally does not turn
anonymous symbols into material semantics without a compiled permutation
differential or independently mapped constant layout.

The strict differential planner and analyzer deliberately accept only archived
program pairs that differ in one retained material option:

```powershell
python .\tools\research\plan_sv_kanto_program_differentials.py `
  --inventory D:\private\sv_kanto_shader_inventory.json `
  --shader-study D:\private\sv-v3.0.1-shader-study `
  --output .\artifacts\sv-kanto-differential-plan.json

.\tools\research\extract_sv_kanto_differential_programs.ps1 `
  -ShaderStudyRoot D:\private\sv-v3.0.1-shader-study `
  -PlanPath .\artifacts\sv-kanto-differential-plan.json `
  -ExporterDll D:\private\TrinityBatchExporter.dll `
  -ShaderDecoderExe D:\private\Ryujinx.ShaderTools.exe

python .\tools\research\analyze_sv_kanto_program_differentials.py `
  --plan .\artifacts\sv-kanto-differential-plan.json `
  --selected-program-root D:\private\sv-v3.0.1-shader-study\selected-programs `
  --comparison-program-root D:\private\sv-v3.0.1-shader-study\differential-programs `
  --output .\artifacts\sv-kanto-program-differentials.json
```

Dense GLSL binding ordinals renumber when a sampler disappears, so the stable
compiled identity is the retained `tcb` symbol plus sampler type. Comparisons
that change multiple options or do not isolate one sampled symbol are rejected.

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

Tentacool/Tentacruel's selected FresnelEffect program has a dedicated static
use-site analyzer. It verifies the four regular/shiny manifests, the promoted
ABI hash, sampler and material-buffer use sites, the fifth-power Fresnel
equation, and the explicit undecoded-local-probe boundary:

```powershell
python .\tools\research\analyze_sv_fresnel_effect_static_material.py `
  --game-root . `
  --shader-study D:\private\sv-v3.0.1-shader-study `
  --output .\artifacts\character-static-evidence\sv-fresnel-effect.json

.\tools\research\test_sv_fresnel_effect_static_material_workflow.ps1
.\tools\research\test_sv_kanto_runtime_bridge.ps1
```

This workflow is emulator-free. It proves the material mapping and equation;
it does not claim that Phlosion's neutral environment is the source game's
authored local probe or final framebuffer.

### Legends: Z-A corpus workflow

Z-A uses the same emulator-free discipline with its own shader registry. The
extractor validates the retained v2.0.0 archives and metadata, resolves all 234
selected Kanto materials, extracts the six selected programs, and builds a
complete graph of exact single-option program transitions:

```powershell
.\tools\research\extract_za_kanto_shader_sources.ps1 `
  -GameFilesRoot D:\private\Pokemon_Legends_ZA_v2.0.0_Merged_GameFiles `
  -ShaderStudyRoot D:\private\za-v2.0.0-shader-study `
  -ExporterDll D:\private\TrinityBatchExporter.dll

.\tools\research\extract_sv_kanto_selected_programs.ps1 `
  -ShaderStudyRoot D:\private\za-v2.0.0-shader-study `
  -ExporterDll D:\private\TrinityBatchExporter.dll `
  -ShaderDecoderExe D:\private\Ryujinx.ShaderTools.exe

python .\tools\research\plan_za_kanto_option_graph.py `
  --inventory D:\private\za-v2.0.0-shader-study\za_kanto_shader_inventory.json `
  --shader-study D:\private\za-v2.0.0-shader-study `
  --output D:\private\za-v2.0.0-shader-study\za_kanto_option_graph_plan.json

.\tools\research\extract_sv_kanto_differential_programs.ps1 `
  -PlanPath D:\private\za-v2.0.0-shader-study\za_kanto_option_graph_plan.json `
  -ShaderStudyRoot D:\private\za-v2.0.0-shader-study `
  -ExporterDll D:\private\TrinityBatchExporter.dll `
  -ShaderDecoderExe D:\private\Ryujinx.ShaderTools.exe

python .\tools\research\analyze_za_kanto_option_differentials.py `
  --plan D:\private\za-v2.0.0-shader-study\za_kanto_option_graph_plan.json `
  --selected-program-root D:\private\za-v2.0.0-shader-study\selected-programs `
  --comparison-program-root D:\private\za-v2.0.0-shader-study\option-graph-programs `
  --differential-kind FullGraph `
  --output .\artifacts\za-kanto-option-graph.json
```

After recooking the selected corpus with the current Forge exporter, generate
the three shader-family reports and the local-reflection transport report with
the commands in `docs/kanto/evidence/README.md`. Those analyzers reconstruct
the packed HDR probes byte for byte and inspect only hashes, manifests,
compiled GLSL data flow, and Phlosion source. They do not launch an editor,
game, emulator, or graphics window.

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
