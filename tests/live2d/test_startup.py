#!/usr/bin/env python3
"""Require a visible standard model from the real full Cubism CLI.

Usage: python3 tests/live2d/test_startup.py /path/to/BongoCat /new/evidence
Needs a graphical desktop and the full SDK build, with no other BongoCat running.
Evidence includes logs, isolated storage, frame series and a frame for inspection.
"""
import argparse
import csv
import datetime
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--model", choices=("standard", "keyboard", "gamepad"), default="standard")
    parser.add_argument("--exit-ms", type=int, default=1500)
    parser.add_argument("--deadline", type=float, default=15)
    parser.add_argument("--ignore-input", action="store_true")
    parser.add_argument("--cwd", type=Path)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    storage = output / "storage"
    command = [str(binary), "--ci-smoke", f"--ci-model={args.model}",
               "--ci-frame-series", f"--ci-exit-ms={args.exit_ms}",
               f"--storage-root={storage}"]
    if args.ignore_input:
        command.append("--ci-ignore-global-input")
    env = dict(os.environ, BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN="1")
    report = {"command": command, "storage_root": str(storage),
              "working_directory": str(args.cwd.resolve()) if args.cwd else os.getcwd(),
              "environment_overrides": {"BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN": "1"},
              "deadline_seconds": args.deadline,
              "started_at": datetime.datetime.now().astimezone().isoformat(),
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    (output / "launch.json").write_text(json.dumps(report, indent=2) + "\n")
    start = time.monotonic()
    with (output / "stdout.log").open("wb") as out, (output / "stderr.log").open("wb") as err:
        process = subprocess.Popen(command, cwd=args.cwd, env=env, stdout=out, stderr=err)
        report["pid"] = process.pid
        report["timeout"] = False
        try:
            report["returncode"] = process.wait(timeout=args.deadline)
        except subprocess.TimeoutExpired:
            report["timeout"] = True
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            report["returncode"] = process.returncode
    report["duration_seconds"] = time.monotonic() - start
    stdout = (output / "stdout.log").read_text(errors="replace")
    stderr = (output / "stderr.log").read_text(errors="replace")
    state_file = storage / "state/runtime-state.txt"
    report["runtime_state"] = state_file.read_text() if state_file.exists() else None
    frames = storage / "state/frame-series.csv"
    rows = []
    if frames.exists():
        with frames.open() as frame_file:
            rows = list(csv.DictReader(frame_file))
    first_visible = next((index for index, row in enumerate(rows)
                          if row["window_os_visible"] == "1"), len(rows))
    visible_rows = rows[first_visible:]
    checks = {
        "bounded_normal_exit": report["returncode"] == 0 and not report["timeout"],
        "actual_initialization": "[runtime] Process started:" in stderr and "Startup stage: window-ready" in stderr,
        "full_cubism_runtime": "Live2D Cubism SDK Core Version" in stdout and "diagnostic backend" not in stdout + stderr,
        "selected_model_completed": f"Model load completed: id={args.model}" in stderr,
        "startup_ready": "[runtime] Startup ready" in stderr,
        "clean_shutdown": "Shutdown started: stage=shutdown:normal exit_code=0" in stderr and "Shutdown complete: exit_code=0" in stderr,
        "visible_frame_readback": "First-frame diagnosis: OpenGL framebuffer contains visible content" in stderr,
        "visible_model_frames": len(visible_rows) >= 2 and all(
            row["model_mode"] == args.model and row["model_state_consistent"] == "1"
            and row.get("loaded_model") == args.model
            and row.get("active_model") == args.model
            and int(row["visible_pixels"]) > 100 and int(row["alpha_pixels"]) > 100
            and row["window_config_visible"] == "1" and row["window_os_visible"] == "1"
            for row in visible_rows),
        "consistent_frame_context": bool(rows) and all(
            row.get("context_current") == "1"
            and row.get("gl_error_before") == "0"
            and row.get("gl_error_after") == "0" for row in rows),
        "preserved_frame": (storage / "state/frame.bmp").is_file(),
        "no_startup_or_gl_error": "Startup failed:" not in stderr and "[ERROR:" not in stderr
            and "OpenGL framebuffer readback failed" not in stderr
            and "[CSM][E]" not in stdout + stderr
            and not re.search(r"(?:gl_|state_|restore_)?error(?:_before|_after)?=0x0*[1-9a-fA-F][0-9a-fA-F]*", stdout + stderr),
    }
    report.update(checks=checks, frame_count=len(rows),
                  visible_frame_count=len(visible_rows),
                  preparation_frame_count=first_visible, passed=all(checks.values()))
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "returncode": report["returncode"],
                      "duration_seconds": round(report["duration_seconds"], 3),
                      "runtime_state": report["runtime_state"],
                      "failed_checks": [name for name, passed in checks.items() if not passed]}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
