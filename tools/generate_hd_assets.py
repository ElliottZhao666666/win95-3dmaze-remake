#!/usr/bin/env python3
"""Generate the modern, tileable asset set used by the native 3D Maze saver.

The generator intentionally uses only Pillow and numpy.  Every procedural field
is periodic in both axes and the raster outputs are written with a final seam
assertion, so the generated textures are safe for D3D11 wrap addressing.
"""
from __future__ import annotations

import importlib
import math
import subprocess
import sys
from pathlib import Path


def ensure_package(module: str, package: str | None = None) -> None:
    try:
        importlib.import_module(module)
    except ImportError:
        subprocess.check_call([sys.executable, "-m", "pip", "install", package or module])


ensure_package("numpy")
ensure_package("PIL", "Pillow")

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "Windows-95-3D-Maze-Screensaver"
OUT = ROOT / "assets"
OUT.mkdir(parents=True, exist_ok=True)


def periodic_noise(size: int, seed: int, octaves: tuple[int, ...] = (4, 8, 16, 32, 64, 128)) -> np.ndarray:
    """A smooth, exactly periodic multi-scale value field in [0, 1]."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    field = np.zeros((size, size), dtype=np.float32)
    weight_sum = 0.0
    for period in octaves:
        # A Fourier basis is periodic by construction and avoids interpolation
        # seams at the texture boundary.
        terms = 5
        part = np.zeros_like(field)
        for _ in range(terms):
            angle = rng.uniform(0, math.tau)
            phase = rng.uniform(0, math.tau)
            freq = rng.integers(1, max(2, period // 2))
            part += np.sin(math.tau * freq * (np.cos(angle) * x + np.sin(angle) * y) / period + phase)
        part /= terms
        weight = 1.0 / (period ** 0.22)
        field += part * weight
        weight_sum += weight
    field /= max(weight_sum, 1e-6)
    return np.clip(field * 0.5 + 0.5, 0.0, 1.0)


def save_tile(rgb: np.ndarray, path: Path) -> None:
    arr = np.clip(rgb, 0, 255).astype(np.uint8)
    # Duplicate the first row/column to make the byte edge equality explicit.
    arr[-1, :, :] = arr[0, :, :]
    arr[:, -1, :] = arr[:, 0, :]
    Image.fromarray(arr, "RGB").save(path, optimize=True)
    check = np.asarray(Image.open(path))
    assert np.array_equal(check[0, :, :], check[-1, :, :])
    assert np.array_equal(check[:, 0, :], check[:, -1, :])


def generate_wall(size: int = 2048) -> None:
    y, x = np.mgrid[0:size, 0:size]
    brick_h, brick_w = 128, 512
    row = (y // brick_h).astype(np.int32)
    local_y = y % brick_h
    stagger = (row & 1) * (brick_w // 2)
    local_x = (x + stagger) % brick_w
    mortar = (local_x < 10) | (local_y < 10)
    # Subtle mortar shadow is broad enough to read under anisotropic filtering.
    grout_shadow = np.exp(-np.minimum(local_x, brick_w - local_x) / 14.0)
    grout_shadow += np.exp(-np.minimum(local_y, brick_h - local_y) / 14.0)
    grain = periodic_noise(size, 11, (32, 64, 128, 256))
    fine = periodic_noise(size, 42, (8, 16, 32))
    variation = (grain - 0.5) * 34.0 + (fine - 0.5) * 11.0
    brick = np.stack([146 + variation, 40 + variation * 0.55, 32 + variation * 0.35], axis=-1)
    shade = 1.0 - 0.22 * np.clip(grout_shadow, 0, 1)
    brick *= shade[..., None]
    brick[mortar] = np.stack([122 + variation[mortar] * 0.2,
                              128 + variation[mortar] * 0.15,
                              126 + variation[mortar] * 0.12], axis=-1)
    save_tile(brick, OUT / "wall.png")


def generate_floor(size: int = 2048) -> None:
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    # Four boards across the tile; all phase terms use integer cycles.
    board = np.floor((x % size) / (size / 4)).astype(np.int32)
    seams = np.minimum(x % (size / 4), (size / 4) - (x % (size / 4)))
    fiber = (np.sin(y * 0.042 + np.sin(y * 0.009) * 2.0 + board * 1.2) * 0.5 + 0.5)
    rings = (np.sin(y * 0.013 + np.sin(x * 0.004) * 9.0 + board * 2.0) * 0.5 + 0.5)
    grain = periodic_noise(size, 87, (16, 32, 64, 128))
    value = (fiber - 0.5) * 30.0 + (rings - 0.5) * 18.0 + (grain - 0.5) * 24.0
    base = np.stack([148 + value, 83 + value * 0.58, 39 + value * 0.28], axis=-1)
    base *= (0.72 + 0.28 * np.clip(seams / 10.0, 0, 1))[..., None]
    # Staggered short end joints, rendered as periodic dark ticks.
    joint = ((y % 512) < 8) & (((x + (board & 1) * 200) % 512) < 52)
    base[joint] *= 0.48
    save_tile(base, OUT / "floor.png")


def generate_ceiling(size: int = 2048) -> None:
    n1 = periodic_noise(size, 123, (8, 16, 32, 64))
    n2 = periodic_noise(size, 456, (32, 64, 128, 256))
    n3 = periodic_noise(size, 789, (128, 256, 512))
    value = (n1 - 0.5) * 58 + (n2 - 0.5) * 38 + (n3 - 0.5) * 26
    # Neutral diffuse: equal channel response, no baked directional lighting.
    gray = 126 + value
    save_tile(np.repeat(gray[..., None], 3, axis=-1), OUT / "ceiling.png")


def load_reference(name: str, size: tuple[int, int]) -> Image.Image:
    path = SRC / name
    if path.exists():
        return Image.open(path).convert("RGBA").resize(size, Image.Resampling.LANCZOS)
    return Image.new("RGBA", size, (0, 0, 0, 0))


def generate_picture(size: int = 1024) -> None:
    im = Image.new("RGBA", (size, size), (20, 20, 22, 255))
    d = ImageDraw.Draw(im)
    # Brushed antique metal frame with nested bevels.
    for i in range(44):
        t = i / 43
        c = (int(62 + 116 * t), int(40 + 74 * t), int(18 + 34 * t), 255)
        d.rectangle((i, i, size - i - 1, size - i - 1), outline=c, width=2)
    d.rectangle((66, 66, size - 67, size - 67), fill=(38, 46, 49, 255), outline=(203, 161, 81, 255), width=5)
    # A stylized, high-resolution Utah teapot silhouette with glossy highlights.
    cx, cy = size // 2, size // 2 + 22
    body = [(260, 620), (245, 540), (270, 420), (342, 350), (455, 318),
            (570, 336), (670, 408), (704, 514), (682, 620), (610, 690),
            (384, 690)]
    body = [(x, y) for x, y in body]
    d.ellipse((270, 300, 680, 700), fill=(171, 98, 40, 255), outline=(250, 190, 100, 255), width=5)
    d.polygon([(330, 386), (190, 320), (110, 362), (245, 438), (340, 452)], fill=(177, 101, 40, 255), outline=(238, 167, 86, 255))
    d.polygon([(662, 418), (805, 350), (910, 396), (798, 473), (680, 474)], fill=(160, 82, 35, 255), outline=(232, 162, 79, 255))
    d.ellipse((388, 260, 565, 370), fill=(156, 84, 34, 255), outline=(239, 169, 86, 255), width=6)
    d.ellipse((420, 275, 535, 330), fill=(40, 35, 32, 255))
    d.arc((280, 336, 676, 716), 205, 338, fill=(255, 212, 124, 220), width=12)
    d.arc((302, 358, 650, 680), 30, 155, fill=(91, 42, 25, 220), width=12)
    # Specular glint and soft vignette.
    glint = Image.new("RGBA", im.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glint)
    gd.ellipse((330, 370, 455, 490), fill=(255, 234, 175, 100))
    glint = glint.filter(ImageFilter.GaussianBlur(28))
    im = Image.alpha_composite(im, glint)
    im.save(OUT / "pic.png", optimize=True)
    im.convert("RGB").save(OUT / "pic.bmp")


def alpha_sprite(size: int = 512) -> Image.Image:
    return Image.new("RGBA", (size, size), (0, 0, 0, 0))


def generate_rat(size: int = 512) -> None:
    im = alpha_sprite(size)
    d = ImageDraw.Draw(im)
    d.ellipse((120, 178, 405, 418), fill=(117, 113, 105, 255), outline=(235, 232, 220, 255), width=6)
    d.ellipse((108, 120, 230, 250), fill=(137, 131, 122, 255), outline=(230, 225, 211, 255), width=6)
    d.ellipse((285, 128, 414, 250), fill=(137, 131, 122, 255), outline=(230, 225, 211, 255), width=6)
    d.ellipse((173, 220, 206, 260), fill=(28, 20, 18, 255))
    d.ellipse((313, 220, 346, 260), fill=(28, 20, 18, 255))
    d.ellipse((218, 276, 310, 342), fill=(235, 182, 173, 255))
    d.ellipse((216, 295, 235, 315), fill=(42, 26, 22, 255))
    d.ellipse((295, 295, 314, 315), fill=(42, 26, 22, 255))
    d.arc((215, 300, 320, 370), 15, 165, fill=(40, 25, 22, 255), width=6)
    d.arc((365, 350, 530, 500), 180, 280, fill=(166, 113, 86, 255), width=14)
    im.filter(ImageFilter.GaussianBlur(0.7)).save(OUT / "rat.png")


def generate_smiley(size: int = 512) -> None:
    im = alpha_sprite(size)
    px = np.zeros((size, size, 4), dtype=np.uint8)
    y, x = np.mgrid[0:size, 0:size]
    cx = cy = size / 2
    r = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
    a = np.clip((size * 0.43 - r) * 5, 0, 255)
    light = np.clip(1.0 - r / (size * 0.55), 0, 1)
    px[..., 0] = 226 + (light * 29).astype(np.uint8)
    px[..., 1] = 154 + (light * 82).astype(np.uint8)
    px[..., 2] = 2
    px[..., 3] = a.astype(np.uint8)
    im = Image.fromarray(px, "RGBA")
    d = ImageDraw.Draw(im)
    d.ellipse((158, 180, 205, 245), fill=(43, 29, 11, 255))
    d.ellipse((307, 180, 354, 245), fill=(43, 29, 11, 255))
    d.arc((155, 180, 365, 380), 15, 165, fill=(52, 30, 8, 255), width=18)
    im.filter(ImageFilter.GaussianBlur(0.6)).save(OUT / "fin.png")


def generate_start(size: int = 512) -> None:
    im = alpha_sprite(size)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((32, 118, size - 32, size - 118), radius=38, fill=(54, 66, 76, 255), outline=(213, 218, 224, 255), width=10)
    d.rounded_rectangle((48, 134, size - 48, size - 134), radius=28, fill=(18, 84, 106, 255), outline=(81, 221, 237, 255), width=7)
    font = None
    for candidate in ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf"):
        if Path(candidate).exists():
            font = ImageFont.truetype(candidate, 110)
            break
    font = font or ImageFont.load_default()
    text = "START"
    box = d.textbbox((0, 0), text, font=font)
    pos = ((size - (box[2] - box[0])) // 2, (size - (box[3] - box[1])) // 2 - 8)
    d.text((pos[0] + 4, pos[1] + 6), text, font=font, fill=(4, 30, 40, 240), stroke_width=4, stroke_fill=(3, 22, 30, 255))
    d.text(pos, text, font=font, fill=(255, 248, 150, 255), stroke_width=2, stroke_fill=(255, 255, 255, 255))
    im.filter(ImageFilter.GaussianBlur(0.45)).save(OUT / "start2.png")


def main() -> None:
    generate_wall()
    generate_floor()
    generate_ceiling()
    generate_picture()
    generate_rat()
    generate_smiley()
    generate_start()
    # Keep a conventional name for the old WebGL viewer as well.
    load_reference("gl.png", (512, 512)).save(OUT / "gl.png")
    for name in ("wall.png", "floor.png", "ceiling.png"):
        print(f"generated {name}: {Image.open(OUT / name).size}")
    for name in ("pic.png", "pic.bmp", "rat.png", "fin.png", "start2.png"):
        print(f"generated {name}: {Image.open(OUT / name).size}")


if __name__ == "__main__":
    main()
