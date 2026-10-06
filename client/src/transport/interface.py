"""Legacy import path (smc_core.transport.base)."""
import os as _os
import sys as _sys
# Migration shim: smc_core lives in client/, one level above src/.
_CLIENT = _os.path.dirname(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
if _CLIENT not in _sys.path:
    _sys.path.insert(0, _CLIENT)

from smc_core.transport.base import *  # noqa: F401,F403,E402
from smc_core.transport.base import ITransport, TransportError, TransportInfo  # noqa: F401,E402
