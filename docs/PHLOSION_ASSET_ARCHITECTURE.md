# Phlosion Asset Architecture

Status: Active
Type: Architecture
Last updated: 2026-08-08

This document defines the long-term asset pipeline shared by Pokemon
Autochess and future games built with Phlosion Engine. It replaces the idea
of inventing a separate runtime format for each game or source ecosystem.

## Decision

Phlosion Engine uses a deliberate mixture of:

- established formats for universal media payloads;
- Phlosion formats for engine-owned runtime semantics;
- one shared Phlosion Resource Container implementation for custom resource
  schemas;
- one asset-ID and dependency system regardless of physical file format.

Game Freak files and GLB files are source inputs. Neither source family
dictates the complete runtime representation.

The first supported format family is:

| Extension | Name | Responsibility |
| --- | --- | --- |
| `.phlo` | Phlosion Loadable Object | An assembled, reusable runtime object or prefab. |
| `.phmesh` | Phlosion Mesh | Cooked geometry, vertex streams, submeshes, bounds, and LODs. |
| `.phskel` | Phlosion Skeleton | Joint hierarchy, bind pose, stable joint identities, and compatible sockets. |
| `.phanim` | Phlosion Animation | Compressed clips, events, root-motion metadata, and skeleton compatibility. |
| `.phmat` | Phlosion Material | Material family, parameters, render state, and texture-role bindings. |
| `.phcol` | Phlosion Collision | Engine-level collision data and optional backend-cooked payloads. |
| `.phscene` | Phlosion Scene | World or level composition that places `.phlo` objects and other resources. |
| `.phv` | Phlosion Vault | Mountable shipping archive for cooked resources. |

PHRC means **Phlosion Resource Container**. It is the common container
specification and implementation behind the custom formats, not another
required public extension.

The current `.pacmdl` version 9 and PACD `.pak` are compatibility-era caches.
They remain functional during migration but are not the new source of truth.
`PACENV`, `PACPOKEMON`, `.paccar`, and other game- or genre-specific runtime
formats must not be introduced.

## Pipeline

```text
Source and authoring files
  Game Freak GFPAK/GFBMDL/BNTX/GFBANM/GFBCOL/BNSH
  Game Freak Trinity TRMDL/TRMSH/TRMBF/TRSKL/TRMTR/TRANM/BNTX
  GLB/glTF
  Blender and optional USD scene data
  PNG/EXR and other texture sources
  WAV/FLAC and other audio masters
                    |
                    v
Importers -> canonical engine asset IR
                    |
                    v
Phlosion Forge
  validation
  semantic translation
  dependency discovery
  deterministic cooking
  target-profile optimization
                    |
                    v
Runtime resources
  .phlo .phmesh .phskel .phanim .phmat .phcol .phscene
  KTX2 textures
  Opus/Ogg/WAV audio
  SPIR-V/DXIL/backend shader payloads
                    |
                    v
Loose development assets or mounted .phv vaults
                    |
                    v
Phlosion Engine on OpenGL, Vulkan, and D3D12
```

The canonical asset IR is the importer boundary, not necessarily a public
file extension. It may be retained as an inspectable JSON-plus-binary evidence
cache when provenance or reverse-engineering validation requires it. Runtime
code consumes cooked resources and never parses proprietary source formats in
the frame loop.

The Pokemon Scarlet importer uses `.phmodel` for that retained evidence cache.
It is JSON plus a bounded binary payload, not a shipping model format and not a
GLTF surrogate. It preserves native mesh streams, skeleton provenance,
animation names and loop evidence, material families, shader options,
parameters, texture roles, and sampler evidence before Forge translates them
to the typed runtime resources.

## Established Formats

Phlosion does not replace mature standards merely to put its name on every
file.

### Textures

KTX2 is the preferred cooked texture container. A cook profile may select
GPU-native, universal, lossless, or other appropriate payloads. Source-faithful
Game Freak work must retain authored mip chains, colorspace, sampler evidence,
and provenance; KTX2 does not authorize a lossy conversion.

### Audio

