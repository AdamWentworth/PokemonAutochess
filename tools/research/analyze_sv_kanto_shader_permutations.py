#!/usr/bin/env python3
"""Inventory and resolve selected Kanto Scarlet/Violet material programs.

The inventory is derived from the canonical asset catalog and native model IR.
When a private shader-study directory is supplied, decoded Trinity metadata is
used to select exact BNSH variations. The report retains identities, packed
keys, and option provenance; it never copies proprietary shader payloads.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import sys
from typing import Any


SOURCE_PROFILE = "pokemon-scarlet-v3.0.1"
REPORT_SCHEMA = "pokemon-autochess-sv-kanto-shader-inventory-v1"
REGISTRY_SCHEMA = "pokemon-autochess-sv-shader-source-registry-v1"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_project_path(value: str) -> pathlib.PurePosixPath:
    candidate = pathlib.PurePosixPath(value.replace("\\", "/"))
    if candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError(f"Project path must be relative and contained: {value}")
    return candidate


def project_path(root: pathlib.Path, value: str) -> pathlib.Path:
    relative = normalize_project_path(value)
    return root.joinpath(*relative.parts)


def selected_sv_models(game_root: pathlib.Path) -> list[dict[str, Any]]:
    catalog = read_json(game_root / "config" / "assets" / "asset_catalog.json")
    if catalog.get("schema_version") != 1 or catalog.get("kind") != "pokemon_autochess_asset_catalog":
        raise ValueError("Unsupported asset catalog schema")

    selected: list[dict[str, Any]] = []
    for import_set in catalog.get("native_import_sets", []):
        recipe_identity = str(import_set["recipe"]).replace("\\", "/")
        recipe = read_json(project_path(game_root, recipe_identity))
        if recipe.get("sourceGame") != SOURCE_PROFILE:
            continue
        selection = import_set.get("selection")
        if selection not in ("all_outputs", "include_stems"):
            raise ValueError(f"Unsupported native import selection: {selection}")
        allowed = {str(value) for value in import_set.get("stems", [])}
        for imported in recipe.get("imports", []):
            species_id = int(imported["speciesId"])
            if species_id > 151:
                continue
            for output in imported.get("outputs", []):
                stem = str(output["stem"])
                if selection == "include_stems" and stem not in allowed:
                    continue
                selected.append(
                    {
                        "species_id": species_id,
                        "species_name": str(imported["speciesName"]),
                        "gender_label": str(imported.get("genderLabel", "default")),
                        "appearance": str(output["appearance"]),
                        "stem": stem,
                        "recipe": recipe_identity,
                    }
                )

    counts = collections.Counter(row["stem"] for row in selected)
    duplicates = sorted(stem for stem, count in counts.items() if count != 1)
    if duplicates:
        raise ValueError(f"Selected model stems are not unique: {', '.join(duplicates)}")
    if not selected:
        raise ValueError("The asset catalog selected no Scarlet/Violet Kanto models")
    return sorted(selected, key=lambda row: (row["species_id"], row["stem"]))


def bool_string(value: Any) -> str:
    return "True" if bool(value) else "False"


def permutation_signature(material: dict[str, Any]) -> str:
    options = sorted(
        f"{name}={value}" for name, value in material.get("shader_options", {}).items()
    )
    roles = sorted(
        f"{texture['role']}@{int(texture['slot'])}"
        for texture in material.get("textures", [])
    )
    return "|".join(
        (
            f"family={material.get('shader_family', '')}",
            f"transparent={bool_string(material.get('is_transparent', False))}",
            f"options={';'.join(options)}",
            f"roles={';'.join(roles)}",
        )
    )


def material_parameter_schema(material: dict[str, Any]) -> dict[str, list[str]]:
    return {
        "float": sorted(material.get("float_parameters", {}).keys()),
        "vec2": sorted(material.get("vec2_parameters", {}).keys()),
        "vec3": sorted(material.get("vec3_parameters", {}).keys()),
        "vec4": sorted(material.get("vec4_parameters", {}).keys()),
    }


def build_permutations(
    game_root: pathlib.Path, selections: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    groups: dict[str, dict[str, Any]] = {}
    model_count = 0
    material_count = 0
    species: set[int] = set()

    for selection in selections:
        manifest_identity = f"assets/models/{selection['stem']}.phmodel"
        manifest_path = project_path(game_root, manifest_identity)
        if not manifest_path.is_file():
            raise FileNotFoundError(manifest_path)
        document = read_json(manifest_path)
        if document.get("schema") != "phlosion-native-model-ir-v1" or document.get("schema_version") != 1:
            raise ValueError(f"Unsupported native model manifest: {manifest_identity}")
        if document.get("source", {}).get("profile") != SOURCE_PROFILE:
            raise ValueError(f"Source profile mismatch: {manifest_identity}")

        model_count += 1
        species.add(selection["species_id"])
        for material in document.get("materials", []):
            material_count += 1
            signature = permutation_signature(material)
            digest = hashlib.sha256(signature.encode("utf-8")).hexdigest()
            occurrence = {
                "species_id": selection["species_id"],
                "species_name": selection["species_name"],
                "model": selection["stem"],
                "appearance": selection["appearance"],
                "gender_label": selection["gender_label"],
                "material": str(material.get("name", "")),
            }
            if digest not in groups:
                groups[digest] = {
                    "permutation_sha256": digest,
                    "signature": signature,
                    "shader_family": str(material.get("shader_family", "")),
                    "transparent": bool(material.get("is_transparent", False)),
                    "shader_options": {
                        name: str(value)
                        for name, value in sorted(material.get("shader_options", {}).items())
                    },
                    "texture_bindings": sorted(
                        (
                            {
                                "role": str(texture["role"]),
                                "slot": int(texture["slot"]),
                            }
                            for texture in material.get("textures", [])
                        ),
                        key=lambda row: (row["role"], row["slot"]),
                    ),
                    "parameter_schema": material_parameter_schema(material),
                    "occurrences": [],
                }
            else:
                schema = groups[digest]["parameter_schema"]
                occurrence_schema = material_parameter_schema(material)
                for kind in schema:
                    schema[kind] = sorted(set(schema[kind]) | set(occurrence_schema[kind]))
            groups[digest]["occurrences"].append(occurrence)

    result: list[dict[str, Any]] = []
    for digest in sorted(groups):
        group = groups[digest]
        occurrences = sorted(
            group.pop("occurrences"),
            key=lambda row: (row["species_id"], row["model"], row["material"]),
        )
        group["material_count"] = len(occurrences)
        group["model_count"] = len({row["model"] for row in occurrences})
        group["species_count"] = len({row["species_id"] for row in occurrences})
        group["examples"] = occurrences[:8]
        result.append(group)

    summary = {
        "selected_species": len(species),
        "selected_models": model_count,
        "selected_materials": material_count,
        "material_permutations": len(result),
        "shader_families": len({row["shader_family"] for row in result}),
    }
    return result, summary


def trailing_zeroes(mask: int) -> int:
    if mask <= 0:
        return 0
    return (mask & -mask).bit_length() - 1


def encode_options(
    material_options: dict[str, str], slots: list[dict[str, Any]]
) -> tuple[int, list[dict[str, Any]]]:
    key = 0
    selections: list[dict[str, Any]] = []
    for slot in slots:
        name = str(slot["slot_name"])
        values = slot["slot_values"]
        selected_index = int(slot["bool1"])
        selection_source = "shader_metadata_default"
        requested = material_options.get(name)
        if requested is not None:
            requested_string = str(requested).lower()
            matches = [
                index
                for index, value in enumerate(values)
                if str(value["string_value"]).lower() == requested_string
                or str(value["u_int_value"]).lower() == requested_string
            ]
            if not matches:
                raise ValueError(f"Unsupported {name}={requested} for shader metadata")
            selected_index = matches[0]
            selection_source = "material_document"
        if selected_index < 0 or selected_index >= len(values):
            raise ValueError(f"Invalid metadata default index for {name}")
        mask = int(slot["offset"])
        encoded = selected_index << trailing_zeroes(mask)
        if encoded & ~mask:
            raise ValueError(f"Selection exceeds packed mask for {name}")
        key |= encoded
        selections.append(
            {
                "name": name,
                "choice": str(values[selected_index]["string_value"]),
                "choice_value": int(values[selected_index]["u_int_value"]),
                "choice_index": selected_index,
                "mask_hex": f"0x{mask:X}",
                "encoded_hex": f"0x{encoded:X}",
                "selection_source": selection_source,
            }
        )
    return key, selections


def source_file_state(root: pathlib.Path | None, identity: str) -> dict[str, Any]:
    result: dict[str, Any] = {"identity": identity, "staged": False}
    if root is None:
        return result
    path = root / identity
    if path.is_file():
        result.update(
            {
                "staged": True,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    return result


def resolve_family(
    family: str,
    permutations: list[dict[str, Any]],
    metadata: dict[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    param_buffer = metadata.get("param_buffer", [])
    if len(param_buffer) % 2:
        raise ValueError(f"{family} metadata param_buffer has an odd length")
    pairs = list(zip(param_buffer[::2], param_buffer[1::2]))
    declared = {
        str(slot["slot_name"])
        for slot in metadata.get("shader_param", []) + metadata.get("global_param", [])
    }
    resolved: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []
    for permutation in permutations:
        try:
            options = permutation["shader_options"]
            shader_key, shader_options = encode_options(options, metadata.get("shader_param", []))
            global_key, global_options = encode_options(options, metadata.get("global_param", []))
            variation_indices = [
                index
                for index, pair in enumerate(pairs)
                if int(pair[0]) == shader_key and int(pair[1]) == global_key
            ]
            if len(variation_indices) != 1:
                raise ValueError(
                    f"packed key pair selected {len(variation_indices)} variations"
                )
            resolved.append(
                {
                    "permutation_sha256": permutation["permutation_sha256"],
                    "material_count": permutation["material_count"],
                    "shader_key_hex": f"0x{shader_key:X}",
                    "global_key_hex": f"0x{global_key:X}",
                    "variation_index": variation_indices[0],
                    "shader_options": shader_options,
                    "global_options": global_options,
                    "undeclared_material_options": sorted(set(options) - declared),
                }
            )
        except (KeyError, TypeError, ValueError) as error:
            failures.append(
                {
                    "permutation_sha256": permutation["permutation_sha256"],
                    "material_count": permutation["material_count"],
                    "reason": str(error),
                }
            )
    return resolved, failures


def build_shader_sources(
    registry: dict[str, Any],
    permutations: list[dict[str, Any]],
    study_root: pathlib.Path | None,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, int]]:
    by_family: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    for permutation in permutations:
        by_family[permutation["shader_family"]].append(permutation)

    registry_families = {
        str(row["shader_family"]): row for row in registry.get("families", [])
    }
    unknown = sorted(set(by_family) - set(registry_families))
    if unknown:
        raise ValueError(f"Shader source registry is missing families: {', '.join(unknown)}")

    sources: list[dict[str, Any]] = []
    extraction_queue: list[dict[str, Any]] = []
    resolved_permutations = 0
    resolved_materials = 0
    unique_programs: set[tuple[str, int]] = set()

    for family in sorted(by_family):
        configuration = registry_families[family]
        archive = source_file_state(study_root, configuration["archive"]["file"])
        metadata_binary = source_file_state(study_root, configuration["metadata"]["file"])
        metadata_json = source_file_state(study_root, configuration["metadata"]["decoded_file"])
        source = {
            "shader_family": family,
            "material_count": sum(row["material_count"] for row in by_family[family]),
            "permutation_count": len(by_family[family]),
            "archive": {
                **archive,
                "romfs_path": configuration["archive"]["romfs_path"],
                "romfs_hash": configuration["archive"]["romfs_hash"],
            },
            "metadata": {
                **metadata_binary,
                "romfs_path": configuration["metadata"]["romfs_path"],
                "romfs_hash": configuration["metadata"]["romfs_hash"],
            },
            "decoded_metadata": metadata_json,
            "resolution_status": "source_metadata_not_staged",
            "resolved_permutations": [],
            "resolution_failures": [],
        }
        for source_kind, state, source_info in (
            ("shader_archive", archive, configuration["archive"]),
            ("shader_metadata", metadata_binary, configuration["metadata"]),
        ):
            if not state["staged"]:
                extraction_queue.append(
                    {
                        "shader_family": family,
                        "kind": source_kind,
                        "output_file": source_info["file"],
                        "romfs_path": source_info["romfs_path"],
                        "romfs_hash": source_info["romfs_hash"],
                    }
                )
        if metadata_binary["staged"] and not metadata_json["staged"]:
            extraction_queue.append(
                {
                    "shader_family": family,
                    "kind": "decode_shader_metadata",
                    "input_file": configuration["metadata"]["file"],
                    "output_file": configuration["metadata"]["decoded_file"],
                }
            )

        if metadata_json["staged"]:
            metadata = read_json(study_root / configuration["metadata"]["decoded_file"])
            if metadata.get("file_name") != configuration["archive"]["file"]:
                raise ValueError(
                    f"{family} decoded metadata names {metadata.get('file_name')}, "
                    f"expected {configuration['archive']['file']}"
                )
            resolved, failures = resolve_family(family, by_family[family], metadata)
            source["resolved_permutations"] = resolved
            source["resolution_failures"] = failures
            source["metadata_variation_count"] = len(metadata.get("param_buffer", [])) // 2
            source["resolution_status"] = "exact" if not failures else "incomplete"
            resolved_permutations += len(resolved)
            resolved_materials += sum(row["material_count"] for row in resolved)
            unique_programs.update((family, row["variation_index"]) for row in resolved)
        sources.append(source)

    coverage = {
        "source_families_staged": sum(
            1
            for source in sources
            if source["archive"]["staged"]
            and source["metadata"]["staged"]
            and source["decoded_metadata"]["staged"]
        ),
        "exactly_resolved_permutations": resolved_permutations,
        "exactly_resolved_materials": resolved_materials,
        "unique_selected_programs": len(unique_programs),
    }
    return sources, extraction_queue, coverage


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2])
    parser.add_argument("--registry", type=pathlib.Path)
    parser.add_argument("--shader-study", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--require-complete-source", action="store_true")
    parser.add_argument("--require-exact-resolution", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    game_root = args.game_root.resolve()
    registry_path = (
        args.registry.resolve()
        if args.registry
        else pathlib.Path(__file__).with_name("sv_kanto_shader_families.json")
    )
    registry = read_json(registry_path)
    if registry.get("schema") != REGISTRY_SCHEMA or registry.get("source_profile") != SOURCE_PROFILE:
        raise ValueError("Unsupported Scarlet/Violet shader source registry")
    study_root = args.shader_study.resolve() if args.shader_study else None
    if study_root is not None and not study_root.is_dir():
        raise FileNotFoundError(study_root)

    selections = selected_sv_models(game_root)
    permutations, summary = build_permutations(game_root, selections)
    sources, extraction_queue, coverage = build_shader_sources(
        registry, permutations, study_root
    )
    summary.update(coverage)
    summary["source_families_total"] = len(sources)
    summary["unresolved_permutations"] = (
        summary["material_permutations"] - coverage["exactly_resolved_permutations"]
    )
    summary["unresolved_materials"] = (
        summary["selected_materials"] - coverage["exactly_resolved_materials"]
    )

    report = {
        "schema": REPORT_SCHEMA,
        "scope": {
            "source_profile": SOURCE_PROFILE,
            "pokedex_max": 151,
            "selection_authority": "config/assets/asset_catalog.json",
            "material_authority": "selected phlosion-native-model-ir-v1 manifests",
        },
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "selection_rule": (
                "Material and global option names present in Trinity metadata use "
                "the retained material value; omitted options use metadata bool1 as "
                "the source default choice index. Packed shader/global keys must map "
                "to exactly one param_buffer variation."
            ),
            "permutation_rule": (
                "SHA-256 of shader family, transparency, sorted shader option "
                "name/value pairs, and sorted source texture role/slot pairs."
            ),
        },
        "registry": {
            "identity": registry_path.name,
            "sha256": sha256_file(registry_path),
            "source_index": registry.get("source_index"),
        },
        "summary": summary,
        "families": sources,
        "material_permutations": permutations,
        "extraction_queue": extraction_queue,
    }

    if args.require_complete_source and coverage["source_families_staged"] != len(sources):
        raise RuntimeError(
            f"Only {coverage['source_families_staged']} of {len(sources)} source families are staged"
        )
    if args.require_exact_resolution and summary["unresolved_permutations"]:
        raise RuntimeError(
            f"{summary['unresolved_permutations']} material permutations remain unresolved"
        )

    payload = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(payload, encoding="utf-8", newline="\n")
        print(f"[SvKantoShaderInventory] wrote {output}")
    else:
        sys.stdout.write(payload)
    print(
        "[SvKantoShaderInventory] "
        f"models={summary['selected_models']} materials={summary['selected_materials']} "
        f"permutations={summary['material_permutations']} "
        f"resolved={summary['exactly_resolved_permutations']} "
        f"families={summary['source_families_staged']}/{summary['source_families_total']}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(f"[SvKantoShaderInventory] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
