#!/usr/bin/env python3
"""Consume artifacts while builds run; one worker owns the shared VT quota.

Both workflows must use the same job concurrency group. The initial cooldown
protects the quota when a previous worker finished or was cancelled abruptly.
"""

from __future__ import annotations

import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import time
import zipfile


spec = importlib.util.spec_from_file_location(
    "scan_virustotal", Path(__file__).with_name("scan-virustotal.py")
)
vt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vt)


def github_bytes(endpoint: str) -> bytes:
    result = subprocess.run(
        ["gh", "api", endpoint], capture_output=True, check=False
    )
    if result.returncode:
        raise vt.VirusTotalError(
            f"GitHub artifact discovery/download failed: {result.stderr.decode(errors='replace')}"
        )
    return result.stdout


def github_items(endpoint: str, key: str) -> list[dict]:
    items = []
    separator = "&" if "?" in endpoint else "?"
    page = 1
    while True:
        data = json.loads(github_bytes(f"{endpoint}{separator}per_page=100&page={page}"))
        batch = data[key]
        items.extend(batch)
        if len(batch) < 100:
            return items
        page += 1


class ArtifactSource:
    def __init__(self, prefix: str, producer_prefix: str, directory: Path):
        self.repo = f"repos/{os.environ['GITHUB_REPOSITORY']}"
        self.run = f"{self.repo}/actions/runs/{os.environ['GITHUB_RUN_ID']}"
        self.prefix = prefix
        self.producer_prefix = producer_prefix
        self.directory = directory
        self.seen: set[int] = set()

    def discover(self) -> list[Path]:
        files = []
        for artifact in github_items(f"{self.run}/artifacts", "artifacts"):
            artifact_id = artifact["id"]
            if not artifact["name"].startswith(self.prefix) or artifact_id in self.seen:
                continue
            if artifact["expired"]:
                raise vt.VirusTotalError(f"Scan artifact expired: {artifact_id}")
            payload = github_bytes(f"{self.repo}/actions/artifacts/{artifact_id}/zip")
            destination = (self.directory / str(artifact_id)).resolve()
            with zipfile.ZipFile(io.BytesIO(payload)) as archive:
                for member in archive.infolist():
                    target = (destination / member.filename).resolve()
                    if not target.is_relative_to(destination):
                        raise vt.VirusTotalError("Unsafe path in artifact archive")
                    if member.is_dir() or target.name.lower().endswith(".sha256"):
                        continue
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(member))
                    files.append(target)
            self.seen.add(artifact_id)
        return files

    def finished(self) -> bool:
        # Include earlier successful jobs on a failed-jobs rerun, keeping the
        # newest job for each matrix name. Queued jobs are included by the API.
        jobs = github_items(f"{self.run}/jobs?filter=all", "jobs")
        latest = {}
        for job in sorted(jobs, key=lambda item: item["id"]):
            latest[job["name"]] = job
        producers = [job for name, job in latest.items()
                     if name.startswith(self.producer_prefix)]
        others = [job for name, job in latest.items()
                  if name != "security / virustotal"]
        # The second condition also handles a skipped matrix after an upstream
        # failure, for which GitHub may expose a single unexpanded job.
        return bool(producers) and (
            (len(producers) >= 5 and all(job["status"] == "completed" for job in producers))
            or (bool(others) and all(job["status"] == "completed" for job in others))
        )


def watch(source: ArtifactSource, api_key: str, reports: Path,
          timeout: int = 18000, analysis_timeout: int = 900) -> None:
    deadline = time.monotonic() + timeout
    pending = []
    uploaded = 0
    failures = []
    reports.mkdir(parents=True, exist_ok=True)
    # The concurrency lock excludes other CI/release workers; this covers the
    # previous holder's final requests even if it could not persist any state.
    print("Waiting 120s for the previous VirusTotal worker's quota to expire", flush=True)
    time.sleep(vt.REQUEST_WINDOW_SECONDS)
    while True:
        if time.monotonic() >= deadline:
            raise vt.VirusTotalError("Timed out waiting for build artifacts/scans")
        finished = source.finished()
        # Discover AFTER checking jobs so final uploads cannot fall between the
        # last discovery and the decision to stop.
        ready = source.discover()
        for path in ready:
            sha256 = vt.file_sha256(path)
            print(f"Submitting completed artifact: {path.name}", flush=True)
            analysis_id = vt.upload(path, api_key)
            uploaded += 1
            pending.append((path, sha256, analysis_id, time.monotonic(), uploaded))
        if pending and not ready:
            # One poll per discovery pass. New artifacts take priority over
            # polling, and a slow analysis never blocks other uploads.
            path, sha256, analysis_id, started, index = pending.pop(0)
            response = vt.api_request("GET", f"{vt.API_ROOT}/analyses/{analysis_id}", api_key)
            attributes = response.get("data", {}).get("attributes", {})
            status = attributes.get("status", "unknown")
            if status == "completed":
                vt.write_summary(path, sha256, attributes,
                                 reports / f"virustotal-report-{index}-{path.name}.json")
            elif status in ("failure", "timeout") or time.monotonic() - started >= analysis_timeout:
                failures.append(f"{path.name}: analysis {status} or polling timed out")
            else:
                pending.append((path, sha256, analysis_id, started, index))
        if finished and not pending:
            if not uploaded:
                raise vt.VirusTotalError("No packaged artifacts found for VirusTotal")
            if failures:
                raise vt.VirusTotalError("; ".join(failures))
            return
        if not ready:
            time.sleep(15)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-prefix", required=True)
    parser.add_argument("--producer-prefix", required=True)
    args = parser.parse_args()
    api_key = os.environ.get("VIRUSTOTAL_API_KEY", "").strip()
    if not api_key:
        vt.write_skip_summary("VIRUSTOTAL_API_KEY is not configured")
        return 0
    try:
        watch(ArtifactSource(args.artifact_prefix, args.producer_prefix, Path("vt-input")),
              api_key, Path("."))
    except (vt.VirusTotalError, OSError, ValueError, zipfile.BadZipFile) as exc:
        print(f"::error::{vt.escape_workflow_value(exc)}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
