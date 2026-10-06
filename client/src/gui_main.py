#!/usr/bin/env python3
"""Legacy launcher: the GUI now lives in client/smc_gui (python -m smc_gui)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from smc_gui.__main__ import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
