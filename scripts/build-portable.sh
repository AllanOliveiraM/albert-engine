#!/usr/bin/env bash
set -euo pipefail

# Optional reproducible toolchain environment; no compiler/system packages are
# installed on the host. Docker itself must already be available.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
platform=${1:-all}
case "$platform" in linux|windows|all) ;; *) echo "Uso: $0 [linux|windows|all]" >&2; exit 1 ;; esac
if ! command -v docker >/dev/null; then
    echo "Docker necessario para o build em container. Veja README.md para o build nativo." >&2
    exit 1
fi
bash "$root/scripts/prepare-assets.sh"
docker build -t albert-engine-builder -f "$root/scripts/Dockerfile.build" "$root/scripts"
run=(docker run --rm --init --user "$(id -u):$(id -g)" -v "$root:/work" -w /work albert-engine-builder -lc)
if [[ "$platform" == linux || "$platform" == all ]]; then
    "${run[@]}" 'cmake -S . -B build/portable-linux -G Ninja -DCMAKE_BUILD_TYPE=Release &&
        cmake --build build/portable-linux --parallel 4 &&
        ctest --test-dir build/portable-linux --output-on-failure &&
        python3 tests/http_fixture.py build/portable-linux/tests/download_test &&
        python3 tests/wms_fixture.py build/portable-linux/tests/wms_test &&
        cmake -E rm -rf dist/linux &&
        cmake --install build/portable-linux --prefix /work/dist/linux --component Runtime --strip'
fi
if [[ "$platform" == windows || "$platform" == all ]]; then
    "${run[@]}" 'cmake -S scripts/host-tools -B build/portable-host-tools -G Ninja -DCMAKE_BUILD_TYPE=Release &&
        cmake --build build/portable-host-tools --target albert_host_tools --parallel 4 &&
        cmake -S . -B build/portable-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw64.cmake -DALBERT_HOST_TOOLS_DIR=/work/build/portable-host-tools/bin &&
        cmake --build build/portable-windows --parallel 4 &&
        cmake -E rm -rf dist/windows &&
        cmake --install build/portable-windows --prefix /work/dist/windows --component Runtime --strip'
fi
echo "Distribuicao pronta em dist/. Copie ou zipe a pasta de cada sistema."
