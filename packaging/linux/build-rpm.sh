#!/usr/bin/env bash
# Build a binary RPM from the already-built BongoCat Linux runtime.
set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  echo 'RPM packaging requires Linux x86_64.' >&2
  exit 1
fi

for tool in cmake rpmbuild rpm tar sha256sum; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is required for RPM packaging." >&2
    exit 1
  fi
done

build_dir=$(cd "${1:?Usage: build-rpm.sh BUILD_DIRECTORY}" && pwd)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
name=$(tr -d '\r\n' < "$build_dir/bongocat-package-name.txt")
[[ $name =~ ^BongoCat-([0-9][A-Za-z0-9.+-]*)-linux-x64$ ]] || {
  echo "RPM packaging requires a Cubism-enabled production build: $name" >&2
  exit 1
}
version=${BASH_REMATCH[1]}

work=$(mktemp -d "$build_dir/rpm.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$build_dir/dist"

root="$work/root"
runtime="$root/usr/libexec/bongocat"
cmake --install "$build_dir" --component Runtime --prefix "$runtime"

test -x "$runtime/BongoCat"
test -d "$runtime/assets"
test -f "$runtime/assets/models/standard/cat.model3.json"
install -m 755 "$source_dir/packaging/linux/bongocat-update-rpm.sh" \
  "$runtime/bongocat-update-rpm.sh"

install -D -m 755 "$source_dir/packaging/linux/bongocat-run.sh" \
  "$root/usr/bin/BongoCat"
ln -s BongoCat "$root/usr/bin/bongocat"
install -D -m 644 "$source_dir/packaging/linux/bongocat.desktop" \
  "$root/usr/share/applications/bongocat.desktop"
install -D -m 644 "$source_dir/resources/assets/bongocat.png" \
  "$root/usr/share/icons/hicolor/512x512/apps/bongocat.png"
install -D -m 644 "$source_dir/README.md" \
  "$root/usr/share/doc/bongocat/README.md"

license_dir="$root/usr/share/licenses/bongocat"
mkdir -p "$license_dir"
install -m 644 "$source_dir/LICENSE" "$license_dir/LICENSE"
install -m 644 "$source_dir/LICENSE-MIT" "$license_dir/LICENSE-MIT"
install -m 644 "$source_dir/resources/assets/models/LICENSE" \
  "$license_dir/LICENSE-models"
for license in "$source_dir"/resources/assets/licenses/*.txt; do
  install -m 644 "$license" "$license_dir/$(basename "$license")"
done

if command -v desktop-file-validate >/dev/null 2>&1; then
  desktop-file-validate "$root/usr/share/applications/bongocat.desktop"
fi

rpmbuild_dir="$work/rpmbuild"
mkdir -p "$rpmbuild_dir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
payload="$rpmbuild_dir/SOURCES/bongocat-$version-runtime.tar.gz"
tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 \
  --numeric-owner -C "$root" -czf "$payload" .

spec="$rpmbuild_dir/SPECS/bongocat.spec"
changelog_date=$(LC_ALL=C date '+%a %b %d %Y')
sed -e "s/@VERSION@/$version/g" \
  -e "s/@CHANGELOG_DATE@/$changelog_date/g" \
  "$source_dir/packaging/linux/bongocat.spec.in" > "$spec"
rpmbuild --define "_topdir $rpmbuild_dir" -bb "$spec"

mapfile -t rpms < <(
  find "$rpmbuild_dir/RPMS" -type f -name "bongocat-$version-*.rpm"
)
if [[ ${#rpms[@]} -ne 1 ]]; then
  echo "Expected one BongoCat RPM, found ${#rpms[@]}." >&2
  exit 1
fi

output="$build_dir/dist/$(basename "${rpms[0]}")"
install -m 644 "${rpms[0]}" "$output"
(
  cd "$build_dir/dist"
  sha256sum "$(basename "$output")" > "$(basename "$output").sha256"
)
echo "RPM ready: $output"
