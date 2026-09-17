#!/usr/bin/env bash
# BongoCat launcher installed at /opt/BongoCat/run.sh.
#
# An X11 client cannot read the global keyboard/pointer state under Wayland, so
# the read-only evdev backend is enabled only for Wayland sessions. Native X11
# keeps using XInput2 and needs no extra backend. Detection happens at every
# launch, so switching between X11 and Wayland keeps working after install.
set -euo pipefail
cd /opt/BongoCat

if [[ -n "${WAYLAND_DISPLAY:-}" ]] || [[ "${XDG_SESSION_TYPE:-}" == "wayland" ]]; then
  export BONGOCAT_ENABLE_EVDEV=1
fi

exec ./BongoCat "$@"
