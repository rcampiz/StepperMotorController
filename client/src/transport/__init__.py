"""Legacy import path; the transports now live in smc_core.transport."""
import os as _os
import sys as _sys
# Migration shim: smc_core lives in client/, one level above src/.
_CLIENT = _os.path.dirname(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
if _CLIENT not in _sys.path:
    _sys.path.insert(0, _CLIENT)

from smc_core.transport.base import (ITransport, TransportError, TransportInfo,  # noqa: E402
                                     ConnectionError, TimeoutError)
from smc_core.transport.serial import VcpTransport  # noqa: E402
from smc_core.transport.rtt import RttTransport, PYLINK_AVAILABLE  # noqa: E402

__all__ = ["ITransport", "TransportError", "TransportInfo", "ConnectionError", "TimeoutError",
           "VcpTransport", "RttTransport", "PYLINK_AVAILABLE"]
