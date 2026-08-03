#!/usr/bin/env python3
"""Extract a built-in Codex pet atlas from app.asar and split it into GIFs."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

from PIL import Image


ASAR_CANDIDATES = [
    Path("/Applications/ChatGPT.app/Contents/Resources/app.asar"),
    Path("/Applications/Codex.app/Contents/Resources/app.asar"),
]
ANIMATIONS = [
    ("idle", [1680, 660, 660, 840, 840, 1920]),
    ("running-right", [120, 120, 120, 120, 120, 120, 120, 220]),
    ("running-left", [120, 120, 120, 120, 120, 120, 120, 220]),
    ("waving", [140, 140, 140, 280]),
    ("jumping", [140, 140, 140, 140, 280]),
    ("failed", [140, 140, 140, 140, 140, 140, 140, 240]),
    ("waiting", [150, 150, 150, 150, 150, 260]),
    ("running", [120, 120, 120, 120, 120, 220]),
    ("review", [150, 150, 150, 150, 150, 280]),
]
ATLAS_SIZES = {(1536, 1872): 1, (1536, 2288): 2}
CELL_SIZE = (192, 208)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract a Codex Desktop built-in pet WebP atlas and generate one GIF per state."
    )
    parser.add_argument("--pet", required=True, help="Built-in pet id, for example codex, hoots, rocky, or seedy.")
    parser.add_argument("--asar", type=Path, help="Path to app.asar. Defaults to the installed ChatGPT or Codex app.")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory where the atlas and GIFs are written.")
    return parser.parse_args()


def default_asar() -> Path:
    for candidate in ASAR_CANDIDATES:
        if candidate.is_file():
            return candidate
    raise ValueError("could not find ChatGPT.app or Codex.app in /Applications")


def load_asar(asar_path: Path) -> tuple[bytes, dict[str, Any], int]:
    data = asar_path.read_bytes()
    if len(data) < 16:
        raise ValueError(f"{asar_path} is too small to be an ASAR archive")

    header_size = struct.unpack_from("<I", data, 4)[0]
    json_size = struct.unpack_from("<I", data, 12)[0]
    header = json.loads(data[16 : 16 + json_size].decode("utf-8"))
    data_start = 8 + header_size
    return data, header, data_start


def walk_files(node: dict[str, Any], prefix: str = "") -> list[tuple[str, dict[str, Any]]]:
    files = node.get("files")
    if not files:
        return [(prefix, node)]

    results: list[tuple[str, dict[str, Any]]] = []
    for name, child in files.items():
        results.extend(walk_files(child, f"{prefix}/{name}"))
    return results


def find_pet_asset(header: dict[str, Any], pet: str) -> tuple[str, dict[str, Any]]:
    matches = [
        (path, meta)
        for path, meta in walk_files(header)
        if path.startswith("/webview/assets/")
        and path.endswith(".webp")
        and f"/{pet}-spritesheet-" in path
    ]
    if not matches:
        available = sorted(
            path.split("/")[-1].split("-spritesheet-")[0]
            for path, _meta in walk_files(header)
            if path.startswith("/webview/assets/") and "-spritesheet-" in path and path.endswith(".webp")
        )
        raise ValueError(f"no built-in pet named {pet!r}; available pets: {', '.join(available)}")
    if len(matches) > 1:
        raise ValueError(f"multiple assets matched {pet!r}: {[path for path, _meta in matches]}")
    return matches[0]


def extract_asset(data: bytes, data_start: int, meta: dict[str, Any], output_path: Path) -> None:
    offset = data_start + int(meta["offset"])
    size = int(meta["size"])
    output_path.write_bytes(data[offset : offset + size])


def write_state_gifs(atlas_path: Path, output_dir: Path, pet: str) -> None:
    atlas = Image.open(atlas_path).convert("RGBA")
    version = ATLAS_SIZES.get(atlas.size)
    if version is None:
        raise ValueError(f"unexpected atlas size {atlas.size}; expected one of {list(ATLAS_SIZES)}")
    print(f"sprite-sheet format: v{version}")

    cell_w, cell_h = CELL_SIZE
    for row, (state, durations) in enumerate(ANIMATIONS):
        frames: list[Image.Image] = []

        for col in range(len(durations)):
            frame = atlas.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))
            frames.append(frame)

        if not frames:
            raise ValueError(f"{state} row has no non-empty frames")

        gif_path = output_dir / f"{pet}-{state}.gif"
        frames[0].save(
            gif_path,
            save_all=True,
            append_images=frames[1:],
            duration=durations,
            loop=0,
            disposal=2,
        )
        print(f"{gif_path}: {len(frames)} frames, {sum(durations)} ms/cycle")


def main() -> None:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    asar_path = args.asar or default_asar()
    data, header, data_start = load_asar(asar_path)
    internal_path, meta = find_pet_asset(header, args.pet)
    atlas_path = args.output_dir / f"{args.pet}-spritesheet.webp"
    extract_asset(data, data_start, meta, atlas_path)
    print(f"{atlas_path}: extracted {internal_path}")

    write_state_gifs(atlas_path, args.output_dir, args.pet)


if __name__ == "__main__":
    main()
