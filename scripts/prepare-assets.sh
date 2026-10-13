#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
rebuild=false
if [[ ${1:-} == --rebuild ]]; then rebuild=true; shift; fi
if [[ $# -eq 0 && "$rebuild" == false && -f "$root/assets/imagery/manifest.json" ]]; then
    echo "Biblioteca versionada: assets/imagery/ (reunida automaticamente no build)"
    exit 0
fi
docker build -t albert-assets-builder -f "$root/scripts/Dockerfile.assets" "$root/scripts"
docker run --rm --init --user "$(id -u):$(id -g)" -v "$root:/work" -w /work albert-assets-builder "$@"