WAV or FLAC remains suitable for source masters. Runtime choices include Opus,
Ogg/Vorbis, or PCM/WAV according to latency, streaming, quality, and platform
requirements. Phlosion does not define a custom audio codec.

### Shaders

Editable shader sources remain in the engine's supported source languages.
The cooker may package established backend representations such as SPIR-V and
DXIL. Engine material schemas describe how those programs and their resources
are selected.

### Interchange

GLB/glTF remains the preferred general model interchange and compatibility
input. Blender remains an editing and review surface. OpenUSD may be evaluated
later if multi-application scene composition requires it. These formats do not
replace cooked Phlosion semantics for prefabs, proprietary materials,
animation events, collision, streaming, or gameplay composition.

## Custom Resource Semantics

### `.phlo`

A `.phlo` is a high-level assembled object. A standalone texture or animation
is not called a `.phlo`.

Example:

```text
bulbasaur.phlo
  root type: CharacterPrefab
  dependencies:
    bulbasaur_body.phmesh
    bulbasaur.phskel
    bulbasaur_locomotion.phanim
    bulbasaur_combat.phanim
    bulbasaur_skin.phmat
    bulbasaur_diffuse.ktx2
    bulbasaur_normal.ktx2
    bulbasaur_cry.opus
  bindings:
    mesh -> skeleton
    submesh -> material
    animation set -> compatible skeleton
  object data:
    stable sockets
    default pose and animation bindings
    LOD policy
    material overrides
```

Gameplay values do not belong in the visual object merely because the object
is used by one game. Pokemon species statistics, car handling, and weapon
damage remain game-owned configuration that references `.phlo` asset IDs.

### Low-level Phlosion resources

The low-level extensions are distinct PHRC root schemas, not independently
invented serialization systems. They share:

- a versioned little-endian header;
- root type and root schema version;
- content hash and cooker identity;
- target platform and render-profile identity;
- dependency records expressed as stable asset IDs;
- typed, aligned binary chunks;
- bounded reads and validation;
- optional compression metadata;
- deterministic inspection output.

Private resource data may eventually be embedded in a `.phlo` as a cooker
optimization. Logically it remains a typed resource. Shared resources remain
addressable dependencies so vault construction can deduplicate them.

Loose development model textures use the canonical
`content/phlosion/dependencies/ktx2/` store. A PHMAT dependency ID has the
form `dependencies/ktx2/<content-fnv1a64>-<semantic-fnv1a64>.ktx2`. The first
hash identifies the final encoded KTX2 bytes. The second covers the target
profile, usage role, transfer function, dimensions, sampler state, and
material mode/flags. Thus compatible normal/shiny/sex references share one
immutable file, while byte-matching textures with incompatible interpretation
remain separate identities. Full sampler and material values remain in the
PHMAT contract at each reference site.

The cooker publishes an immutable dependency through a verified partial file,
builds the owning object in a sibling staging directory, verifies its complete
texture graph, and only then swaps the object directory. An interrupted cook
can leave at most an unreferenced immutable payload or partial; successful
manifest finalization prunes both against its exact shared-dependency inventory.
Legacy object-relative `textures/` references remain readable during migration,
but current manifest publication rejects them.

### `.phscene`

A scene owns spatial and environment composition:

- object and resource placement;
- hierarchy and transforms;
- lighting and environment settings;
- terrain and vegetation composition;
- collision and navigation references;
- streaming regions;
- declared board or gameplay layout deltas.

A `.phscene` references `.phlo` objects rather than reinterpreting their
private components. The LGPE fidelity contract continues to govern source
environment changes.

Route 1 is the first strict runtime proof. The host asset store is consulted
only for `content/phlosion/scenes/route1.phscene`; the mounted archive's virtual
store owns every canonical scene dependency thereafter. Missing or invalid
PHSC data is an actionable load failure and never falls back to a loose LGPE
cache or a retired environment GLB. Project-authored board and scene deltas are
applied as a separate layer when present.

### `.phv`

