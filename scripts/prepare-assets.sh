#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
body=""
if [[ ${1:-} == --body ]]; then
    if [[ $# -lt 2 ]]; then echo 'Informe o corpo: --body mercury' >&2; exit 2; fi
    body=$2; shift 2
fi
rebuild=false
if [[ ${1:-} == --rebuild ]]; then rebuild=true; shift; fi
if [[ -z "$body" && $# -eq 0 && "$rebuild" == false && -f "$root/assets/imagery/manifest.json" ]]; then
    echo "Biblioteca versionada: assets/imagery/ (reunida automaticamente no build)"
    exit 0
fi
docker build -t albert-assets-builder -f "$root/scripts/Dockerfile.assets" "$root/scripts"
if [[ -n "$body" ]]; then
    docker run --rm --init --user "$(id -u):$(id -g)" --entrypoint python3 -v "$root:/work" -w /work albert-assets-builder scripts/prepare-body-assets.py "$body" "$@"
    exit 0
fi
docker run --rm --init --user "$(id -u):$(id -g)" -v "$root:/work" -w /work albert-assets-builder "$@"
