#!/usr/bin/env bash
set -euo pipefail

# Cubism ships separate static libraries, so build each slice independently.
for arch in arm64 x86_64; do
  cmake -S . -B "build-app-store/$arch" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DBUILD_TESTING=OFF \
    -DBONGO_CAT_REQUIRE_CUBISM=ON \
    -DBONGO_CAT_WARNINGS_AS_ERRORS=ON \
    -DBONGO_CAT_OPTIMIZE_RELEASE_SIZE=ON \
    -DBONGO_CAT_OPTIMIZE_RELEASE_IPO=ON
  bash .github/scripts/build-unix.sh cmake --build "build-app-store/$arch" --parallel 3
done

app="build-app-store/BongoCat.app"
rm -rf "$app"
ditto build-app-store/arm64/BongoCat.app "$app"
codesign --remove-signature "$app"
lipo -create \
  build-app-store/arm64/BongoCat.app/Contents/MacOS/BongoCat \
  build-app-store/x86_64/BongoCat.app/Contents/MacOS/BongoCat \
  -output "$app/Contents/MacOS/BongoCat"
lipo -verify_arch arm64 x86_64 "$app/Contents/MacOS/BongoCat"

# Dependencies must be static or Apple system libraries. Do not upload a bundle
# that still relies on a Homebrew library installed only on the CI machine.
python3 - <<'PY'
import subprocess
binary = 'build-app-store/BongoCat.app/Contents/MacOS/BongoCat'
for arch in ('arm64', 'x86_64'):
    output = subprocess.check_output(['otool', '-arch', arch, '-L', binary], text=True)
    for line in output.splitlines():
        if not line.startswith('\t'):
            continue
        dependency = line.strip().split(' (', 1)[0]
        if not dependency.startswith(('/System/Library/', '/usr/lib/')):
            raise SystemExit(f'Unbundled dependency ({arch}): {dependency}')
PY
