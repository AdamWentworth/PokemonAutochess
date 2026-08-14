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
import struct
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


def option_word_count(slots: list[dict[str, Any]]) -> int:
    word_count = 1
    previous_slot_index = -1
    for slot in slots:
        slot_index = int(slot["slot_index"])
        if previous_slot_index >= 0 and slot_index <= previous_slot_index:
            word_count += 1
        previous_slot_index = slot_index
    return word_count


def encode_options(
    material_options: dict[str, str], slots: list[dict[str, Any]]
) -> tuple[list[int], list[dict[str, Any]]]:
    keys = [0] * option_word_count(slots)
    selections: list[dict[str, Any]] = []
    word_index = 0
    previous_slot_index = -1
    for slot in slots:
        name = str(slot["slot_name"])
        slot_index = int(slot["slot_index"])
        if previous_slot_index >= 0 and slot_index <= previous_slot_index:
            word_index += 1
        previous_slot_index = slot_index
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
        keys[word_index] |= encoded
        selections.append(
            {
                "name": name,
                "word_index": word_index,
                "choice": str(values[selected_index]["string_value"]),
                "choice_value": int(values[selected_index]["u_int_value"]),
                "choice_index": selected_index,
                "mask_hex": f"0x{mask:X}",
                "encoded_hex": f"0x{encoded:X}",
                "selection_source": selection_source,
            }
        )
    return keys, selections


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


def bnsh_variation_count(path: pathlib.Path) -> int:
    payload = path.read_bytes()
    grsc_offset = payload.find(b"grsc", 0, min(len(payload), 0x100))
    count_offset = grsc_offset + 0x1C
    if grsc_offset < 0 or count_offset + 4 > len(payload):
        raise ValueError(f"BNSH grsc variation header is missing: {path.name}")
    count = struct.unpack_from("<I", payload, count_offset)[0]
    if count <= 0:
        raise ValueError(f"BNSH variation table is empty: {path.name}")
    return count


