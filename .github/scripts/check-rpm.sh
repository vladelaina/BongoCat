#!/usr/bin/env bash
# Install, inspect, and remove a built RPM in a RHEL-compatible container.
# Rocky Linux 9 is the default because the reduced UBI repositories omit
# libglvnd-opengl, which is present in the full RHEL 9 repositories.
set -euo pipefail

rpm_file=$(realpath "${1:?Usage: check-rpm.sh RPM_FILE}")
image="${RPM_TEST_IMAGE:-rockylinux:9}"
engine="${CONTAINER_ENGINE:-}"

if [[ -z $engine ]]; then
  if command -v docker >/dev/null 2>&1; then
    engine=docker
  elif command -v podman >/dev/null 2>&1; then
    engine=podman
  else
    echo 'docker or podman is required for RPM checks.' >&2
    exit 1
  fi
fi

"$engine" run --rm -i \
  --security-opt label=disable \
  --volume "$rpm_file:/tmp/bongocat.rpm:ro" \
  "$image" bash -s <<'CONTAINER'
set -euo pipefail

dnf install -y /tmp/bongocat.rpm desktop-file-utils

rpm -q bongocat
rpm -V bongocat > /tmp/bongocat-rpm-verify.txt
if [[ -s /tmp/bongocat-rpm-verify.txt ]]; then
  cat /tmp/bongocat-rpm-verify.txt >&2
  exit 1
fi
test -x /usr/bin/BongoCat
test -L /usr/bin/bongocat
test -x /usr/libexec/bongocat/BongoCat
test -x /usr/libexec/bongocat/bongocat-update-rpm.sh
test -f /usr/libexec/bongocat/assets/models/standard/cat.model3.json
test -f /usr/share/applications/bongocat.desktop
test -f /usr/share/icons/hicolor/512x512/apps/bongocat.png
desktop-file-validate /usr/share/applications/bongocat.desktop

ldd /usr/libexec/bongocat/BongoCat > /tmp/bongocat-ldd.txt
if grep -q 'not found' /tmp/bongocat-ldd.txt; then
  cat /tmp/bongocat-ldd.txt >&2
  exit 1
fi

dnf remove -y bongocat
! rpm -q bongocat >/dev/null 2>&1
test ! -e /usr/libexec/bongocat
test ! -e /usr/share/applications/bongocat.desktop
test ! -e /usr/share/icons/hicolor/512x512/apps/bongocat.png

echo 'RPM install, dependency, and removal checks passed.'
CONTAINER
