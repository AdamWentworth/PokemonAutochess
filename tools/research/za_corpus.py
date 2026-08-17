"""Resolve the canonical Z-A model stems from the asset catalog.

Research tools used to duplicate a 52-stem ``include_stems`` assumption.  The
catalog now intentionally selects every output from the canonical recipe, so
all Z-A analyzers must derive the same corpus from that recipe instead of
silently retaining the former subset.
"""

from __future__ import annotations

import json
import pathlib
from typing import Any


ZA_RECIPE = "tools/assets/gamefreak_pokemon_imports_za.json"


def _read_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def selected_za_stems(
        game_root: pathlib.Path,
        catalog: dict[str, Any] | None = None) -> list[str]:
    root = game_root.resolve()
    if catalog is None:
        catalog = _read_json(root / "config/assets/asset_catalog.json")
    rows = [
        row for row in catalog.get("native_import_sets", [])
        if row.get("recipe") == ZA_RECIPE
    ]
    if len(rows) != 1:
        raise ValueError("Canonical Z-A catalog entry is missing or duplicated")
    row = rows[0]
    selection = row.get("selection")
    if selection == "include_stems":
        stems = [str(value) for value in row.get("stems", [])]
    elif selection == "all_outputs":
        recipe = _read_json(root.joinpath(*pathlib.PurePosixPath(ZA_RECIPE).parts))
        stems = [
            str(output["stem"])
            for import_row in recipe.get("imports", [])
            for output in import_row.get("outputs", [])
        ]
    else:
        raise ValueError(f"Unsupported canonical Z-A selection: {selection}")
    if not stems or len(stems) != len(set(stems)):
        raise ValueError("Canonical Z-A stem set is empty or duplicated")
    return stems
