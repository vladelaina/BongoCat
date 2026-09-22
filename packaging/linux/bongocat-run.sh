#!/bin/sh
set -eu

runtime=/usr/libexec/bongocat
cd "$runtime"

if [ "${XDG_SESSION_TYPE:-}" = "wayland" ] ||
    [ -n "${WAYLAND_DISPLAY:-}" ]; then
  BONGO_CAT_ENABLE_EVDEV=1
  export BONGO_CAT_ENABLE_EVDEV
fi

exec "$runtime/BongoCat" "$@"
