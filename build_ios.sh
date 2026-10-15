#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")" && pwd)"
sdk="${1:-iphonesimulator}"
case "$sdk" in iphoneos|iphonesimulator) ;; *) echo "usage: bash build_ios.sh [iphoneos|iphonesimulator] [ROM]" >&2; exit 2;; esac
if [[ "$(uname -s)" != Darwin ]]; then echo "iOS builds require macOS and Xcode." >&2; exit 2; fi
if [[ -z "${DEVELOPER_DIR:-}" ]] && ! xcodebuild -version >/dev/null 2>&1; then
    for candidate in /Applications/Xcode.app /Applications/Xcode-beta.app; do
        if [[ -d "$candidate" ]]; then export DEVELOPER_DIR="$candidate/Contents/Developer"; break; fi
    done
fi
for tool in cmake python3 xcodebuild; do command -v "$tool" >/dev/null; done
xcrun --sdk "$sdk" --show-sdk-path >/dev/null
rom="${2:-${DKC1_ROM:-$repo_dir/dkc1.sfc}}"
if ! compgen -G "$repo_dir/generated/snesrecomp/*.c" >/dev/null; then
    if [[ -z "$rom" ]]; then echo "Private generated sources are missing; supply the supported ROM as argument 2." >&2; exit 2; fi
    python3 "$repo_dir/scripts/generate_snesrecomp.py" --rom "$rom"
fi
build_dir="${DKC1_BUILD_DIR:-$repo_dir/build/ios-$sdk}"
# Recreate only this target's bundle so removed resources cannot leak into an IPA.
cmake -E remove_directory "$build_dir/Release-$sdk/DKC1Recomp.app"
signing=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO)
if [[ -n "${DKC1_IOS_TEAM:-}" && "$sdk" == iphoneos ]]; then
    signing=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=YES "-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=$DKC1_IOS_TEAM")
fi
cmake -DDKC1_COOP_ROM="$rom" -S "$repo_dir" -B "$build_dir" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
    -DCMAKE_POLICY_DEFAULT_CMP0177=NEW \
    -DDKC1_IOS_DIAGNOSTICS="${DKC1_IOS_DIAGNOSTICS:-OFF}" "${signing[@]}"
build_options=(-quiet)
if [[ -n "${DKC1_IOS_TEAM:-}" && "$sdk" == iphoneos ]]; then build_options+=(-allowProvisioningUpdates); fi
cmake --build "$build_dir" --config Release --target dkc1_ios --parallel "${DKC1_BUILD_JOBS:-8}" -- "${build_options[@]}"
echo "IOS_BUILD_OK"
echo "$build_dir/Release-$sdk/DKC1Recomp.app"
