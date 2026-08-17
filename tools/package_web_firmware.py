#!/usr/bin/env python3
"""Create the merged ESP32-S3 image consumed by the web installer."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = PROJECT_ROOT / "build"
DEFAULT_OUTPUT = (
    PROJECT_ROOT / "docs" / "firmware" / "codex-micro-cardputer-full.bin"
)
MANIFEST_PATH = PROJECT_ROOT / "docs" / "manifest.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Merge ESP-IDF build outputs for ESP Web Tools."
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="ESP-IDF build directory (default: ./build)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="Merged image path",
    )
    return parser.parse_args()


def load_flash_configuration(build_dir: Path) -> tuple[str, dict[str, str], list[str]]:
    config_path = build_dir / "flasher_args.json"
    if not config_path.is_file():
        raise FileNotFoundError(
            f"Missing {config_path}. Run 'idf.py build' before packaging."
        )

    config = json.loads(config_path.read_text(encoding="utf-8"))
    chip = config["extra_esptool_args"]["chip"]
    flash_settings = config["flash_settings"]
    flash_files = config["flash_files"]

    if chip != "esp32s3":
        raise ValueError(f"Expected esp32s3 build, got {chip!r}")

    parts: list[str] = []
    for offset, relative_path in sorted(
        flash_files.items(), key=lambda item: int(item[0], 0)
    ):
        part_path = build_dir / relative_path
        if not part_path.is_file():
            raise FileNotFoundError(f"Missing build output: {part_path}")
        parts.extend((offset, str(part_path.resolve())))

    return chip, flash_settings, parts


def validate_manifest(output: Path) -> None:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    builds = manifest.get("builds", [])
    if len(builds) != 1 or builds[0].get("chipFamily") != "ESP32-S3":
        raise ValueError("Web manifest must contain exactly one ESP32-S3 build")

    manifest_part = builds[0].get("parts", [{}])[0]
    expected_path = output.relative_to(MANIFEST_PATH.parent).as_posix()
    if manifest_part.get("path") != expected_path or manifest_part.get("offset") != 0:
        raise ValueError(
            "Web manifest does not point to the merged image at flash offset 0"
        )


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    output = args.output.resolve()
    chip, flash_settings, parts = load_flash_configuration(build_dir)

    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "merge_bin",
        "-o",
        str(output),
        "--flash_mode",
        flash_settings["flash_mode"],
        "--flash_freq",
        flash_settings["flash_freq"],
        "--flash_size",
        flash_settings["flash_size"],
        *parts,
    ]
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)

    validate_manifest(output)
    image = output.read_bytes()
    if not image or image[0] != 0xE9:
        raise ValueError("Merged output is not a valid ESP image")

    digest = hashlib.sha256(image).hexdigest()
    print(f"Web installer image: {output}")
    print(f"Size: {len(image)} bytes")
    print(f"SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