A vault is a mountable distribution archive. It provides an indexed mapping
from stable asset IDs to cooked records and supports content hashing,
deduplication, alignment, compression, dependency auditing, and future patch
metadata. It may store standardized payloads such as KTX2 and Opus without
rewriting their internal formats.

Development can load loose resources while shipping builds mount vaults. The
gameplay-facing asset ID remains unchanged:

```text
characters/bulbasaur

development -> loose cooked resources
shipping    -> a record mounted from pokemon_common.phv
```

## Importer Rules

Every importer targets the same canonical engine concepts where the source
semantics agree:

```text
GFBMDL -------+
TRMDL/TRMSH --+
GLB ----------+--> Mesh IR --> .phmesh

GFBANM -------+
TRANM --------+
GLB animation +--> Animation IR --> .phanim

BNTX ---------+
PNG/EXR ------+--> Texture IR --> KTX2
```

Source-specific information must not be discarded to make importers look
artificially identical. Recognized Game Freak shader behavior becomes
portable engine material-family semantics. Unknown or unresolved proprietary
data remains attached to canonical provenance until understood.

## Runtime Rules

- Gameplay addresses resources by stable asset ID, not physical extension.
- Import, decoding, and cooking occur offline or in explicit development
  tooling, never implicitly in the shipping frame loop.
- Runtime files are content-hash invalidated. Source modification times alone
  are insufficient.
- Each schema has independent evolution and a testable compatibility policy.
- A failed or incompatible cook produces a clear diagnostic; it does not fall
  back silently to guessed behavior.
- OpenGL, Vulkan, and D3D12 consume the same engine semantics.
- A new custom format or field requires a demonstrated engine-owned semantic,
  loading/streaming need, or measured runtime benefit.

## Migration

Migration is incremental and preserves working restore points:

1. Implement PHRC headers, bounded readers/writers, asset IDs, dependency
   records, inspection, and validation.
2. Add `.phlo` manifests and a compatibility path that can reference the
   existing GLB-derived model runtime while `.phmesh`, `.phskel`, `.phanim`,
   and `.phmat` cooks are implemented.
3. Split the current monolithic `.pacmdl` data into the new typed resources
   without losing geometry, skinning, animations, materials, or texture
   behavior.
4. Cook every configured Pokemon model and render them from `.phlo` asset IDs
   in gameplay.
5. Express canonical Route 1 as `.phscene` plus referenced Phlosion resources
   while retaining its direct-source hashes, material semantics, placement
   manifests, board-layout delta, motion, and projected lighting.
6. Render Route 1 and Pokemon together in the gameplay window with no
   `.pacmdl`, GLB, or provisional LGPE cache read after cook completion.
7. Add `.phv` construction and prove that loose and vault-mounted assets
   produce identical runtime resource hashes and renderer output.
8. Remove compatibility readers only after qualification and documented
   migration.

## Current Milestone

The active vertical-slice target is:

> All configured Pokemon and the complete canonical Route 1 environment are
> cooked into the Phlosion format family and rendered together in the gameplay
> window through the new runtime path.

Acceptance requires:

- every configured Pokemon resolves through a `.phlo`;
- their geometry, skeleton, animation, material, and texture behavior remains
  equivalent to the promoted gameplay baseline;
- Route 1 resolves through a `.phscene`;
- canonical source environment geometry, materials, textures, placements,
  encounter grass, animation, projected cloud, projected shadow, and board
  registration remain equivalent to restore point `27f0f36`;
- the runtime proof performs no source GLB or proprietary LGPE cache parsing
  after the required Phlosion resources have been cooked;
- loose-resource loading is proven first; `.phv` mounting may follow as a
  separately qualified packaging pass;
- the complete test suite and renderer contract pass;
- at least one fixed gameplay capture is visually reviewed;
- migration status, source hashes, cooked hashes, and remaining compatibility
  boundaries are documented honestly.

This milestone does not require every future engine resource type to be
finished. It does require the schemas and loader boundaries to be general
enough that a car, shooter weapon, cyberpunk character, or original environment
would use the same resource family rather than a new game-specific format.
