#!/usr/bin/env bash
# Build a port for Linux (native Linux, or WSL from a Windows checkout).
#   tools/build_linux.sh <port dir> [--headless]
# Output: <port>/build-linux/game/<slug>. Needs cmake, ninja, a C++20 compiler; for the window
# build also SDL3 (libsdl3-dev), Vulkan (libvulkan-dev) and glslc (glslc or shaderc package).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
port="$(cd "$1" && pwd)"; shift || true
extra=()
for a in "$@"; do
  case "$a" in
    --headless) extra+=(-DDREAM_RENDERER=OFF) ;;
    *) echo "unknown option $a" >&2; exit 2 ;;
  esac
done
cmake -S "$port" -B "$port/build-linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDREAMCOMP_DIR="$here" "${extra[@]}"
id="$(basename "$(ls "$port"/game/*.toml | grep -v suggested | head -1)" .toml)"
cmake --build "$port/build-linux" --target "${id}_boot" --parallel
ls -la "$port/build-linux/game/"
