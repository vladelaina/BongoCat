#!/usr/bin/env python3
"""Validate store configuration and prepare signing inputs without logging secrets."""

import base64
import datetime
import hashlib
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys


SIGNING_SECRETS = (
    "APPLE_APP_CERTIFICATE_BASE64", "APPLE_INSTALLER_CERTIFICATE_BASE64",
    "APPLE_CERTIFICATE_PASSWORD", "APPLE_PROVISION_PROFILE_BASE64",
)


def check_configuration():
    required = SIGNING_SECRETS + (
        "APPLE_TEAM_ID", "APPLE_BUNDLE_ID", "APPLE_BUILD_NUMBER",
        "ASC_KEY_ID", "ASC_ISSUER_ID", "ASC_PRIVATE_KEY",
    )
    missing = [name for name in required if not os.environ.get(name, "").strip()]
    if missing:
        raise ValueError("Missing GitHub Secrets / Variables: " + ", ".join(missing))
    patterns = {
        "APPLE_TEAM_ID": r"[A-Z0-9]{10}",
        "APPLE_BUNDLE_ID": r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+",
        "APPLE_BUILD_NUMBER": r"[1-9][0-9]{0,3}(?:\.(?:0|[1-9][0-9]?)){0,2}",
        "ASC_KEY_ID": r"[A-Z0-9]{10}",
        "ASC_ISSUER_ID": r"[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}",
    }
    for name, pattern in patterns.items():
        if not re.fullmatch(pattern, os.environ[name]):
            raise ValueError(f"Invalid format: {name}")
    if "-----BEGIN PRIVATE KEY-----" not in os.environ["ASC_PRIVATE_KEY"]:
        raise ValueError("ASC_PRIVATE_KEY must contain the full .p8 text, not base64")


def profile_entitlements(profile, team, bundle, now):
    if profile.get("ExpirationDate", datetime.datetime.min) <= now:
        raise ValueError("Provisioning profile has expired")
    if team not in profile.get("TeamIdentifier", []):
        raise ValueError("Provisioning profile Team ID does not match APPLE_TEAM_ID")
    if "OSX" not in profile.get("Platform", []):
        raise ValueError("Provisioning profile must be for macOS")
    source = profile.get("Entitlements", {})
    if (profile.get("ProvisionedDevices") or profile.get("ProvisionsAllDevices")
            or source.get("get-task-allow") or source.get("com.apple.security.get-task-allow")):
        raise ValueError("Use a Mac App Store distribution profile, not development / Developer ID")
    app_id = source.get("com.apple.application-identifier", "")
    prefixes = profile.get("ApplicationIdentifierPrefix", [])
    if not any(app_id == f"{prefix}.{bundle}" for prefix in prefixes):
        raise ValueError("Provisioning profile must match the explicit APPLE_BUNDLE_ID")
    if source.get("com.apple.developer.team-identifier") != team:
        raise ValueError("Profile entitlement Team ID does not match APPLE_TEAM_ID")
    return {
        "com.apple.application-identifier": app_id,
        "com.apple.developer.team-identifier": team,
        "com.apple.security.app-sandbox": True,
        "com.apple.security.files.user-selected.read-write": True,
        "com.apple.security.network.client": True,
    }


def prepare():
    work = Path(os.environ["RUNNER_TEMP"]) / "bongocat-store-signing"
    work.mkdir(mode=0o700, exist_ok=True)
    for name, filename in (
        ("APPLE_APP_CERTIFICATE_BASE64", "app.p12"),
        ("APPLE_INSTALLER_CERTIFICATE_BASE64", "installer.p12"),
        ("APPLE_PROVISION_PROFILE_BASE64", "profile.provisionprofile"),
    ):
        try:
            data = base64.b64decode("".join(os.environ[name].split()), validate=True)
        except ValueError as exc:
            raise ValueError(f"Invalid base64 in {name}") from exc
        (work / filename).write_bytes(data)
    decoded = subprocess.check_output([
        "security", "cms", "-D", "-i", str(work / "profile.provisionprofile")])
    profile = plistlib.loads(decoded)
    entitlements = profile_entitlements(
        profile, os.environ["APPLE_TEAM_ID"], os.environ["APPLE_BUNDLE_ID"],
        datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None))
    (work / "entitlements.plist").write_bytes(plistlib.dumps(entitlements))
    certificates = profile.get("DeveloperCertificates", [])
    if not certificates:
        raise ValueError("Provisioning profile contains no signing certificates")
    (work / "allowed-certificates.txt").write_text("\n".join(
        hashlib.sha1(cert).hexdigest().upper() for cert in certificates) + "\n")

    contents = Path("build-app-store/BongoCat.app/Contents")
    (contents / "embedded.provisionprofile").write_bytes(
        (work / "profile.provisionprofile").read_bytes())
    info_path = contents / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    info.update({
        "CFBundleIdentifier": os.environ["APPLE_BUNDLE_ID"],
        "CFBundleVersion": os.environ["APPLE_BUILD_NUMBER"],
        "LSApplicationCategoryType": "public.app-category.entertainment",
    })
    info_path.write_bytes(plistlib.dumps(info))


if __name__ == "__main__":
    try:
        if sys.argv[1:] == ["check"]:
            check_configuration()
        elif sys.argv[1:] == ["prepare"]:
            prepare()
        else:
            raise ValueError("Usage: prepare.py check|prepare")
    except (ValueError, KeyError) as error:
        sys.exit(str(error))
