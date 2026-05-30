#!/usr/bin/env python3
"""Extract a built-in Codex pet atlas from app.asar and split it into GIFs."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

from PIL import Image


DEFAULT_ASAR = Path("/Applications/Codex.app/Contents/Resources/app.asar")
DEFAULT_STATES = [
    "idle",
    "running-right",
    "running-left",
    "waving",
    "jumping",
    "failed",
    "waiting",
    "thinking",
    "review",
]
ATLAS_SIZE = (1536, 1872)
CELL_SIZE = (192, 208)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract a Codex Desktop built-in pet WebP atlas and generate one GIF per state."
    )
    parser.add_argument("--pet", required=True, help="Built-in pet id, for example codex, hoots, rocky, or seedy.")
    parser.add_argument("--asar", type=Path, default=DEFAULT_ASAR, help=f"Path to app.asar. Default: {DEFAULT_ASAR}")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory where the atlas and GIFs are written.")
    parser.add_argument("--duration-ms", type=int, default=500, help="GIF frame duration in milliseconds. Default: 500.")
    parser.add_argument(
        "--keep-empty",
        action="store_true",
        help="Keep fully transparent atlas cells instead of dropping unused frames.",
    )
    return parser.parse_args()


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


def frame_is_empty(frame: Image.Image) -> bool:
    return frame.getchannel("A").getbbox() is None


def write_state_gifs(atlas_path: Path, output_dir: Path, pet: str, duration_ms: int, keep_empty: bool) -> None:
    atlas = Image.open(atlas_path).convert("RGBA")
    if atlas.size != ATLAS_SIZE:
        raise ValueError(f"unexpected atlas size {atlas.size}; expected {ATLAS_SIZE}")

    cell_w, cell_h = CELL_SIZE
    for row, state in enumerate(DEFAULT_STATES):
        frames: list[Image.Image] = []
        dropped = 0

        for col in range(8):
            frame = atlas.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))
            if not keep_empty and frame_is_empty(frame):
                dropped += 1
                continue
            frames.append(frame)

        if not frames:
            raise ValueError(f"{state} row has no non-empty frames")

        gif_path = output_dir / f"{pet}-{state}.gif"
        frames[0].save(
            gif_path,
            save_all=True,
            append_images=frames[1:],
            duration=duration_ms,
            loop=0,
            disposal=2,
        )
        print(f"{gif_path}: {len(frames)} frames, {duration_ms} ms/frame, dropped {dropped}")


def main() -> None:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    data, header, data_start = load_asar(args.asar)
    internal_path, meta = find_pet_asset(header, args.pet)
    atlas_path = args.output_dir / f"{args.pet}-spritesheet.webp"
    extract_asset(data, data_start, meta, atlas_path)
    print(f"{atlas_path}: extracted {internal_path}")

    write_state_gifs(atlas_path, args.output_dir, args.pet, args.duration_ms, args.keep_empty)


if __name__ == "__main__":
    main()
