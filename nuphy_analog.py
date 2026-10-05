#!/usr/bin/env python3
"""NuPhy Air60 HE travel -> virtual Xbox left stick, controlled by the client mod."""
import argparse
import contextlib
import fcntl
import math
import os
from pathlib import Path
import select
import signal
import socket
import struct
import sys
import time

import nuphy_distance as sensor

KEY_CODES = (0x1A, 0x04, 0x16, 0x07)  # W, A, S, D (USB HID usage IDs)
CONTROL = struct.Struct("<4sBBBB")  # magic, enabled, gameplay/focus, auto-sprint, reserved
STATUS = struct.Struct("<4sB3x")
EVENT = struct.Struct("@llHHi")
TIMEOUT = 0.5
OUTPUT_INTERVAL = 1 / 60


def socket_address(uid=None):
    return "\0nuphy-analog-" + str(os.getuid() if uid is None else uid)


def axes(travel, full=330, deadzone=10):
    """Linear key depth, opposite-key cancellation, circular diagonal clamp."""
    def depth(value):
        return min(1.0, max(0.0, (value - deadzone) / (full - deadzone)))
    w, a, s, d = map(depth, travel)
    x, y = d - a, s - w
    length = math.hypot(x, y)
    if length > 1:
        x, y = x / length, y / length
    return round(x * 32767), round(y * 32767)


def output(travel, control, control_age, sensor_ready, full=330, deadzone=10):
    # Travel reports describe changes, not a heartbeat. A quiet held key is valid.
    if not control[0] or not control[1] or control_age > TIMEOUT or not sensor_ready:
        return 0, 0, False
    x, y = axes(travel, full, deadzone)
    # Preserve slow walking: automatic sprint starts only near full forward travel.
    sprint = bool(control[2] and travel[0] >= full * 0.9 and y < 0)
    return x, y, sprint


def update_travel(travel, code, distance, full=330, deadzone=10):
    if code not in KEY_CODES:
        return False
    index = KEY_CODES.index(code)
    # Ignore 0.01–0.02 mm wobble, but always accept release and full travel.
    if distance <= deadzone:
        travel[index] = 0
    elif distance >= full or abs(distance - travel[index]) >= 3:
        travel[index] = distance
    return True


def parse_control(packet):
    if len(packet) != CONTROL.size:
        raise ValueError("Invalid control packet")
    magic, enabled, active, sprint, reserved = CONTROL.unpack(packet)
    if magic != b"NAC1" or max(enabled, active, sprint) > 1 or reserved:
        raise ValueError("Invalid control flags")
    return bool(enabled), bool(active), bool(sprint)


def ioctl_write(number, size=4):
    return (1 << 30) | (size << 16) | (ord("U") << 8) | number


