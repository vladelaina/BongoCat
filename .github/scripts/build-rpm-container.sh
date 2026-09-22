#!/usr/bin/env bash
# Build an EL9 RPM inside a RHEL-compatible container.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
image="${RPM_BUILD_IMAGE:-quay.io/centos/centos:stream9}"
build_dir="${RPM_BUILD_DIR:-build-rpm-el9}"
jobs="${RPM_BUILD_JOBS:-2}"
engine="${CONTAINER_ENGINE:-}"

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  echo "RPM_BUILD_JOBS must be a positive integer: $jobs" >&2
  exit 1
fi

if [[ -z $engine ]]; then
  if command -v docker >/dev/null 2>&1; then
    engine=docker
  elif command -v podman >/dev/null 2>&1; then
    engine=podman
  else
    echo 'docker or podman is required for the RPM build.' >&2
    exit 1
  fi
fi

"$engine" run --rm -i \
  --security-opt label=disable \
  --volume "$repo_root:/src" \
  --workdir /src \
  "$image" bash -s <<CONTAINER
set -euo pipefail

dnf -y install dnf-plugins-core >/tmp/dnf-plugins.log
dnf config-manager --set-enabled crb >/tmp/dnf-crb.log
dnf -y install \
  cmake ninja-build gcc gcc-c++ rpm-build \
  libcurl-devel mesa-libGL-devel libX11-devel libXi-devel libXfixes-devel \
  desktop-file-utils file gzip tar which >/tmp/dnf-build.log

cmake -S . -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBONGO_CAT_OPTIMIZE_RELEASE_SIZE=ON \
  -DBONGO_CAT_OPTIMIZE_RELEASE_IPO=ON \
  -DBONGO_CAT_REQUIRE_CUBISM=ON \
  -DBONGO_CAT_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --parallel "$jobs"
cmake --build "$build_dir" --target package-rpm
CONTAINER
