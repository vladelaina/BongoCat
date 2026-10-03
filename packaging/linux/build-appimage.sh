#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  echo 'AppImage packaging requires Linux x86_64.' >&2
  exit 1
fi
build_dir=$(cd "${1:?Usage: build-appimage.sh BUILD_DIRECTORY}" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
name=$(tr -d '\r\n' < "$build_dir/bongocat-package-name.txt")
[[ $name =~ ^BongoCat(-Diagnostic)?-[0-9][A-Za-z0-9.+-]*-linux-x64$ ]] || {
  echo "Unexpected package name: $name" >&2
  exit 1
}
work=$(mktemp -d "$build_dir/appimage.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$build_dir/dist"

# A private staging tree keeps repeated builds free of stale dependencies.
# Keep assets next to the executable, as expected by the runtime.
appdir="$work/BongoCat.AppDir"
cmake --install "$build_dir" --component Runtime --prefix "$appdir/usr/bin"

tool="$work/linuxdeploy.AppImage"
curl --fail --location --retry 3 --output "$tool" \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage
chmod +x "$tool"
export ARCH=x86_64
export VERSION="${name#BongoCat-}"
export OUTPUT="$build_dir/dist/$name.AppImage"
# Build runners do not need FUSE, including subprocess AppImage tools.
export APPIMAGE_EXTRACT_AND_RUN=1
pushd "$build_dir" >/dev/null
"$tool" --appdir "$appdir" \
  --executable "$appdir/usr/bin/BongoCat" \
  --desktop-file "$source_dir/packaging/linux/bongocat.desktop" \
  --icon-file "$source_dir/resources/assets/bongocat.png" \
  --output appimage
popd >/dev/null
test -s "$OUTPUT"
chmod +x "$OUTPUT"
(cd "$build_dir/dist" && sha256sum "$name.AppImage" > "$name.AppImage.sha256")
echo "AppImage ready: $OUTPUT"
