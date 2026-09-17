#!/usr/bin/env bash
# Build a .deb package installing BongoCat to /opt/BongoCat.
set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  echo 'deb packaging requires Linux x86_64.' >&2
  exit 1
fi
if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo 'dpkg-deb is required for deb packaging.' >&2
  exit 1
fi

build_dir=$(cd "${1:?Usage: build-deb.sh BUILD_DIRECTORY}" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
name=$(tr -d '\r\n' < "$build_dir/bongocat-package-name.txt")
[[ $name =~ ^BongoCat(-Diagnostic)?-([0-9][A-Za-z0-9.+-]*)-linux-x64$ ]] || {
  echo "Unexpected package name: $name" >&2
  exit 1
}
version=${BASH_REMATCH[2]}

work=$(mktemp -d "$build_dir/deb.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$build_dir/dist"

# Stage the runtime under /opt/BongoCat, matching the install layout the
# assets expect (executable and assets sit next to each other).
root="$work/root"
cmake --install "$build_dir" --component Runtime --prefix "$root/opt/BongoCat"

# Launcher (detects X11 vs Wayland at start) and the desktop entry.
install -m 755 "$source_dir/packaging/linux/bongocat-run.sh" \
  "$root/opt/BongoCat/run.sh"
install -D -m 644 "$source_dir/packaging/linux/bongocat.desktop" \
  "$root/usr/share/applications/bongocat.desktop"

# Debian control metadata.
debdir="$root/DEBIAN"
mkdir -p "$debdir"
cat > "$debdir/control" <<EOF
Package: bongocat
Version: $version
Section: utils
Priority: optional
Architecture: amd64
Maintainer: vladelaina
Homepage: https://github.com/vladelaina/BongoCat
Description: Animated desktop cat that reacts to your input
 BongoCat is an animated desktop pet that reacts to keyboard and mouse input.
EOF
install -m 755 "$source_dir/packaging/linux/debian/postinst" "$debdir/postinst"
install -m 755 "$source_dir/packaging/linux/debian/postrm" "$debdir/postrm"

# Build the package with root-owned files regardless of the build user.
output="$build_dir/dist/$name.deb"
dpkg-deb --root-owner-group --build "$root" "$output"
(cd "$build_dir/dist" && sha256sum "$name.deb" > "$name.deb.sha256")
echo "deb ready: $output"
