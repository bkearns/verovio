#!/usr/bin/env python3
"""Regenerate the native compact fixture with the Rust reference encoder."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).parent
    subprocess.run(
        [
            str(args.converter),
            "to-compact",
            str(root / "key-signature-accidentals.jsm"),
            str(root / "key-signature-accidentals-compact.jsm"),
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
