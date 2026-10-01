#!/usr/bin/env python3
"""Scan release artifacts in batches of four and publish CI summaries.

Detections are advisory because unsigned Windows software can trigger a small
number of heuristic engines. API and report-processing failures still fail the
scan job, while the independent release job remains available.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request
import uuid
from collections import deque
from pathlib import Path
from typing import Any


API_ROOT = "https://www.virustotal.com/api/v3"
MAX_DIRECT_UPLOAD_BYTES = 32 * 1024 * 1024
REQUEST_WINDOW_SECONDS = 120
REQUEST_LIMIT = 4
BATCH_SIZE = 4
BATCH_DELAY_SECONDS = 120
_request_times: deque[float] = deque()


class VirusTotalError(RuntimeError):
    """An API or result-processing failure."""


def escape_workflow_value(value: object) -> str:
    """Keep third-party result text from creating workflow commands."""
    return (
        str(value)
        .replace("%", "%25")
        .replace("\r", "%0D")
        .replace("\n", "%0A")
    )


def escape_markdown_cell(value: object) -> str:
    return (
        str(value)
        .replace("`", "\\`")
        .replace("|", "\\|")
        .replace("\r", " ")
        .replace("\n", " ")
    )


def api_request(
    method: str,
    url: str,
    api_key: str,
    body: bytes | None = None,
    content_type: str | None = None,
) -> dict[str, Any]:
    headers = {
        "accept": "application/json",
        "x-apikey": api_key,
        "user-agent": "BongoCat-VirusTotal-CI/1",
    }
    if body is not None:
        headers["content-type"] = content_type or "application/octet-stream"

    request = urllib.request.Request(
        url, data=body, headers=headers, method=method
    )
    for attempt in range(5):
        # Upload URLs, uploads, polling and retries all consume API quota.
        while _request_times:
            now = time.monotonic()
            if now - _request_times[0] >= REQUEST_WINDOW_SECONDS:
                _request_times.popleft()
            elif len(_request_times) >= REQUEST_LIMIT:
                delay = REQUEST_WINDOW_SECONDS - (now - _request_times[0])
                print(f"VirusTotal request quota used; waiting {delay:.1f}s")
                time.sleep(delay)
            else:
                break
        _request_times.append(time.monotonic())
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                payload = response.read()
            try:
                return json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise VirusTotalError(
                    "VirusTotal returned invalid JSON"
                ) from exc
        except urllib.error.HTTPError as exc:
            response_body = exc.read().decode("utf-8", errors="replace")
            exc.close()
            if exc.code in (429, 500, 502, 503, 504) and attempt < 4:
                minimum_delay = 120 if exc.code == 429 else 5
                retry_after = exc.headers.get("Retry-After", str(minimum_delay))
                try:
                    delay = max(int(retry_after), minimum_delay)
                except ValueError:
                    delay = minimum_delay
                print(
                    f"VirusTotal HTTP {exc.code}; retrying in {delay}s",
                    file=sys.stderr,
                )
                time.sleep(delay)
                continue
            detail = response_body[:500].replace("\n", " ")
            raise VirusTotalError(
                f"VirusTotal HTTP {exc.code}: {detail}"
            ) from exc
        except (urllib.error.URLError, TimeoutError) as exc:
            if attempt < 4:
                delay = 2**attempt
                print(
                    f"VirusTotal request failed; retrying in {delay}s",
                    file=sys.stderr,
                )
                time.sleep(delay)
                continue
            raise VirusTotalError(
                f"VirusTotal request failed: {exc}"
            ) from exc

    raise VirusTotalError("VirusTotal request failed after retries")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_multipart(path: Path) -> tuple[bytes, str]:
    boundary = f"----BongoCatVirusTotal{uuid.uuid4().hex}"
    name = path.name.replace('"', "")
    prefix = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{name}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8")
    suffix = f"\r\n--{boundary}--\r\n".encode("ascii")
    return (
        prefix + path.read_bytes() + suffix,
        f"multipart/form-data; boundary={boundary}",
    )


def upload(path: Path, api_key: str) -> str:
    upload_url = f"{API_ROOT}/files"
    if path.stat().st_size > MAX_DIRECT_UPLOAD_BYTES:
        response = api_request(
            "GET", f"{API_ROOT}/files/upload_url", api_key
        )
        upload_data = response.get("data", {})
        if isinstance(upload_data, str):
            upload_url = upload_data
        elif isinstance(upload_data, dict):
            attributes = upload_data.get("attributes", {})
            links = upload_data.get("links", {})
            if not isinstance(attributes, dict):
                attributes = {}
            if not isinstance(links, dict):
                links = {}
            upload_url = (
                attributes.get("url", "")
                or links.get("self", "")
                or upload_data.get("id", "")
                or upload_data.get("url", "")
            )
        else:
            upload_url = ""
        if not upload_url:
            raise VirusTotalError(
                "VirusTotal did not return an upload URL"
            )

    body, content_type = build_multipart(path)
    response = api_request(
        "POST", upload_url, api_key, body, content_type
    )
    response_data = response.get("data", {})
    analysis_id = (
        response_data.get("id", "")
        if isinstance(response_data, dict)
        else ""
    )
    if not analysis_id:
        raise VirusTotalError(
            "VirusTotal upload response did not contain an analysis ID"
        )
    return analysis_id


def wait_for_analysis(
    analysis_id: str,
    api_key: str,
    timeout_seconds: int,
    poll_seconds: int,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    url = f"{API_ROOT}/analyses/{analysis_id}"
    while True:
        response = api_request("GET", url, api_key)
        response_data = response.get("data", {})
        attributes = (
            response_data.get("attributes", {})
            if isinstance(response_data, dict)
            else {}
        )
        if not isinstance(attributes, dict):
            attributes = {}
        status = attributes.get("status", "unknown")
        print(f"VirusTotal analysis status: {status}")
        if status == "completed":
            return attributes
        if status in ("failure", "timeout"):
            raise VirusTotalError(
                f"VirusTotal analysis ended with status: {status}"
            )
        if time.monotonic() >= deadline:
            raise VirusTotalError(
                "VirusTotal analysis polling timed out"
            )
        time.sleep(poll_seconds)


def write_summary(
    path: Path, sha256: str, attributes: dict[str, Any],
    report_path: Path | None = None,
) -> None:
    stats = attributes.get("stats", {})
    if not isinstance(stats, dict):
        stats = {}
    keys = (
        "malicious",
        "suspicious",
        "harmless",
        "undetected",
        "timeout",
        "failure",
    )
    counts = {key: int(stats.get(key, 0) or 0) for key in keys}
    detections = []
    results = attributes.get("results", {})
    if not isinstance(results, dict):
        results = {}
    for engine, result in results.items():
        if not isinstance(result, dict):
            continue
        category = result.get("category")
        if category in ("malicious", "suspicious"):
            detections.append(
                {
                    "engine": str(engine),
                    "category": category,
                    "result": result.get("result") or "unspecified",
                }
            )
    detections.sort(key=lambda item: (item["category"], item["engine"]))

    report = {
        "file_name": path.name,
        "sha256": sha256,
        "status": attributes.get("status"),
        "stats": counts,
        "detections": detections,
        "file_report_url": (
            f"https://www.virustotal.com/gui/file/{sha256}"
        ),
    }
    report_path = report_path or Path(
        os.environ.get("VT_REPORT_PATH", "virustotal-report.json")
    )
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    total_flagged = counts["malicious"] + counts["suspicious"]
    if total_flagged:
        print(
            f"::warning::VirusTotal flagged {total_flagged} engine(s): "
            f"malicious={counts['malicious']}, "
            f"suspicious={counts['suspicious']}"
        )
        for item in detections[:20]:
            print(
                "::warning::VirusTotal "
                f"{escape_workflow_value(item['category'])} / "
                f"{escape_workflow_value(item['engine'])}: "
                f"{escape_workflow_value(item['result'])}"
            )
    else:
        print(
            "::notice::VirusTotal completed without malicious or "
            "suspicious detections "
            f"({counts['harmless']} harmless, "
            f"{counts['undetected']} undetected)"
        )

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return
    lines = [
        "## VirusTotal scan",
        "",
        f"- File: `{path.name}`",
        f"- SHA-256: `{sha256}`",
        "- Report: "
        f"[open in VirusTotal](https://www.virustotal.com/gui/file/{sha256})",
        "- Policy: advisory; review malicious or suspicious engine results",
        "- Counts: "
        + ", ".join(f"{key}={counts[key]}" for key in keys),
    ]
    if detections:
        lines.extend(
            [
                "",
                "### Malicious or suspicious engines",
                "",
                "| Engine | Category | Result |",
                "| --- | --- | --- |",
            ]
        )
        lines.extend(
            f"| `{escape_markdown_cell(item['engine'])}` | "
            f"`{escape_markdown_cell(item['category'])}` | "
            f"`{escape_markdown_cell(item['result'])}` |"
            for item in detections[:50]
        )
    else:
        lines.extend(
            ["", "No malicious or suspicious result was reported."]
        )
    with Path(summary_path).open("a", encoding="utf-8") as stream:
        stream.write("\n".join(lines) + "\n")


def write_skip_summary(reason: str) -> None:
    print(f"::notice::VirusTotal scan skipped: {reason}")
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with Path(summary_path).open("a", encoding="utf-8") as stream:
            stream.write(
                f"## VirusTotal scan\n\nScan skipped: {reason}\n"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", required=True, type=Path, nargs="+")
    parser.add_argument("--report-directory", type=Path)
    parser.add_argument("--require-api-key", action="store_true")
    parser.add_argument("--timeout-seconds", type=int, default=900)
    parser.add_argument("--poll-seconds", type=int, default=15)
    args = parser.parse_args()

    for path in args.file:
        if not path.is_file():
            print(
                f"VirusTotal input file does not exist: {path}",
                file=sys.stderr,
            )
            return 2
    api_key = os.environ.get("VIRUSTOTAL_API_KEY", "").strip()
    if not api_key:
        if args.require_api_key:
            print(
                "VIRUSTOTAL_API_KEY is required for this scan",
                file=sys.stderr,
            )
            return 2
        write_skip_summary("VIRUSTOTAL_API_KEY is not configured")
        return 0

    try:
        if args.report_directory:
            args.report_directory.mkdir(parents=True, exist_ok=True)
        for start in range(0, len(args.file), BATCH_SIZE):
            if start:
                print("Previous batch completed; waiting 120s before next batch")
                time.sleep(BATCH_DELAY_SECONDS)
            batch = args.file[start:start + BATCH_SIZE]
            print(f"VirusTotal batch {start // BATCH_SIZE + 1}: {len(batch)} files")
            pending = []
            # Submit the whole batch before waiting for any analysis result.
            for path in batch:
                sha256 = file_sha256(path)
                print(f"VirusTotal input: {path} ({path.stat().st_size} bytes)")
                print(f"VirusTotal SHA-256: {sha256}")
                pending.append((path, sha256, upload(path, api_key)))
            for index, (path, sha256, analysis_id) in enumerate(pending, start + 1):
                attributes = wait_for_analysis(
                    analysis_id, api_key,
                    max(args.timeout_seconds, 30),
                    max(args.poll_seconds, 5),
                )
                report_path = None
                if args.report_directory or len(args.file) > 1:
                    report_path = (args.report_directory or Path(".")) / (
                        f"virustotal-report-{index}-{path.name}.json"
                    )
                write_summary(path, sha256, attributes, report_path)
        return 0
    except VirusTotalError as exc:
        print(f"::error::VirusTotal scan failed: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
