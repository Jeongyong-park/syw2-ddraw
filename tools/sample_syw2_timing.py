"""Compatibility entry point. Implementation: tools.perf.sample_syw2_timing."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
if __name__ == "__main__":
    from runpy import run_module
    run_module("tools.perf.sample_syw2_timing", run_name="__main__")
else:
    from tools.perf.sample_syw2_timing import *
