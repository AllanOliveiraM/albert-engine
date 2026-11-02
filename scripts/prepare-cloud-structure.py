#!/usr/bin/env python3
"""Prepare the bundled historical NASA cloud structure reference (requires Pillow)."""
import argparse
import hashlib
from pathlib import Path
import shutil
import urllib.request
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
URL = "https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_8192.tif"
SHA256 = "d137775d8966ab8d443fd5126dc6e7ad72072bc1ed50555c5818d221735daf0f"

def prepare(source, output):
    source.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        temporary = source.with_suffix(".download")
        try:
            with urllib.request.urlopen(URL, timeout=120) as incoming, temporary.open("wb") as destination:
                shutil.copyfileobj(incoming, destination)
            temporary.replace(source)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(source.read_bytes()).hexdigest() != SHA256:
        raise ValueError("NASA cloud reference checksum mismatch")
    with Image.open(source) as original:
        image = original.convert("L").resize((4096, 2048), Image.Resampling.LANCZOS)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, optimize=True)
    print(output, output.stat().st_size, hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "build/cloud_combined_8192.tif")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/clouds/structure.png")
    args = parser.parse_args()
    prepare(args.source, args.output)
