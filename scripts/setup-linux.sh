#!/usr/bin/env bash
set -euo pipefail

# Supported system packages are explicit; application libraries are fetched
# with checksums by CMake. Running this script is the opt-in system install.
if [[ ! -r /etc/os-release ]]; then
    echo "Sistema nao identificado. Consulte README.md para instalacao manual." >&2
    exit 1
fi
source /etc/os-release
elevate=()
if (( EUID != 0 )); then
    if ! command -v sudo >/dev/null; then
        echo "Execute como root ou instale sudo para instalar os pacotes do sistema." >&2
        exit 1
    fi
    elevate=(sudo)
fi

case "$ID" in
    ubuntu|debian|linuxmint|pop)
        "${elevate[@]}" apt-get update
        "${elevate[@]}" apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build pkg-config ca-certificates python3 git-lfs \
            libssl-dev xorg-dev libglu1-mesa-dev
        ;;
    fedora)
        "${elevate[@]}" dnf install -y \
            gcc gcc-c++ cmake ninja-build pkgconf-pkg-config ca-certificates python3 git-lfs \
            openssl-devel libX11-devel libXrandr-devel libXinerama-devel \
            libXcursor-devel libXi-devel libXext-devel mesa-libGL-devel mesa-libGLU-devel
        ;;
    *)
        echo "Distribuicao nao automatizada: $ID. Consulte README.md." >&2
        exit 1
        ;;
esac

cmake_version=$(cmake --version | head -n 1 | awk '{print $3}')
IFS=. read -r cmake_major cmake_minor _ <<< "$cmake_version"
if (( cmake_major < 3 || (cmake_major == 3 && cmake_minor < 25) )); then
    echo "CMake >= 3.25 necessario; encontrado $cmake_version. Consulte README.md." >&2
    exit 1
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -d "$root/.git" ]]; then
    git -C "$root" lfs install --local
    git -C "$root" lfs pull
fi
echo "Dependencias instaladas. Execute: make render-run"
