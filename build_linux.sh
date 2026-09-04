#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "error: build_linux.sh must run on Linux" >&2
  exit 2
fi

repo_dir="$(cd "$(dirname "$0")" && pwd)"
build_dir="$repo_dir/build/linux"
rom_path="${1:-${DKC1_ROM:-}}"

for tool in cc cmake ninja sdl2-config python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: missing required tool: $tool" >&2
    echo "Install a C compiler, CMake, Ninja, Python 3, and SDL2 development files." >&2
    exit 2
  fi
done

if [[ ! -f "$repo_dir/snesrecomp/runner/runner.cmake" ]]; then
  echo "error: snesrecomp is not initialized" >&2
  echo "run: git submodule update --init --recursive" >&2
  exit 2
fi

if ! compgen -G "$repo_dir/generated/snesrecomp/*.c" >/dev/null; then
  if [[ -z "$rom_path" ]]; then
    echo "error: private generated sources are missing" >&2
    echo "usage: ./build_linux.sh '/path/to/Donkey Kong Country (USA).sfc'" >&2
    exit 2
  fi
  python3 "$repo_dir/scripts/generate_snesrecomp.py" --rom "$rom_path"
fi

cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSNESRECOMP_SDL_BACKEND=SDL2 \
  -DCMAKE_PREFIX_PATH="$(sdl2-config --prefix)"
cmake --build "$build_dir" \
  --target dkc1_snesrecomp_sdl dkc1_snesrecomp_headless --parallel

echo "LINUX_BUILD_OK"
echo "$build_dir/DKC1Recomp"
