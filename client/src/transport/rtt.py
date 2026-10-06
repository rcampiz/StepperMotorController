"""Legacy import path (smc_core.transport.rtt)."""
import os as _os
import sys as _sys
# Migration shim: smc_core lives in client/, one level above src/.
_CLIENT = _os.path.dirname(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
if _CLIENT not in _sys.path:
    _sys.path.insert(0, _CLIENT)

from smc_core.transport.rtt import RttTransport, PYLINK_AVAILABLE  # noqa: F401,E402
