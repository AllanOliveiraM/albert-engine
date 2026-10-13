"""Produce the bundled bright-star subset from a local HYG 4.1 CSV.

Maintenance tool only. Python is not required to build or run Albert Engine.
Source: https://github.com/astronexus/HYG-Database, CC BY-SA 4.0, David Nash.
"""
import argparse
import csv
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path, help="Downloaded hygdata_v41.csv")
args = parser.parse_args()
destination = Path(__file__).resolve().parents[1] / "assets" / "stars.csv"
fields = ["id", "proper", "mag", "ci", "x", "y", "z", "vx", "vy", "vz"]
with args.source.open(encoding="utf-8", newline="") as source, destination.open("w", encoding="utf-8", newline="") as output:
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(fields)
    for star in csv.DictReader(source):
        if star["id"] != "0" and star["mag"] and float(star["mag"]) <= 6:
            writer.writerow([star[field] for field in fields])
print(destination)
