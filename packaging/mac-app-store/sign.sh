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
security list-keychains -d user -s "$keychain"
for certificate in app installer; do
  security import "$work/$certificate.p12" -k "$keychain" \
    -P "$APPLE_CERTIFICATE_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productbuild
done
# The -T flags above authorize the signing tools. Some macOS runner images
# reject set-key-partition-list with "item could not be found" for imported
# distribution identities, so do not make that optional metadata operation a
# blocker for an otherwise valid temporary keychain.
security set-key-partition-list -S apple-tool:,apple:,codesign: \
  -k "$password" "$keychain" >/dev/null 2>&1 || true

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
# Only the shipped bundle should be public, never the temporary signing inputs.
# prepare.py writes the embedded profile under umask 077; normalize it along
# with other resources while preserving executable bits and directory access.
chmod -R a+rX "$app"
# codesign creates _CodeSignature/CodeResources. It must also be readable by
# ordinary users, so use the public-file mask before signing and packaging.
umask 022
codesign --force --sign "$(cat "$work/app-identity")" \
  --keychain "$keychain" --entitlements "$work/entitlements.plist" \
  --timestamp "$app"
codesign --verify --deep --strict --verbose=2 "$app"
python3 - "$app" <<'PY'
from pathlib import Path
import stat
import sys
app = Path(sys.argv[1])
for path in (app, *app.rglob('*')):
    mode = path.stat().st_mode
    required = 0o555 if stat.S_ISDIR(mode) or mode & 0o111 else 0o444
    if mode & required != required:
        raise SystemExit(f'Bundle entry is not accessible to ordinary users: {path}')
PY
mkdir -p build-app-store/dist
productbuild --component "$app" /Applications \
  --sign "$(cat "$work/installer-identity")" --keychain "$keychain" \
  --timestamp build-app-store/dist/BongoCat.pkg
pkgutil --check-signature build-app-store/dist/BongoCat.pkg
