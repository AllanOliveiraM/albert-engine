#!/usr/bin/env python3
"""Serialize configure/build operations sharing the native Linux build tree."""
import argparse
import fcntl
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--jobs", type=int, default=2)
parser.add_argument("--target", default="all")
args = parser.parse_args()
if args.jobs < 1:
    parser.error("--jobs must be positive")
root = Path(__file__).resolve().parent.parent
(root / "build").mkdir(exist_ok=True)
with (root / "build/.linux-build.lock").open("a") as lock:
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("Outra compilação está em andamento; aguardando sua conclusão…", flush=True)
        fcntl.flock(lock, fcntl.LOCK_EX)
    def run(command):
        result = subprocess.run(command, cwd=root)
        if result.returncode:
            sys.exit(result.returncode if result.returncode > 0 else 1)
    run(["cmake", "--preset", "linux"])
    cache = root / "build/linux/CMakeCache.txt"
    if cache.exists():
        ninja = next(line.split("=", 1)[1] for line in cache.read_text().splitlines()
                     if line.startswith("CMAKE_MAKE_PROGRAM:"))
        # Ninja <=1.13.2 can repeatedly discard new dependencies after a corrupt
        # log. Official compaction repairs it before any new commands are run.
        # https://github.com/ninja-build/ninja/issues/2703
        run([ninja, "-C", str(root / "build/linux"), "-t", "recompact"])
    run(["cmake", "--build", "--preset", "linux", "--parallel", str(args.jobs), "--target", args.target])
