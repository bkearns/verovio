#!/usr/bin/env python3
"""Assert canonical and compact JSM render to identical pixels."""
import argparse
import tempfile
from pathlib import Path

from pixel_parity import compare, rasterize_svg, render_svg

parser = argparse.ArgumentParser()
parser.add_argument("--verovio", type=Path, required=True)
parser.add_argument("--resources", type=Path, required=True)
parser.add_argument("--canonical", type=Path, required=True)
parser.add_argument("--compact", type=Path, required=True)
parser.add_argument("--chrome", type=Path, default=Path("/usr/bin/google-chrome"))
args = parser.parse_args()

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    canonical_svg = render_svg(args.verovio, args.resources, args.canonical, "jsm", tmp / "canonical.svg", [])
    compact_svg = render_svg(args.verovio, args.resources, args.compact, "jsm", tmp / "compact.svg", [])
    if len(canonical_svg) != len(compact_svg):
        raise SystemExit("canonical and compact page counts differ")
    for index, (left, right) in enumerate(zip(canonical_svg, compact_svg, strict=True)):
        left_png, right_png = tmp / f"canonical-{index}.png", tmp / f"compact-{index}.png"
        rasterize_svg(left, left_png, args.chrome)
        rasterize_svg(right, right_png, args.chrome)
        result = compare(left_png, right_png, tmp / f"diff-{index}.png")
        if not result["identical"]:
            raise SystemExit(f"page {index + 1} differs: {result}")
print("canonical/compact native JSM pixel parity passed")
