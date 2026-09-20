# Licensing scope

Status: Active
Type: Reference
Last updated: 2026-09-20

## Original code

[The Apache License 2.0](../LICENSE) applies to original Pokemon Autochess game code,
scripts, tools, tests, build configuration and text documentation authored by
Adam Wentworth, except for the excluded material below. Preserve the required
copyright, licence and notice information when redistributing covered code.

The licence is a grant for the author's original work. It does not assert
ownership of third-party material or grant rights the author does not hold.
Code ownership in the architecture docs describes repository responsibility;
it does not change the copyright or licensing of recovered source material.

## Excluded material

| Material | Location / examples | Scope |
| --- | --- | --- |
| Recovered field shaders and CPU reference implementations | `src/game/render/materials/field/` | The entire directory is excluded from the Apache 2.0 grant. See [field provenance](FIELD_MATERIALS.md). |
| Recovered character, eye, lighting and layered-effect programs | `src/game/render/materials/character/` | The entire directory is excluded from the Apache 2.0 grant. See [character provenance](CHARACTER_MATERIALS.md). |
| Source-specific material profiles and shader fixtures | `config/render/world_materials.json`, `tests/materials/` | These paths, and recovered expressions/data in other fixtures, are excluded. |
| Source-game captures and extracted data | `docs/vfx/captures/`, including buffer and mesh CSV data | The capture directory is excluded, as are source-derived data embedded elsewhere. |
| Artwork, branding and showcase media | `docs/assets/`, `docs/art/`, screenshots, GIFs and logos | These assets are excluded; the code licence does not grant rights to the depicted Pokemon artwork or branding. |
| Runtime asset payloads | Private `assets/`, `content/phlosion/`, cooked models, textures, animations, audio and environment payloads | Outside the Apache 2.0 grant, whether restored locally or referenced by tracked manifests. |

The exclusion also covers other recovered, copied or source-derived third-party
implementations and data, wherever stored, and generated outputs containing
that material. This list identifies the principal locations; a renamed file,
source-neutral format or compiled shader does not change its underlying rights.
No separate licence for these excluded materials is granted by this repository.

Pokemon names, characters and trademarks retain their respective owners' rights.
The code licence does not grant trademark rights or permission to redistribute
the private asset depot. The project is not affiliated with Nintendo, Game Freak
or The Pokemon Company.

## Dependencies

Phlosion Engine, Phlosion VFX and other dependencies retain their own licence
terms and notices; this repository does not relicense them. Refer to their
repositories and the dependencies pinned by `vcpkg.json`. Installed vcpkg
packages provide their notices under `share/<port>/copyright`.

Preserve applicable dependency notices when distributing them. The Apache 2.0
licence on original game code does not make every component of a built game or
content bundle Apache-licensed.
