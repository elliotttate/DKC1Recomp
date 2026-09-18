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

if ! compgen -G "$repo_dir/generated/snesrecomp_dixie/*.c" >/dev/null; then
  if [[ -z "$rom_path" ]]; then
    echo "error: private Dixie sources are missing" >&2
    echo "usage: ./build_macos.sh '/path/to/Donkey Kong Country (USA).sfc'" >&2
    exit 2
  fi
  python3 "$repo_dir/scripts/generate_macos_dixie.py" --rom "$rom_path"
fi

# CMake writes Info.plist while generating the build tree. Remove the old
# bundle before configure so a clean bundle gets fresh metadata and resources.
cmake -E remove_directory "$build_dir/DKC1Recomp-HD.app"
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DSNESRECOMP_SDL_BACKEND=SDL2 \
  -DCMAKE_PREFIX_PATH="$(sdl2-config --prefix)"
cmake --build "$build_dir" --target \
  dkc1_macos dkc1_macos_dixie \
  dkc1_snesrecomp_headless dkc1_dixie_headless --parallel

# Make the app bundle independent of the Homebrew install used to build it.
app="$build_dir/DKC1Recomp-HD.app"
executable="$app/Contents/MacOS/DKC1Recomp-HD"
dixie_build="$build_dir/DKC1Recomp-HD-Dixie"
dixie_executable="$app/Contents/MacOS/DKC1Recomp-HD-Dixie"
frameworks="$app/Contents/Frameworks"
sdl_source="$(sdl2-config --prefix)/lib/libSDL2-2.0.0.dylib"
sdl_name="$(basename "$sdl_source")"
sdl_bundle="$frameworks/$sdl_name"
mkdir -p "$frameworks"
cp -fL "$sdl_source" "$sdl_bundle"
cp -f "$dixie_build" "$dixie_executable"
chmod u+w "$sdl_bundle"
install_name_tool -id "@rpath/$sdl_name" "$sdl_bundle"

for runtime in "$executable" "$dixie_executable"; do
  linked_sdl="$(otool -L "$runtime" | awk '/libSDL2.*dylib/ {print $1; exit}')"
  if [[ "$linked_sdl" != "@executable_path/../Frameworks/$sdl_name" ]]; then
    install_name_tool -change "$linked_sdl" \
      "@executable_path/../Frameworks/$sdl_name" "$runtime"
  fi
done

codesign --force --sign - "$sdl_bundle"
codesign --force --sign - "$dixie_executable"
current_pack="$repo_dir/build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/materials"
legacy_pack="$repo_dir/build/hd-slice/scene-pack"
hd_pack="${DKC1_HD_RELEASE_PACK:-}"
if [[ -z "$hd_pack" && -f "$current_pack/preload.txt" ]]; then
  hd_pack="$current_pack"
elif [[ -z "$hd_pack" && -f "$legacy_pack/pack-manifest.json" ]]; then
  hd_pack="$legacy_pack"
fi
if [[ -n "$hd_pack" && ( -f "$hd_pack/preload.txt" || -f "$hd_pack/pack-manifest.json" ) ]]; then
  private_scene="$app/Contents/Resources/HDScene"
  cmake -E remove_directory "$private_scene/Materials"
  mkdir -p "$private_scene/Materials"
  ditto "$hd_pack" "$private_scene/Materials"
  candy_plate="$private_scene/Materials/fixed-treehouse-wide.bgra"
  if [[ -f "$candy_plate" ]]; then
    python3 "$repo_dir/scripts/apply_hd_fixed_patch.py" \
      --plate "$candy_plate" \
      --manifest "$repo_dir/assets/hd-preview/treehouse-candy-v1.json"
  fi
fi
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
touch "$app"
launch_services="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [[ -x "$launch_services" ]]; then
  "$launch_services" -f "$app" >/dev/null
fi

echo "MACOS_BUILD_OK"
echo "$app"
