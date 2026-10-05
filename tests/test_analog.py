import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from unittest.mock import patch
import nuphy_analog as analog

assert analog.axes((0, 0, 0, 0)) == (0, 0)
assert analog.axes((10, 0, 0, 0)) == (0, 0)
assert analog.axes((170, 0, 0, 0)) == (0, -16384)
assert analog.axes((330, 0, 0, 0)) == (0, -32767)
assert analog.axes((0, 330, 0, 0)) == (-32767, 0)
assert analog.axes((0, 0, 330, 0)) == (0, 32767)
assert analog.axes((0, 0, 0, 330)) == (32767, 0)
assert analog.axes((65535, 0, 0, 0)) == (0, -32767)
assert analog.axes((330, 0, 330, 0)) == (0, 0)
assert analog.axes((0, 330, 0, 330)) == (0, 0)
x, y = analog.axes((330, 0, 0, 330))
assert x > 0 and y < 0 and abs(math.hypot(x, y) - 32767) < 1
assert analog.axes((210, 0, 0, 0), full=410, deadzone=10) == (0, -16384)
assert analog.output((170, 0, 0, 0), (1, 1, 1), 0, True) == (0, -16384, False)
assert analog.output((330, 0, 0, 0), (1, 1, 1), 0, True) == (0, -32767, True)
for control, control_age, ready in (((0, 1, 1), 0, True), ((1, 0, 1), 0, True),
                                   ((1, 1, 1), 0.51, True), ((1, 1, 1), 0, False)):
    assert analog.output((330, 0, 0, 0), control, control_age, ready) == (0, 0, False)

# One travel change followed by 60 seconds of silence must not stop a held key.
travel = [0] * 4
assert not analog.update_travel(travel, 0x28, 100)
assert analog.update_travel(travel, analog.KEY_CODES[0], 170)
for _ in range(60 * 120):
    assert analog.output(travel, (1, 1, 0), 0.01, True) == (0, -16384, False)
analog.update_travel(travel, analog.KEY_CODES[0], 172)
assert travel[0] == 170  # Small sensor wobble should not create controller events.
analog.update_travel(travel, analog.KEY_CODES[0], 173)
assert travel[0] == 173
analog.update_travel(travel, analog.KEY_CODES[0], 9)
assert analog.output(travel, (1, 1, 0), 0, True) == (0, 0, False)

# Test real event encoding without touching uinput: coalesce changes and send
# only changed axes, while always letting a neutral release through immediately.
pad = analog.VirtualPad.__new__(analog.VirtualPad)
pad.fd, pad.last, pad.last_emit = 123, None, 0
packets = []
def write_events(fd, packet):
    assert fd == 123
    packets.append(list(analog.EVENT.iter_unpack(packet)))
    return len(packet)
with patch.object(analog.os, "write", write_events), patch.object(analog.time, "monotonic") as clock:
    clock.return_value = 10
    pad.emit(0, 0, False)
    clock.return_value = 10.02
    pad.emit(0, -16000, False)
    assert [(e[2], e[3], e[4]) for e in packets[-1]] == [(3, 1, -16000), (0, 0, 0)]
    pad.emit(0, -16000, False)
    assert len(packets) == 2
    clock.return_value = 10.025
    pad.emit(1000, -17000, False)
    assert len(packets) == 2
    clock.return_value = 10.04
    pad.emit(2000, -18000, False)
    assert pad.last == (2000, -18000, False) and len(packets) == 3
    clock.return_value = 10.041
    pad.emit(0, 0, False)
    assert pad.last == (0, 0, False) and len(packets) == 4
assert analog.parse_control(analog.CONTROL.pack(b"NAC1", 1, 1, 0, 0)) == (True, True, False)
for packet in (b"", b"x" * 80, analog.CONTROL.pack(b"BAD1", 1, 1, 0, 0),
               analog.CONTROL.pack(b"NAC1", 2, 1, 0, 0), analog.CONTROL.pack(b"NAC1", 1, 1, 0, 1)):
    try:
        analog.parse_control(packet)
    except ValueError:
        pass
    else:
        raise AssertionError("Malformed control packet accepted")
assert analog.socket_address(1000) == "\0nuphy-analog-1000"
assert analog.ioctl_write(3, 92) == 0x405C5503
assert analog.ioctl_write(4, 28) == 0x401C5504
print("PASS: depth mapping, quiet held keys, release, noise filtering, event coalescing, immediate neutralization, control watchdog, and IPC validation")
