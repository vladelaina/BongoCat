#!/usr/bin/env bash
set -euo pipefail
umask 077

work="$RUNNER_TEMP/bongocat-store-signing"
keychain="$RUNNER_TEMP/bongocat-store-signing.keychain-db"
cleanup() {
  if [[ -f "$keychain" ]]; then
    security delete-keychain "$keychain"
  fi
  rm -rf "$work"
}
trap cleanup EXIT
python3 packaging/mac-app-store/prepare.py prepare

password="$(openssl rand -hex 32)"
security create-keychain -p "$password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$password" "$keychain"
for certificate in app installer; do
  security import "$work/$certificate.p12" -k "$keychain" \
    -P "$APPLE_CERTIFICATE_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productbuild
done
security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
  -k "$password" "$keychain" >/dev/null

# Select valid identities from this temporary keychain only, and ensure the app
# certificate is one of the certificates authorized by the provisioning profile.
security find-identity -v "$keychain" > "$work/identities.txt"
python3 - "$work" <<'PY'
import os
from pathlib import Path
import re
import sys
work = Path(sys.argv[1])
identities = re.findall(r'\b([0-9A-F]{40}) "([^"]+)"',
                        (work / 'identities.txt').read_text())
team = os.environ['APPLE_TEAM_ID']
allowed = (work / 'allowed-certificates.txt').read_text().splitlines()
for kind, prefixes in (
    ('app', ('Apple Distribution:', '3rd Party Mac Developer Application:')),
    ('installer', ('3rd Party Mac Developer Installer:',)),
):
    candidates = [(fingerprint, name) for fingerprint, name in identities
                  if name.startswith(prefixes) and name.endswith(f'({team})')
                  and (kind != 'app' or fingerprint in allowed)]
    if len(candidates) != 1:
        raise SystemExit(f'Expected one valid {kind} identity matching team/profile; '
                         'check certificate type, private key, expiry and profile.')
    fingerprint, name = candidates[0]
    # codesign accepts SHA-1; productbuild accepts the certificate common name.
    (work / f'{kind}-identity').write_text(fingerprint if kind == 'app' else name)
PY

app="build-app-store/BongoCat.app"
codesign --force --sign "$(cat "$work/app-identity")" \
  --keychain "$keychain" --entitlements "$work/entitlements.plist" \
  --timestamp "$app"
codesign --verify --deep --strict --verbose=2 "$app"
mkdir -p build-app-store/dist
productbuild --component "$app" /Applications \
  --sign "$(cat "$work/installer-identity")" --keychain "$keychain" \
  --timestamp build-app-store/dist/BongoCat.pkg
pkgutil --check-signature build-app-store/dist/BongoCat.pkg
