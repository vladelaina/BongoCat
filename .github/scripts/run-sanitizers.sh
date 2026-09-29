#!/usr/bin/env bash

set -euo pipefail

executable="${1:?Application executable is required}"
reports="${2:?Diagnostic output directory is required}"
mkdir -p "$reports"

run_scenario() {
  local name="$1"
  shift
  local storage
  storage="$(mktemp -d "$reports/$name.XXXXXX")"
  mkdir -p "$storage/config"
  cat > "$storage/config/settings.json" <<'JSON'
{
  "format": "bongocat/settings",
  "schemaVersion": 1,
  "application": { "showTrayIcon": false }
}
JSON
  echo "Running sanitizer scenario: $name"
  echo "System tray disabled for the headless sanitizer session."
  timeout --kill-after=10s 60s "$executable" \
    --ci-smoke --ci-ignore-global-input --ci-exit-ms=2500 \
    "--storage-root=$storage" "$@" 2>&1 | tee "$storage/console.log"
}

run_scenario startup
for page in 0 1 2 3; do
  run_scenario "preferences-$page" --ci-preferences "--ci-preference-page=$page"
done
