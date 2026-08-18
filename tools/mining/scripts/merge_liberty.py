#!/usr/bin/env python3
"""Merge one-cell Liberty files into a single Liberty library."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


_CELL_START = re.compile(r"(?m)^\s*cell\s*\([^)]*\)\s*\{")
_HEADER_BLOCK = re.compile(
    r"(?m)^\s*(lu_table_template|power_lut_template|normalized_driver_waveform)\s*\(([^)]*)\)\s*\{"
)


def _matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unbalanced Liberty braces")


def _library_parts(path: Path) -> tuple[str, list[str]]:
    text = path.read_text()
    library = text.find("library")
    opening = text.find("{", library)
    if library < 0 or opening < 0:
        raise ValueError(f"{path}: missing library block")
    closing = _matching_brace(text, opening)
    body = text[opening + 1:closing]
    first_cell_match = _CELL_START.search(body)
    if first_cell_match is None:
        raise ValueError(f"{path}: missing cell block")
    header = body[:first_cell_match.start()]
    cells: list[str] = []
    cursor = first_cell_match.start()
    while cursor < len(body):
        cell_match = _CELL_START.match(body, cursor)
        if cell_match is None:
            if body[cursor:].strip():
                raise ValueError(f"{path}: unexpected text after cell block")
            break
        cell_opening = body.find("{", cell_match.start(), cell_match.end())
        cell_closing = _matching_brace(body, cell_opening)
        cells.append(body[cell_match.start():cell_closing + 1])
        cursor = cell_closing + 1
    return header, cells


def _normalized_header(header: str) -> str:
    header = re.sub(r"/\*.*?\*/\s*", "", header, flags=re.S)
    return re.sub(r'^\s*date\s*:\s*"[^"]*";\s*\n', "", header, flags=re.M)


def _header_blocks(header: str) -> list[tuple[int, int, str, str]]:
    blocks = []
    for match in _HEADER_BLOCK.finditer(header):
        opening = header.find("{", match.start(), match.end())
        closing = _matching_brace(header, opening)
        blocks.append((match.start(), closing + 1, match.group(1), match.group(2).strip()))
    return blocks


def _namespace_variant(header: str, cells: list[str], variant: int) -> tuple[str, list[str]]:
    names = {
        name for _, _, _, name in _header_blocks(header)
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name)
    }
    replacements = {name: f"{name}_v{variant}" for name in names}
    for name, replacement in replacements.items():
        header = re.sub(rf"\b{re.escape(name)}\b", replacement, header)
    return header, [
        re.sub(
            "|".join(rf"\b{re.escape(name)}\b" for name in replacements),
            lambda match: replacements[match.group(0)],
            cell,
        ) if replacements else cell
        for cell in cells
    ]


def _supplemental_blocks(header: str) -> str:
    return "\n".join(header[start:end] for start, end, _, _ in _header_blocks(header))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    files = sorted(args.lib_dir.glob("*.lib"))
    if not files:
        raise ValueError(f"no Liberty files found in {args.lib_dir}")
    grouped: dict[str, tuple[str, list[str]]] = {}
    for path in files:
        header, cells = _library_parts(path)
        key = _normalized_header(header)
        if key not in grouped:
            grouped[key] = (header, [])
        grouped[key][1].extend(cells)

    variants = list(grouped.values())
    merged_header, merged_cells = _namespace_variant(*variants[0], 0)
    for variant, (header, cells) in enumerate(variants[1:], 1):
        namespaced_header, namespaced_cells = _namespace_variant(header, cells, variant)
        merged_header += "\n" + _supplemental_blocks(namespaced_header)
        if "voltage_map (GND, 0);" in namespaced_header and "voltage_map (GND, 0);" not in merged_header:
            merged_header += "\n  voltage_map (GND, 0);"
        merged_cells.extend(namespaced_cells)
    args.out.write_text(
        "library (SO3_merged) {\n" + merged_header + "\n" + "\n\n".join(merged_cells) + "\n}\n"
    )


if __name__ == "__main__":
    main()
