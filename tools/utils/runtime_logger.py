
from __future__ import annotations

import os
import sys
import time
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

class RuntimeLogger:

    def __init__(self, tool_name: str, log_file: str | None = None):
        self.tool_name = tool_name
        self._steps: list[tuple[str, float]] = []
        self._start = time.monotonic()

        if log_file is None:
            log_dir = os.environ.get("NLCELL_RUNTIME_LOG")
            if log_dir:
                Path(log_dir).mkdir(parents=True, exist_ok=True)
                self._log_path = str(Path(log_dir) / f"{tool_name}.runtime.log")
            else:
                self._log_path = f"{tool_name}.runtime.log"
        else:
            self._log_path = log_file

        with open(self._log_path, "w") as f:
            pass

        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self._log(f"[{tool_name}] Started at {ts}")

    @contextmanager
    def step(self, name: str):
        self._log(f"  [{name}] started")
        t0 = time.monotonic()
        try:
            yield
        finally:
            elapsed = time.monotonic() - t0
            self._steps.append((name, elapsed))
            self._log(f"  [{name}] done in {_fmt(elapsed)}")

    def done(self):
        total = time.monotonic() - self._start
        self._log(f"\n[{self.tool_name}] Runtime Summary:")
        for name, elapsed in self._steps:
            self._log(f"  {name:<40s} {_fmt(elapsed):>12s}")
        self._log(f"  {'TOTAL':<40s} {_fmt(total):>12s}")
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self._log(f"[{self.tool_name}] Finished at {ts}")

    def _log(self, msg: str):
        print(msg, file=sys.stderr)
        with open(self._log_path, "a") as f:
            f.write(msg + "\n")

def _fmt(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:.2f}s"
    m, s = divmod(seconds, 60)
    if m < 60:
        return f"{int(m)}m {s:.2f}s"
    h, m = divmod(m, 60)
    return f"{int(h)}h {int(m)}m {s:.2f}s"
