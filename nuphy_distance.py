#!/usr/bin/env python3
"""Read NuPhy Air60 HE key travel on Linux using only Python's standard library.

Protocol checked against https://drive.nuphy.io/ (main.23dc78ef.js):
GetFunc=5, SetFunc=6; modes are 64 bytes apart, debug is bit 3 at offset 7.
A0 reports contain a big-endian key code, sensor value and travel in 0.01 mm.
"""

import argparse
import contextlib
import csv
import fcntl
import math
import os
from pathlib import Path
import select
import signal
import struct
import sys
import time


KEYS = {i + 4: chr(65 + i) for i in range(26)}
KEYS.update({i + 0x1E: name for i, name in enumerate("1234567890")})
KEYS.update({i + 0x28: name for i, name in enumerate([
    "Enter", "Esc", "Backspace", "Tab", "Space", "-", "=", "[", "]",
    "Backslash", "#", ";", "'", "`", ",", ".", "/", "CapsLock",
])})
KEYS.update({i + 0x3A: f"F{i + 1}" for i in range(12)})
KEYS.update({0x4C: "Delete", 0x4F: "Right", 0x50: "Left", 0x51: "Down",
             0x52: "Up", 0x100: "LeftCtrl", 0x200: "LeftShift",
             0x400: "LeftAlt", 0x800: "LeftMeta", 0x1000: "RightCtrl",
             0x2000: "RightShift", 0x4000: "RightAlt", 0x8000: "RightMeta",
             0xFF01: "Fn"})


def devices():
    """Select the vendor interface, never the normal keyboard input interface."""
    found = []
    for entry in sorted(Path("/sys/class/hidraw").glob("hidraw*")):
        try:
            info = (entry / "device/uevent").read_text()
            descriptor = (entry / "device/report_descriptor").read_bytes()
        except OSError:
            continue
        if ("HID_ID=0003:000019F5:0000FEE0" in info.upper()
                and descriptor.startswith(bytes.fromhex("05010900a101"))):
            found.append(Path("/dev") / entry.name)
    return found


def command(op, address, value=None):
    report = bytearray(64)
    report[:2] = bytes((0x55, op))
    report[4] = 0 if op in (1, 2) else 1
    report[5:7] = address.to_bytes(2, "little")
    if value is not None:
        report[8] = value
    report[3] = sum(report[4:]) & 0xFF
    return b"\x00" + report  # hidraw needs the zero report ID for writes.


def receive(fd, timeout):
    if not select.select([fd], [], [], max(0, timeout))[0]:
        return None
    try:
        report = os.read(fd, 64)
    except BlockingIOError:
        return None
    if not report:
        raise OSError("Keyboard disconnected")
    return report


def send(fd, op, address=0, value=None):
    # Discard old replies; cap the drain in case the device is streaming.
    for _ in range(256):
        if receive(fd, 0) is None:
            break
    request = command(op, address, value)
    if os.write(fd, request) != len(request):
        raise OSError("Incomplete HID command write")
    return request


def exchange(fd, op, address=0, value=None):
    request = send(fd, op, address, value)
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        reply = receive(fd, deadline - time.monotonic())
        if reply is None or len(reply) != 64 or reply[:2] != bytes((0xAA, op)):
            continue
        if reply[2] != 0 or reply[4] != request[5] or reply[5:7] != request[6:8]:
            continue
        if (sum(reply[4:]) & 0xFF) != reply[3]:
            raise RuntimeError("Invalid settings reply checksum; close NuPhyIO and retry")
        if value is not None and reply[8] != value:
            raise RuntimeError("Keyboard did not acknowledge the requested debug setting")
        return reply[8]
    raise TimeoutError(f"No reply to command 0x{op:02X} at offset {address}; "
                       "close NuPhyIO and retry")


@contextlib.contextmanager
def quick_commands(fd):
    # Start can be silent. The subsequent settings read verifies the handshake.
    try:
        send(fd, 1)
        # This Air60 HE needs time to enter the session before accepting GetFunc.
        time.sleep(0.1)
        yield
    finally:
        exchange(fd, 2)


