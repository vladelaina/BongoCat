#!/bin/sh
# Upgrade a package-managed BongoCat installation from a release RPM URL.
set -eu

url=
parent_pid=

while [ "$#" -gt 0 ]; do
  case "$1" in
    --url)
      [ "$#" -ge 2 ] || exit 2
      url=$2
      shift 2
      ;;
    --pid)
      [ "$#" -ge 2 ] || exit 2
      parent_pid=$2
      shift 2
      ;;
    *)
      echo "Unknown BongoCat update argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$url" in
  https://github.com/vladelaina/BongoCat/releases/download/*.x86_64.rpm) ;;
  *)
    echo 'Refusing an unexpected BongoCat update URL.' >&2
    exit 2
    ;;
esac
case "$parent_pid" in
  ''|*[!0-9]*)
    echo 'Invalid BongoCat process ID.' >&2
    exit 2
    ;;
esac

notify() {
  if command -v notify-send >/dev/null 2>&1; then
    notify-send 'BongoCat' "$1" >/dev/null 2>&1 || true
  fi
}

lock_root=${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}
lock_dir="$lock_root/bongocat-update-$parent_pid.lock"
if ! mkdir -m 700 "$lock_dir" 2>/dev/null; then
  exit 0
fi
cleanup() {
  rmdir "$lock_dir" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

if ! command -v pkexec >/dev/null 2>&1; then
  notify 'Package updates require pkexec and a PolicyKit agent.'
  exit 1
fi

if command -v dnf >/dev/null 2>&1; then
  dnf_path=$(command -v dnf)
  if ! pkexec "$dnf_path" install -y "$url"; then
    notify 'BongoCat update failed or was cancelled.'
    exit 1
  fi
else
  notify 'BongoCat automatic updates currently require DNF.'
  exit 1
fi

kill -TERM "$parent_pid" 2>/dev/null || true
attempt=0
while kill -0 "$parent_pid" 2>/dev/null && [ "$attempt" -lt 100 ]; do
  sleep 0.1
  attempt=$((attempt + 1))
done
kill -KILL "$parent_pid" 2>/dev/null || true
sleep 0.2

restart=${BONGO_CAT_RESTART_BINARY:-/usr/bin/bongocat}
if [ -x "$restart" ]; then
  if command -v setsid >/dev/null 2>&1; then
    setsid "$restart" </dev/null >/dev/null 2>&1 &
  else
    nohup "$restart" </dev/null >/dev/null 2>&1 &
  fi
fi
