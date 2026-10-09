#!/usr/bin/env python3
"""Generate SYNTHETIC test images for the ProofLine Stage I demo.

These are not real manufactured parts and not real defect photography.
Each image is a 256x256 grayscale surface: a uniform "machined" base
plus sensor noise. DEFECTIVE images add dark scratches and blotches
(anomalous pixel regions) that the Stage I placeholder heuristic is
designed to catch.

Outputs (in the target dir):
  good_*.ppm / bad_*.ppm   P6 PPM inputs for `proofline inspect`
  good_*.png / bad_*.png   previews for humans
  labels.csv               filename,label

Deterministic: fixed RNG seed, so the demo is reproducible.
"""
import csv
import os
import random
import sys

from PIL import Image, ImageDraw

W = H = 256
BASE = 128        # base grey level of a "good" surface
NOISE_SIGMA = 4   # sensor noise
SEED = 20261009


def noisy_surface(rng):
    img = Image.new("L", (W, H))
    px = img.load()
    for y in range(H):
        for x in range(W):
            v = BASE + rng.gauss(0, NOISE_SIGMA)
            px[x, y] = max(0, min(255, int(round(v))))
    return img


def add_scratches(img, rng, count):
    d = ImageDraw.Draw(img)
    for _ in range(count):
        x0, y0 = rng.randrange(W), rng.randrange(H)
        ang = rng.uniform(0, 3.14159)
        length = rng.uniform(60, 160)
        width = rng.randrange(2, 4)
        x1 = x0 + length * __import__("math").cos(ang)
        y1 = y0 + length * __import__("math").sin(ang)
        dark = rng.randrange(55, 95)
        d.line([x0, y0, x1, y1], fill=dark, width=width)


def add_blotches(img, rng, count):
    d = ImageDraw.Draw(img)
    for _ in range(count):
        cx, cy = rng.randrange(W), rng.randrange(H)
        rx, ry = rng.randrange(8, 22), rng.randrange(8, 22)
        dark = rng.randrange(60, 100)
        d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=dark)


def save_pair(img, outdir, stem):
    ppm = os.path.join(outdir, stem + ".ppm")
    png = os.path.join(outdir, stem + ".png")
    img.convert("RGB").save(ppm, "PPM")  # P6 binary PPM
    img.save(png)
    return stem + ".ppm"


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "demo_images"
    n_good = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    n_bad = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    os.makedirs(outdir, exist_ok=True)
    rng = random.Random(SEED)

    rows = []
    for i in range(n_good):
        img = noisy_surface(rng)
        fn = save_pair(img, outdir, f"good_{i:03d}")
        rows.append((fn, "good"))
    for i in range(n_bad):
        img = noisy_surface(rng)
        add_scratches(img, rng, rng.randrange(2, 5))
        add_blotches(img, rng, rng.randrange(1, 3))
        fn = save_pair(img, outdir, f"bad_{i:03d}")
        rows.append((fn, "bad"))

    with open(os.path.join(outdir, "labels.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["filename", "label"])
        w.writerows(rows)
    print(f"wrote {len(rows)} synthetic images to {outdir}/ (+ labels.csv)")


if __name__ == "__main__":
    main()