class VirtualPad:
    def __init__(self):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK | os.O_CLOEXEC)
        self.last = None
        self.last_emit = 0.0
        try:
            for kind in (1, 3):  # EV_KEY, EV_ABS
                fcntl.ioctl(self.fd, ioctl_write(100), kind)
            # Match the launcher's Linux Xbox 360 mapping (including left-stick click).
            for button in (304, 305, 307, 308, 310, 311, 314, 315, 316, 317, 318):
                fcntl.ioctl(self.fd, ioctl_write(101), button)
            for code in (0, 1, 2, 3, 4, 5, 16, 17):
                minimum, maximum = ((0, 255) if code in (2, 5) else
                                    (-1, 1) if code in (16, 17) else (-32767, 32767))
                fcntl.ioctl(self.fd, ioctl_write(4, 28),
                            struct.pack("@H2x6i", code, 0, minimum, maximum, 0, 0, 0))
            setup = struct.pack("@HHHH80sI", 3, 0x045E, 0x028E, 0x0114,
                                b"NuPhy Analog WASD (Xbox 360)", 0)
            fcntl.ioctl(self.fd, ioctl_write(3, len(setup)), setup)
            fcntl.ioctl(self.fd, (ord("U") << 8) | 1)  # UI_DEV_CREATE
            name = bytearray(128)
            fcntl.ioctl(self.fd, (2 << 30) | (128 << 16) | (ord("U") << 8) | 44, name)
            sysname = bytes(name).split(b"\0", 1)[0].decode("ascii")
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                nodes = list((Path("/sys/devices/virtual/input") / sysname).glob("event*"))
                if nodes:
                    self.event_path = Path("/dev/input") / nodes[0].name
                    try:
                        probe = os.open(self.event_path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                        os.close(probe)
                        break
                    except (FileNotFoundError, PermissionError):
                        pass
                time.sleep(0.05)
            else:
                raise PermissionError("The virtual controller's /dev/input/event node is not readable; "
                                      "grant your user access before starting the helper")
            self.emit(0, 0, False)
        except BaseException:
            os.close(self.fd)  # Closing also destroys a created uinput device.
            raise

    def emit(self, x, y, sprint):
        state = x, y, bool(sprint)
        if state == self.last:
            return
        now = time.monotonic()
        # Coalesce rapid depth changes; menu/focus/disconnect neutralization is immediate.
        if state != (0, 0, False) and now - self.last_emit < OUTPUT_INTERVAL:
            return
        changes = [(kind, code, int(value)) for index, (kind, code, value) in enumerate(
                   ((3, 0, x), (3, 1, y), (1, 317, sprint)))
                   if self.last is None or value != self.last[index]]
        packet = b"".join(EVENT.pack(0, 0, kind, code, value)
                          for kind, code, value in changes + [(0, 0, 0)])
        # A partial write must not silently lose the neutral release.
        if os.write(self.fd, packet) != len(packet):
            raise OSError("Incomplete virtual-controller event write")
        self.last = state
        self.last_emit = now

    def close(self):
        try:
            self.emit(0, 0, False)
        finally:
            os.close(self.fd)


def choose_device(device=None):
    candidates = sensor.devices()
    if device is not None:
        selected = Path(device)
        if selected not in candidates:
            raise RuntimeError("Selected device is not an Air60 HE vendor interface")
        return selected
    if len(candidates) != 1:
        raise RuntimeError("Connect one Air60 HE, or pass --device with its vendor hidraw path")
    return candidates[0]


def check_access(device):
    print(f"NuPhy vendor interface: {device}")
    problems = []
    for path in (device, Path("/dev/uinput")):
        mode = os.O_RDWR if path == device else os.O_WRONLY
        try:
            fd = os.open(path, mode | os.O_NONBLOCK | os.O_CLOEXEC)
        except FileNotFoundError:
            problems.append(f"{path} is missing" + ("; load it with sudo modprobe uinput" if path.name == "uinput" else ""))
        except PermissionError:
            problems.append(f"Access denied to {path}; grant your user temporary read/write access")
        else:
            os.close(fd)
            print(f"Access OK: {path}")
    if problems:
        raise RuntimeError("; ".join(problems))


def run(args):
    device = choose_device(args.device)
    if args.check:
        check_access(device)
        return
    fd = os.open(device, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with contextlib.ExitStack() as stack:
            pad = VirtualPad()
            stack.callback(pad.close)
            server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            stack.callback(server.close)
            server.bind(socket_address())
            server.listen(1)
            server.setblocking(False)
            stack.enter_context(sensor.reporting(fd, args.passive))
            print(f"Ready: {device} -> {pad.event_path}; enable Analog WASD in the L or K menu.", flush=True)
            print(f"Deadzone {args.deadzone_mm:g} mm; full speed {args.full_mm:g} mm; Ctrl+C restores reporting.", flush=True)
            client = None
            travel = [0] * 4
            control = (False, False, False)
            control_time = status_time = 0.0
            sensor_ready = False
            start = time.monotonic()
            try:
                while args.seconds is None or time.monotonic() - start < args.seconds:
                    readers = [fd, server] + ([client] if client else [])
                    readable, _, _ = select.select(readers, [], [], 1 / 120)
                    now = time.monotonic()
                    if server in readable:
                        incoming, _ = server.accept()
                        _, uid, _ = struct.unpack("3i", incoming.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
                        if client or uid != os.getuid():
                            incoming.close()
                        else:
                            client = incoming
                            client.setblocking(False)
                            control = (False, False, False)
                            control_time = 0
                            print("Minecraft connected.", flush=True)
                    if fd in readable:
                        # Drain bounded batches so input IPC and watchdogs cannot starve.
                        for _ in range(256):
                            report = sensor.receive(fd, 0)
                            if report is None:
                                break
                            data = sensor.decode(report)
                            if data:
                                code, _, distance, _ = data
                                if update_travel(travel, code, distance,
                                                 args.full_mm / 0.01, args.deadzone_mm / 0.01):
                                    sensor_ready = True
                    if client in readable:
                        try:
                            packet = client.recv(64)
                            if not packet:
                                raise ConnectionError("Minecraft disconnected")
                            control = parse_control(packet)
                            control_time = now
                        except (OSError, ValueError) as error:
                            print(str(error), file=sys.stderr)
                            client.close()
                            client = None
                            control = (False, False, False)
                    pad.emit(*output(travel, control, now - control_time, sensor_ready,
                                     args.full_mm / 0.01, args.deadzone_mm / 0.01))
                    if client and now - status_time >= 0.05:
                        status_time = now
                        try:
                            client.send(STATUS.pack(b"NAS1", int(sensor_ready)))
                        except BlockingIOError:
                            pass
                        except OSError:
                            client.close()
                            client = None
                            control = (False, False, False)
            finally:
                pad.emit(0, 0, False)  # Neutralize before USB reporting restoration can block.
                if client:
                    client.close()
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device")
    parser.add_argument("--check", action="store_true", help="check access without changing settings or creating a controller")
    parser.add_argument("--passive", action="store_true", help="use reporting already enabled in NuPhyIO")
    parser.add_argument("--full-mm", type=sensor.positive_number, default=3.3)
    parser.add_argument("--deadzone-mm", type=float, default=0.1)
    parser.add_argument("--seconds", type=sensor.positive_number)
    args = parser.parse_args()
    if not math.isfinite(args.deadzone_mm) or not 0 <= args.deadzone_mm < args.full_mm:
        parser.error("deadzone must be finite and between zero and full travel")
    signal.signal(signal.SIGTERM, sensor.stop)
    try:
        run(args)
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, TimeoutError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
