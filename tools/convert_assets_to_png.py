#!/usr/bin/env python3
"""Normalize assets to real PNG files, preserving every changed source byte-for-byte.

Run from any directory. Existing PNGs are recompressed only when the result is
smaller. Images whose extension says PNG but whose content is another format are
backed up in assets/originals before their path is replaced atomically.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import tempfile
from pathlib import Path

from PIL import Image, ImageChops


ASSETS = Path(__file__).resolve().parents[1] / "assets"
ORIGINALS = ASSETS / "originals"
SUPPORTED = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"}
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_png(path: Path) -> bool:
    with path.open("rb") as source:
        return source.read(8) == PNG_MAGIC


def save_png(source: Path, target: Path) -> tuple[Path, int, str]:
    with Image.open(source) as image:
        image.load()
        size = image.size
        if image.mode == "P" and "transparency" in image.info:
            image = image.convert("RGBA")
        elif image.mode not in {"RGB", "RGBA", "L", "LA", "P"}:
            image = image.convert("RGBA" if "A" in image.getbands() else "RGB")
        mode = image.mode
        fd, name = tempfile.mkstemp(prefix=".png-convert-", suffix=".tmp", dir=ASSETS)
        os.close(fd)
        temporary = Path(name)
        try:
            image.save(temporary, format="PNG", optimize=True, compress_level=9)
            with Image.open(temporary) as check:
                check.load()
                assert check.format == "PNG" and check.size == size and check.mode == mode
            return temporary, temporary.stat().st_size, mode
        except Exception:
            temporary.unlink(missing_ok=True)
            raise


def preserve(source: Path) -> None:
    ORIGINALS.mkdir(exist_ok=True)
    backup = ORIGINALS / source.name
    if backup.exists():
        if sha256(backup) != sha256(source):
            raise RuntimeError(f"Existing backup differs from source: {backup}")
    else:
        shutil.copy2(source, backup)
        if sha256(backup) != sha256(source):
            raise RuntimeError(f"Backup verification failed: {source}")


def main() -> None:
    sources = sorted(path for path in ASSETS.iterdir()
                     if path.is_file() and path.suffix.lower() in SUPPORTED)
    for source in sources:
        target = source if source.suffix.lower() == ".png" else source.with_suffix(".png")
        if target != source and target.exists():
            with Image.open(source) as src_image, Image.open(target) as dst_image:
                src_image.load()
                dst_image.load()
                if dst_image.format != "PNG" or src_image.size != dst_image.size:
                    raise RuntimeError(f"Existing output differs from source: {target}")
                comparison_mode = "RGBA" if "A" in src_image.getbands() else "RGB"
                if ImageChops.difference(src_image.convert(comparison_mode),
                                         dst_image.convert(comparison_mode)).getbbox():
                    raise RuntimeError(f"Existing output pixels differ from source: {target}")
            print(f"keep       {target.name:16} matches {source.name}")
            continue
        original_size = source.stat().st_size
        original_is_png = is_png(source)
        temporary, result_size, mode = save_png(source, target)
        try:
            if target == source and original_is_png and result_size >= original_size:
                print(f"keep       {source.name:16} {mode:4} {original_size:>9} bytes")
                continue
            if target == source:
                preserve(source)
            os.replace(temporary, target)
            print(f"converted  {source.name:16} -> {target.name:16} {mode:4} "
                  f"{original_size:>9} -> {result_size:>9} bytes")
        finally:
            temporary.unlink(missing_ok=True)

    for source in sources:
        output = source if source.suffix.lower() == ".png" else source.with_suffix(".png")
        if not is_png(output):
            raise RuntimeError(f"PNG signature verification failed: {output}")
    print("All PNG outputs verified.")


if __name__ == "__main__":
    main()
