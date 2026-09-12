"""Compatibility entry point. Implementation: tools.widescreen.verify_widescreen_alert."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
if __name__ == "__main__":
    from runpy import run_module
    run_module("tools.widescreen.verify_widescreen_alert", run_name="__main__")
else:
    from tools.widescreen.verify_widescreen_alert import *
