#!/usr/bin/env bash
# Launch the isolated renderer experiment with private state/preferences.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
rom_path="${1:-${DKC1_ROM:-$HOME/Downloads/Donkey Kong Country (USA).sfc}}"
pack_path="${DKC1_HD_SCENE_PACK:-$repo_dir/build/hd-slice/scene-pack}"
state_path="${DKC1_HD_STATE:-$repo_dir/build/hd-slice/entry.state}"
if [[ ! -d "$pack_path" || ! -f "$state_path" ]]; then
  echo 'Missing private art pack or root state. See docs/HD_SPRITE_EXPERIMENT.md.' >&2
  exit 2
fi
mkdir -p "$repo_dir/build/hd-slice/user"
export DKC1_HD_SPRITES=1 DKC1_HD_SCENE=1 DKC1_HD_SCENE_PACK="$pack_path"
export DKC1_USER_DIR="$repo_dir/build/hd-slice/user"
export DKC1_SAVESTATE_INPUT="$state_path"
export DKC1_UPSCALER=nearest DKC1_DISPLAY=flat DKC1_SCREEN=raw
export DKC1_PAUSE_AFTER_FRAME="${DKC1_PAUSE_AFTER_FRAME:-1}"
exec "$repo_dir/build/macos/DKC1Recomp-HD.app/Contents/MacOS/DKC1Recomp-HD" "$rom_path"
