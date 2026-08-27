#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: build_macos.sh must run on macOS" >&2
  exit 2
fi

repo_dir="$(cd "$(dirname "$0")" && pwd)"
build_dir="$repo_dir/build/macos"
rom_path="${1:-${DKC1_ROM:-}}"

for tool in cmake ninja sdl2-config python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: missing required tool: $tool" >&2
    echo "Install CMake, Ninja, and SDL2 (for example: brew install cmake ninja sdl2)." >&2
    exit 2
  fi
done

if ! compgen -G "$repo_dir/generated/snesrecomp/*.c" >/dev/null; then
  if [[ -z "$rom_path" ]]; then
    echo "error: private generated sources are missing" >&2
    echo "usage: ./build_macos.sh '/path/to/Donkey Kong Country (USA).sfc'" >&2
    exit 2
  fi
  python3 "$repo_dir/scripts/generate_snesrecomp.py" --rom "$rom_path"
fi

cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSNESRECOMP_SDL_BACKEND=SDL2 \
  -DCMAKE_PREFIX_PATH="$(sdl2-config --prefix)"
# Recreate the bundle itself so Finder never retains a resource-less
# incremental app from an earlier build.
cmake -E remove_directory "$build_dir/DKC1Recomp.app"
cmake --build "$build_dir" --target dkc1_macos dkc1_snesrecomp_headless --parallel

# Make the app bundle independent of the Homebrew install used to build it.
app="$build_dir/DKC1Recomp.app"
executable="$app/Contents/MacOS/DKC1Recomp"
frameworks="$app/Contents/Frameworks"
sdl_source="$(sdl2-config --prefix)/lib/libSDL2-2.0.0.dylib"
sdl_name="$(basename "$sdl_source")"
sdl_bundle="$frameworks/$sdl_name"
mkdir -p "$frameworks"
cp -fL "$sdl_source" "$sdl_bundle"
chmod u+w "$sdl_bundle"
install_name_tool -id "@rpath/$sdl_name" "$sdl_bundle"

linked_sdl="$(otool -L "$executable" | awk '/libSDL2.*dylib/ {print $1; exit}')"
if [[ "$linked_sdl" != "@executable_path/../Frameworks/$sdl_name" ]]; then
  install_name_tool -change "$linked_sdl" \
    "@executable_path/../Frameworks/$sdl_name" "$executable"
fi

codesign --force --sign - "$sdl_bundle"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
touch "$app"
launch_services="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [[ -x "$launch_services" ]]; then
  "$launch_services" -f "$app" >/dev/null
fi

echo "MACOS_BUILD_OK"
echo "$app"