@contextlib.contextmanager
def reporting(fd, passive=False):
    changed = []
    try:
        if not passive:
            with quick_commands(fd):
                for mode in range(3):
                    address = mode * 64 + 7
                    original = exchange(fd, 5, address)
                    if not original & 8:
                        # Record before sending: writes can succeed with a lost reply.
                        changed.append(address)
                        exchange(fd, 6, address, original | 8)
        yield
    finally:
        failures = []
        if changed:
            with quick_commands(fd):
                for address in reversed(changed):
                    for attempt in range(3):
                        try:
                            current = exchange(fd, 5, address)
                            if current & 8:
                                exchange(fd, 6, address, current & ~8)
                            break
                        except (OSError, RuntimeError, TimeoutError) as error:
                            if attempt == 2:
                                failures.append(f"mode {address // 64}: {error}")
        if failures:
            raise RuntimeError("Could not restore reporting settings: " + "; ".join(failures))


def decode(report):
    if len(report) != 64 or report[0] != 0xA0:
        return None
    code, sensor, travel = struct.unpack_from(">HHH", report, 2)
    # Byte 1 also identifies special keys (F0); keep their original codes.
    return code, sensor, travel, report[10]


def positive_number(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return number


def run(args):
    candidates = devices()
    if args.list:
        for device in candidates:
            print(device)
        if not candidates:
            raise RuntimeError("No USB NuPhy Air60 HE sensor interface found")
        return
    if args.device:
        device = Path(args.device)
        if device not in candidates:
            raise RuntimeError("Selected device is not an Air60 HE sensor interface")
    elif len(candidates) == 1:
        device = candidates[0]
    else:
        raise RuntimeError("Connect one Air60 HE, or use --list and --device to select it")

    try:
        fd = os.open(device, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    except PermissionError:
        raise PermissionError(
            f"Access denied to {device}. Run with sudo, or grant temporary access:\n"
            f"  sudo setfacl -m u:$(id -un):rw {device}"
        ) from None
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("Another copy of this script is using the keyboard") from None
        print(f"Reading {device}; firmware travel unit = {args.unit_mm:g} mm. "
              "Press keys slowly; Ctrl+C stops.", file=sys.stderr)
        with reporting(fd, args.passive):
            writer = csv.writer(sys.stdout) if args.csv else None
            if writer:
                writer.writerow(["elapsed_s", "key", "code", "distance_mm",
                                 "travel_raw", "sensor_raw", "status"])
                sys.stdout.flush()
            start = time.monotonic()
            seen = 0
            last = {}
            while args.seconds is None or time.monotonic() - start < args.seconds:
                remaining = (args.seconds - (time.monotonic() - start)
                             if args.seconds is not None else 0.5)
                report = receive(fd, min(0.5, remaining))
                data = decode(report) if report is not None else None
                if data is None:
                    continue
                code, sensor, travel, status = data
                seen += 1
                key = KEYS.get(code, f"Key_0x{code:04X}")
                if args.key and key.casefold() != args.key.casefold():
                    continue
                if not args.all and last.get(code) == (sensor, travel, status):
                    continue
                last[code] = sensor, travel, status
                elapsed = time.monotonic() - start
                distance = travel * args.unit_mm
                if writer:
                    writer.writerow([f"{elapsed:.6f}", key, f"0x{code:04X}",
                                     f"{distance:.6f}", travel, sensor, status])
                    sys.stdout.flush()
                else:
                    print(f"{elapsed:9.3f}s  {key:14s} {distance:6.3f} mm  "
                          f"travel={travel:4d}  sensor={sensor:5d}  "
                          f"code=0x{code:04X}  status={status}", flush=True)
            print(f"Received {seen} analog reports.", file=sys.stderr)
            if not seen:
                print("No travel packets received. Press a key during the run; "
                      "if using --passive, enable debug reporting in NuPhyIO.",
                      file=sys.stderr)
    finally:
        os.close(fd)


def stop(signum, frame):
    raise KeyboardInterrupt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="find the sensor interface")
    parser.add_argument("--device", help="select a path printed by --list")
    parser.add_argument("--seconds", type=positive_number, help="stop after this many seconds")
    parser.add_argument("--key", help="show only this key, e.g. W or Space")
    parser.add_argument("--csv", action="store_true", help="write measurements as CSV to stdout")
    parser.add_argument("--all", action="store_true", help="include duplicate measurements")
    parser.add_argument("--passive", action="store_true", help="listen without changing reporting settings")
    parser.add_argument("--unit-mm", type=positive_number, default=0.01,
                        help="millimetres per travel count (Air60 HE default: 0.01)")
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, stop)
    try:
        run(args)
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, TimeoutError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
