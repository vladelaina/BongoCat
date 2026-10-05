"""Shared full-runtime evidence checks for the real application CLI verifiers."""
import re


def common_runtime_checks(stdout, stderr, frames):
    return {
        "full_runtime": "Live2D Cubism SDK Core Version" in stdout
            and "diagnostic backend" not in stdout + stderr,
        "normal_shutdown": "Shutdown started: stage=shutdown:normal exit_code=0" in stderr
            and "Shutdown complete: exit_code=0" in stderr,
        "frame_context_and_gl": bool(frames) and all(
            row.get("context_current") == "1"
            and row.get("gl_error_before") == "0"
            and row.get("gl_error_after") == "0" for row in frames),
        "no_render_errors": "[CSM][E]" not in stdout + stderr and "[ERROR:" not in stderr
            and not re.search(
                r"(?:gl_|state_|restore_)?error(?:_before|_after)?=0x0*[1-9a-fA-F][0-9a-fA-F]*",
                stdout + stderr),
    }
