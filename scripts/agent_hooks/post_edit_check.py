#!/usr/bin/env python3
"""PostToolUse hook: report lint and format findings for the edited files.

Runs ``pre-commit run --files <paths>`` from .pre-commit-config.yaml and hands
the agent the failing hooks' output plus, for any file a fixer would have
changed, the diff it wanted. It never leaves a file rewritten: each file's
bytes and timestamps are captured first and put back afterwards, because an
autofix under the agent makes its next Edit fail with "file modified since
read". The agent applies the fix itself.

Feedback goes out as ``hookSpecificOutput.additionalContext`` JSON on stdout,
which Claude Code and Codex both add next to the tool result. Always exits 0.

Without pre-commit, its config or the installed git hook, it prints one line
(the agent's debug log) and exits instead of spending the hook timeout
installing hook environments. session_start.py already told the agent how to
bootstrap.
"""

# pyright: strict

from __future__ import annotations

import dataclasses
import difflib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

import hooklib

# Below the 30s hook timeout in .claude/settings.json and .codex/hooks.json, so
# the restore below always runs.
PRE_COMMIT_TIMEOUT = 25
CONTEXT_LIMIT = 6000
HOOK_LINE = re.compile(r"^(.+?)\.{3,}(\(no files to check\))?(Passed|Failed|Skipped)$")


@dataclasses.dataclass(frozen=True)
class Snapshot:
    path: Path
    data: bytes
    times: tuple[int, int]

    @classmethod
    def take(cls, path: Path) -> Snapshot:
        stat = path.stat()
        return cls(path, path.read_bytes(), (stat.st_atime_ns, stat.st_mtime_ns))

    def restore(self) -> str | None:
        """Put the file back; returns the diff a hook wanted, if any."""
        try:
            now = self.path.read_bytes()
        except OSError:
            now = b""
        if now == self.data:
            return None
        self.path.write_bytes(self.data)
        os.utime(self.path, ns=self.times)
        before = self.data.decode("utf-8", errors="replace").splitlines(keepends=True)
        after = now.decode("utf-8", errors="replace").splitlines(keepends=True)
        name = self.path.name
        return "".join(difflib.unified_diff(before, after, f"a/{name}", f"b/{name}"))


def run_process_tree(command: list[str], cwd: Path, timeout: float) -> tuple[str, bool]:
    """Run command and return (output, failed); on timeout kill it and every child.

    pre-commit launches the formatters as child processes. Killing only
    pre-commit on timeout would leave a fixer running that could rewrite a file
    after the snapshots are restored, so the command gets its own process group
    (session on POSIX) and the whole group is killed and reaped first.
    """
    if sys.platform == "win32":
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
    else:
        process = subprocess.Popen(
            command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True
        )
    try:
        output, _ = process.communicate(timeout=timeout)
        return output, process.returncode != 0
    except subprocess.TimeoutExpired:
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True, check=False)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        return f"pre-commit did not finish in {timeout:g}s; run it yourself.", True


def failing_sections(output: str) -> str:
    """Only the output of hooks that failed (pre-commit prints every hook)."""
    kept: list[str] = []
    keep = False
    for line in output.splitlines():
        status = HOOK_LINE.match(line.strip())
        if status:
            keep = status.group(3) == "Failed"
        if keep:
            kept.append(line)
    return "\n".join(kept)


def main() -> int:
    payload = hooklib.read_payload()
    root = hooklib.project_root(payload)
    if root is None:
        return 0
    paths = [edit.path for edit in hooklib.planned_edits(payload) if edit.path.is_file()]
    relative = [rel for rel in (hooklib.relative(root, path) for path in paths) if rel is not None]
    if not relative:
        return 0
    status = hooklib.hooks_status(root)
    if not (root / ".pre-commit-config.yaml").is_file() or status:
        print(status or "post_edit_check: no .pre-commit-config.yaml, nothing to check")
        return 0
    snapshots = [Snapshot.take(root / rel) for rel in relative]
    try:
        output, failed = run_process_tree(
            ["pre-commit", "run", "--color", "never", "--files", *relative], root, PRE_COMMIT_TIMEOUT
        )
    finally:
        diffs = [diff for diff in (snapshot.restore() for snapshot in snapshots) if diff]
    if not failed and not diffs:
        return 0
    parts = [f"pre-commit findings for {', '.join(relative)} (nothing was rewritten; fix them yourself):"]
    if diffs:
        parts.append("A fixer wanted these changes:\n" + "\n".join(diffs))
    sections = failing_sections(output) if failed else ""
    if sections or (failed and not diffs):
        parts.append(sections or output.strip())
    context = "\n\n".join(parts)
    if len(context) > CONTEXT_LIMIT:
        context = context[:CONTEXT_LIMIT] + "\n[truncated; run pre-commit run --files on the file for the rest]"
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": context}}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as error:
        print(f"post_edit_check: skipped ({error})")
        raise SystemExit(0) from None
