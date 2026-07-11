#!/usr/bin/env python3
"""Compare native JSM engraving with its MusicXML projection pixel-for-pixel."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright


def render_svg(
    verovio: Path,
    resources: Path,
    input_path: Path,
    input_format: str,
    output: Path,
    extra_options: list[str],
) -> list[Path]:
    for existing in output.parent.glob(f"{output.stem}_*{output.suffix}"):
        existing.unlink()
    output.unlink(missing_ok=True)
    result = subprocess.run(
        [
            str(verovio),
            "-a",
            "-x",
            "1",
            "-r",
            str(resources),
            "-f",
            input_format,
            "-o",
            str(output),
            *extra_options,
            str(input_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"{input_format} render failed:\n{result.stdout}{result.stderr}")
    if output.exists():
        return [output]
    pages = sorted(output.parent.glob(f"{output.stem}_*{output.suffix}"))
    if not pages:
        raise RuntimeError(f"{input_format} render produced no SVG pages")
    return pages


def rasterize_svg(svg_path: Path, png_path: Path, chrome: Path) -> None:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=str(chrome), headless=True)
        page = browser.new_page(viewport={"width": 1280, "height": 720}, device_scale_factor=1)
        page.goto(svg_path.resolve().as_uri(), wait_until="load")
        svg = page.locator("svg").first
        size = svg.evaluate(
            """node => ({
                width: Math.ceil(node.getBoundingClientRect().width),
                height: Math.ceil(node.getBoundingClientRect().height)
            })"""
        )
        page.set_viewport_size({"width": max(size["width"], 1), "height": max(size["height"], 1)})
        svg.screenshot(path=str(png_path), animations="disabled", caret="hide")
        browser.close()


def rgba(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGBA"), dtype=np.uint8)


def padded(image: np.ndarray, height: int, width: int) -> np.ndarray:
    output = np.full((height, width, 4), 255, dtype=np.uint8)
    output[: image.shape[0], : image.shape[1]] = image
    return output


def compare(native_png: Path, musicxml_png: Path, diff_png: Path) -> dict[str, object]:
    native = rgba(native_png)
    musicxml = rgba(musicxml_png)
    height = max(native.shape[0], musicxml.shape[0])
    width = max(native.shape[1], musicxml.shape[1])
    native_canvas = padded(native, height, width)
    musicxml_canvas = padded(musicxml, height, width)

    channel_delta = np.abs(native_canvas.astype(np.int16) - musicxml_canvas.astype(np.int16))
    changed = np.any(channel_delta != 0, axis=2)
    changed_count = int(changed.sum())
    total = int(changed.size)
    bbox = None
    if changed_count:
        ys, xs = np.where(changed)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]

    diff = np.full((height, width, 4), 255, dtype=np.uint8)
    common_ink = np.any(native_canvas[:, :, :3] < 245, axis=2) & np.any(musicxml_canvas[:, :, :3] < 245, axis=2)
    diff[common_ink] = [190, 190, 190, 255]
    diff[changed] = [220, 0, 80, 255]
    Image.fromarray(diff, mode="RGBA").save(diff_png)

    return {
        "identical": changed_count == 0 and native.shape == musicxml.shape,
        "nativeSize": [int(native.shape[1]), int(native.shape[0])],
        "musicxmlSize": [int(musicxml.shape[1]), int(musicxml.shape[0])],
        "differentPixels": changed_count,
        "totalPixels": total,
        "differentPercent": changed_count * 100.0 / total,
        "meanAbsoluteChannelError": float(channel_delta.mean()),
        "maximumChannelError": int(channel_delta.max()),
        "differenceBounds": bbox,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verovio", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--jsm", type=Path, required=True)
    parser.add_argument("--musicxml", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--chrome", type=Path, default=Path("/usr/bin/google-chrome"))
    parser.add_argument(
        "--verovio-option",
        action="append",
        default=[],
        help="Additional frozen Verovio option; repeat once per argument token",
    )
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stem = args.jsm.stem
    native_svgs = render_svg(
        args.verovio,
        args.resources,
        args.jsm,
        "jsm",
        args.output_dir / f"{stem}.native.svg",
        args.verovio_option,
    )
    musicxml_svgs = render_svg(
        args.verovio,
        args.resources,
        args.musicxml,
        "musicxml",
        args.output_dir / f"{stem}.musicxml.svg",
        args.verovio_option,
    )

    page_metrics: list[dict[str, object]] = []
    for page_number, (native_svg, musicxml_svg) in enumerate(zip(native_svgs, musicxml_svgs), start=1):
        suffix = f"page-{page_number:04d}"
        native_png = args.output_dir / f"{stem}.native.{suffix}.png"
        musicxml_png = args.output_dir / f"{stem}.musicxml.{suffix}.png"
        diff_png = args.output_dir / f"{stem}.diff.{suffix}.png"
        rasterize_svg(native_svg, native_png, args.chrome)
        rasterize_svg(musicxml_svg, musicxml_png, args.chrome)
        page = compare(native_png, musicxml_png, diff_png)
        page.update(
            {
                "page": page_number,
                "nativeSvg": str(native_svg),
                "musicxmlSvg": str(musicxml_svg),
                "nativePng": str(native_png),
                "musicxmlPng": str(musicxml_png),
                "diffPng": str(diff_png),
            }
        )
        page_metrics.append(page)

    page_count_equal = len(native_svgs) == len(musicxml_svgs)
    metrics = {
        "identical": page_count_equal and all(page["identical"] for page in page_metrics),
        "pageCountEqual": page_count_equal,
        "nativePageCount": len(native_svgs),
        "musicxmlPageCount": len(musicxml_svgs),
        "differentPixels": sum(int(page["differentPixels"]) for page in page_metrics),
        "totalPixels": sum(int(page["totalPixels"]) for page in page_metrics),
        "jsm": str(args.jsm),
        "musicxml": str(args.musicxml),
        "verovioOptions": ["-a", "-x", "1", *args.verovio_option],
        "pages": page_metrics,
    }
    metrics["differentPercent"] = (
        float(metrics["differentPixels"]) * 100.0 / int(metrics["totalPixels"])
        if metrics["totalPixels"]
        else 0.0
    )
    metrics_path = args.output_dir / f"{stem}.metrics.json"
    metrics_path.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metrics, indent=2))
    return 0 if metrics["identical"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
