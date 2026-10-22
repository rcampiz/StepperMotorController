"""Bluetooth pairing of a PS4 pad through bluetoothctl, against a scripted bluetoothctl."""

import queue

from smc_core import bt_pair


class ScriptedCtl:
    """Answers each command with the lines bluetoothctl would print."""

    def __init__(self, replies):
        self.replies = replies
        self.sent = []
        self.lines = queue.Queue()
        self.closed = False

    def send(self, command):
        self.sent.append(command)
        for line in self.replies.get(command.split()[0] + (" " + command.split()[1] if command.startswith("scan") else ""), []):
            self.lines.put(line)

    def wait_for(self, match, timeout, log):
        return bt_pair.Bluetoothctl.wait_for(self, match, min(timeout, 0.3), log)

    def close(self):
        self.closed = True


MAC = "A4:AE:12:34:56:78"


def test_a_sony_pad_is_told_from_other_devices_in_bluetoothctl_output():
    assert bt_pair.sony_device("[NEW] Device %s Wireless Controller" % MAC) == MAC
    assert bt_pair.sony_device("[CHG] Device %s Name: Wireless Controller" % MAC.lower()) == MAC
    assert bt_pair.sony_device("[NEW] Device 11:22:33:44:55:66 JBL Flip 5") is None
    assert bt_pair.sony_device("[CHG] Controller 00:11:22:33:44:55 Discovering: yes") is None


def test_a_pad_in_pairing_mode_is_paired_trusted_and_connected():
    ctl = ScriptedCtl({
        "scan on": ["Discovery started", "[NEW] Device 11:22:33:44:55:66 JBL Flip 5",
                    "[NEW] Device %s Wireless Controller" % MAC],
        "pair": ["Attempting to pair with %s" % MAC, "Pairing successful"],
        "trust": ["Changing %s trust succeeded" % MAC],
        "connect": ["Attempting to connect to %s" % MAC, "Connection successful"],
    })
    log = []
    assert bt_pair.pair_playstation_pad(log.append, scan_s=1.0, ctl_factory=lambda: ctl) == MAC
    assert ctl.sent[:4] == ["power on", "agent NoInputNoOutput", "default-agent", "scan on"]
    assert ctl.sent[4:] == ["scan off", "pair " + MAC, "trust " + MAC, "connect " + MAC]
    assert ctl.closed and any("Connected" in l for l in log)


def test_no_pad_seen_and_a_failed_pairing_say_why():
    ctl = ScriptedCtl({"scan on": ["Discovery started"]})
    log = []
    assert bt_pair.pair_playstation_pad(log.append, scan_s=0.3, ctl_factory=lambda: ctl) is None
    assert "pairing mode" in log[-1] and ctl.closed
    ctl = ScriptedCtl({"scan on": ["[NEW] Device %s Wireless Controller" % MAC],
                       "pair": ["Failed to pair: org.bluez.Error.AuthenticationFailed"]})
    log = []
    assert bt_pair.pair_playstation_pad(log.append, scan_s=1.0, ctl_factory=lambda: ctl) is None
    assert "Pairing failed" in log[-1] and "trust " + MAC not in ctl.sent
