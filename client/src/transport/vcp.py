"""Legacy import path (smc_core.transport.serial)."""
import os as _os
import sys as _sys
# Migration shim: smc_core lives in client/, one level above src/.
_CLIENT = _os.path.dirname(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
if _CLIENT not in _sys.path:
    _sys.path.insert(0, _CLIENT)

import serial  # noqa: F401,E402  (tests patch transport.vcp.serial.*)
import serial.tools.list_ports  # noqa: F401,E402
from smc_core.transport.serial import VcpTransport  # noqa: F401,E402