def resolve_family(
    family: str,
    permutations: list[dict[str, Any]],
    metadata: dict[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], int, int]:
    param_buffer = metadata.get("param_buffer", [])
    shader_word_count = option_word_count(metadata.get("shader_param", []))
    global_word_count = option_word_count(metadata.get("global_param", []))
    words_per_variation = shader_word_count + global_word_count
    if not param_buffer or len(param_buffer) % words_per_variation:
        raise ValueError(
            f"{family} metadata param_buffer is not divisible by its "
            f"{words_per_variation}-word ABI"
        )
    variation_words = [
        tuple(int(value) for value in param_buffer[offset : offset + words_per_variation])
        for offset in range(0, len(param_buffer), words_per_variation)
    ]
    declared = {
        str(slot["slot_name"])
        for slot in metadata.get("shader_param", []) + metadata.get("global_param", [])
    }
    resolved: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []
    for permutation in permutations:
        try:
            options = permutation["shader_options"]
            shader_keys, shader_options = encode_options(options, metadata.get("shader_param", []))
            global_keys, global_options = encode_options(options, metadata.get("global_param", []))
            selected_words = tuple(shader_keys + global_keys)
            variation_indices = [
                index for index, words in enumerate(variation_words) if words == selected_words
            ]
            if len(variation_indices) != 1:
                raise ValueError(
                    f"packed key pair selected {len(variation_indices)} variations"
                )
            resolved.append(
                {
                    "permutation_sha256": permutation["permutation_sha256"],
                    "material_count": permutation["material_count"],
                    "shader_key_words_hex": [f"0x{key:X}" for key in shader_keys],
                    "global_key_words_hex": [f"0x{key:X}" for key in global_keys],
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
    return resolved, failures, len(variation_words), words_per_variation


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
    sources: list[dict[str, Any]] = []
    extraction_queue: list[dict[str, Any]] = []
    resolved_permutations = 0
    resolved_materials = 0
    unique_programs: set[tuple[str, int]] = set()

    for family in sorted(by_family):
        configuration = registry_families.get(family)
        if configuration is None:
            material_count = sum(
                row["material_count"] for row in by_family[family])
            failures = [
                {
                    "permutation_sha256": row["permutation_sha256"],
                    "material_count": row["material_count"],
                    "reason": "unregistered_shader_family",
                }
                for row in by_family[family]
            ]
            sources.append({
                "shader_family": family,
                "material_count": material_count,
                "permutation_count": len(by_family[family]),
                "archive": {
                    "identity": None,
                    "staged": False,
                    "romfs_path": None,
                    "romfs_hash": None,
                },
                "metadata": {
                    "identity": None,
                    "staged": False,
                    "romfs_path": None,
                    "romfs_hash": None,
                },
                "decoded_metadata": {
                    "identity": None,
                    "staged": False,
                },
                "resolution_status": "unregistered_shader_family",
                "resolved_permutations": [],
                "resolution_failures": failures,
            })
            extraction_queue.append({
                "shader_family": family,
                "kind": "register_shader_source",
                "reason": (
                    "The selected material corpus contains this family, but "
                    "its BNSH/TRSHA source identities are not yet registered."
                ),
            })
            continue
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
            resolved, failures, variation_count, words_per_variation = resolve_family(
                family, by_family[family], metadata
            )
            if archive["staged"]:
                archive_variations = bnsh_variation_count(
                    study_root / configuration["archive"]["file"]
                )
                if archive_variations != variation_count:
                    raise ValueError(
                        f"{family} BNSH has {archive_variations} variations but "
                        f"metadata declares {variation_count}"
                    )
                source["archive_variation_count"] = archive_variations
            source["resolved_permutations"] = resolved
            source["resolution_failures"] = failures
            source["metadata_variation_count"] = variation_count
            source["parameter_words_per_variation"] = words_per_variation
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


def build_promoted_evidence(report: dict[str, Any]) -> dict[str, Any]:
    families: list[dict[str, Any]] = []
    for source in report["families"]:
        programs: dict[int, dict[str, Any]] = {}
        for resolved in source["resolved_permutations"]:
            variation_index = int(resolved["variation_index"])
            program = programs.setdefault(
                variation_index,
                {
                    "variation_index": variation_index,
                    "material_count": 0,
                    "permutations": [],
                },
            )
            program["material_count"] += int(resolved["material_count"])
            program["permutations"].append(
                {
                    "permutation_sha256": resolved["permutation_sha256"],
                    "material_count": int(resolved["material_count"]),
                    "shader_key_words_hex": resolved["shader_key_words_hex"],
                    "global_key_words_hex": resolved["global_key_words_hex"],
                }
            )
        for program in programs.values():
            program["permutations"].sort(key=lambda row: row["permutation_sha256"])
            program["permutation_count"] = len(program["permutations"])
        families.append(
            {
                "shader_family": source["shader_family"],
                "material_count": source["material_count"],
                "permutation_count": source["permutation_count"],
                "metadata_variation_count": source["metadata_variation_count"],
                "archive_variation_count": source["archive_variation_count"],
                "parameter_words_per_variation": source["parameter_words_per_variation"],
                "archive": {
                    "identity": source["archive"]["identity"],
                    "sha256": source["archive"]["sha256"],
                    "romfs_path": source["archive"]["romfs_path"],
                    "romfs_hash": source["archive"]["romfs_hash"],
                },
                "metadata": {
                    "identity": source["metadata"]["identity"],
                    "sha256": source["metadata"]["sha256"],
                    "decoded_sha256": source["decoded_metadata"]["sha256"],
                    "romfs_path": source["metadata"]["romfs_path"],
                    "romfs_hash": source["metadata"]["romfs_hash"],
                },
                "selected_programs": [programs[index] for index in sorted(programs)],
            }
        )
    return {
        "schema": "pokemon-autochess-sv-kanto-shader-evidence-v1",
        "scope": report["scope"],
        "method": report["method"],
        "registry": report["registry"],
        "summary": report["summary"],
        "families": families,
        "conclusions": [
            (
                "Every selected Scarlet/Violet Kanto material permutation maps "
                "to exactly one source BNSH variation."
            ),
            (
                "This proves program identity and packed option selection; it does "
                "not by itself prove every sampled resource, constant-buffer field, "
                "lighting input, blend state, or final-color equation."
            ),
        ],
    }


def build_material_census(report: dict[str, Any]) -> dict[str, Any]:
    families = []
    for source in report["families"]:
        resolved = source.get("resolved_permutations", [])
        failures = source.get("resolution_failures", [])
        families.append({
            "shader_family": source["shader_family"],
            "material_count": int(source["material_count"]),
            "permutation_count": int(source["permutation_count"]),
            "resolution_status": source["resolution_status"],
            "resolved_permutation_count": len(resolved),
            "resolved_material_count": sum(
                int(row["material_count"]) for row in resolved),
            "unresolved_permutation_count": len(failures),
            "unresolved_material_count": sum(
                int(row["material_count"]) for row in failures),
            "selected_variations": sorted({
                int(row["variation_index"])
                for row in resolved
            }),
        })
    return {
        "schema": "pokemon-autochess-sv-kanto-material-census-v1",
        "scope": report["scope"],
        "method": {
            **report["method"],
            "promotion_policy": (
                "A census may explicitly retain unresolved families. Exact "
                "shader evidence remains separately gated on complete source."
            ),
        },
        "registry": report["registry"],
        "summary": report["summary"],
        "families": families,
        "research_queue": report["extraction_queue"],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2])
    parser.add_argument("--registry", type=pathlib.Path)
    parser.add_argument("--shader-study", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--evidence-output", type=pathlib.Path)
    parser.add_argument("--census-output", type=pathlib.Path)
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
    if args.evidence_output:
        if summary["unresolved_permutations"] or coverage["source_families_staged"] != len(sources):
            raise RuntimeError("Refusing to promote incomplete SV Kanto shader evidence")
        evidence_output = args.evidence_output.resolve()
        evidence_output.parent.mkdir(parents=True, exist_ok=True)
        evidence_output.write_text(
            json.dumps(build_promoted_evidence(report), indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        print(f"[SvKantoShaderInventory] wrote evidence {evidence_output}")
    if args.census_output:
        census_output = args.census_output.resolve()
        census_output.parent.mkdir(parents=True, exist_ok=True)
        census_output.write_text(
            json.dumps(build_material_census(report), indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        print(f"[SvKantoShaderInventory] wrote census {census_output}")
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
