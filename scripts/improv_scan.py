#!/usr/bin/env python3
"""Ask a panel in Bluetooth setup mode which Wi-Fi networks it can see.

Sends Improv RPC 0x04 (Get Wi-Fi Networks) and prints each RPC result the
panel notifies, until the empty result that ends the list. Run it with the
ESPHome venv's python, which has bleak:

    ~/development/tools/esphome-venv/bin/python scripts/improv_scan.py
    ~/development/tools/esphome-venv/bin/python scripts/improv_scan.py --name gameday-2f6a70
    ~/development/tools/esphome-venv/bin/python scripts/improv_scan.py --selftest

The panel only advertises Improv while it has no Wi-Fi (setup mode, or 90s
into a Wi-Fi outage). Close the Game Day app first: a panel takes one
Bluetooth connection at a time.
"""

import argparse
import asyncio
import re
import sys
import time

SERVICE = "00467768-6228-2272-4663-277478268000"
RPC_COMMAND = "00467768-6228-2272-4663-277478268003"
RPC_RESULT = "00467768-6228-2272-4663-277478268004"
ERROR_STATE = "00467768-6228-2272-4663-277478268002"
GET_WIFI_NETWORKS = 0x04


def checksum(data):
    return sum(data) & 0xFF


def build_command(command, strings=()):
    body = bytearray()
    for s in strings:
        b = s.encode()
        body += bytes([len(b)]) + b
    frame = bytes([command, len(body)]) + body
    return frame + bytes([checksum(frame)])


def decode_result(frame):
    """Return (command, [strings]) or raise ValueError."""
    if len(frame) < 3:
        raise ValueError(f"too short: {frame.hex(' ')}")
    length = frame[1]
    if len(frame) != length + 3:
        raise ValueError(f"length byte {length} but {len(frame)} bytes: {frame.hex(' ')}")
    if checksum(frame[:-1]) != frame[-1]:
        raise ValueError(f"bad checksum: {frame.hex(' ')}")
    strings, i, end = [], 2, 2 + length
    while i < end:
        n = frame[i]
        i += 1
        if i + n > end:
            raise ValueError(f"string runs past the end: {frame.hex(' ')}")
        strings.append(frame[i:i + n].decode(errors="replace"))
        i += n
    return frame[0], strings


def verdict(networks):
    """What is wrong with the panel's list of networks: one line per failure, none for a pass.

    Each result should be three strings: the SSID, the RSSI in dBm and YES or
    NO for whether it needs a password. A result of another shape is a
    failure too, not a crash, and the other checks look at the rest.
    """
    fails = []
    shaped = [n for n in networks if len(n) == 3 and re.fullmatch(r"-?\d+", n[1])]
    if len(shaped) != len(networks):
        fails.append("a result is not three strings: SSID, RSSI in dBm, YES or NO")
    ssids = [n[0] for n in shaped]
    rssis = [int(n[1]) for n in shaped]
    if len(set(ssids)) != len(ssids):
        fails.append("duplicate SSIDs")
    if rssis != sorted(rssis, reverse=True):
        fails.append("not sorted strongest first")
    if len(networks) > 20:
        fails.append("more than 20 networks")
    if any(n[2] not in ("YES", "NO") for n in shaped):
        fails.append("third string is not YES or NO")
    return fails


def selftest():
    # The request the app sends, and the two shapes of answer the panel sends.
    assert build_command(GET_WIFI_NETWORKS) == bytes([0x04, 0x00, 0x04])
    net = bytes([0x04, 0x0D, 0x04]) + b"Home" + bytes([0x03]) + b"-52" + bytes([0x03]) + b"YES"
    net += bytes([checksum(net)])
    assert decode_result(net) == (0x04, ["Home", "-52", "YES"])
    assert decode_result(bytes([0x04, 0x00, 0x04])) == (0x04, [])
    try:
        decode_result(net[:-1] + bytes([net[-1] ^ 1]))
        raise AssertionError("bad checksum accepted")
    except ValueError:
        pass

    home, guest = ["Home", "-52", "YES"], ["Guest", "-60", "NO"]
    assert verdict([home, guest]) == []
    assert verdict([]) == []
    assert verdict([home, home]) == ["duplicate SSIDs"]
    assert verdict([guest, home]) == ["not sorted strongest first"]
    assert verdict([home, ["Guest", "-60", "maybe"]]) == ["third string is not YES or NO"]
    assert verdict([[f"n{i}", str(-40 - i), "NO"] for i in range(21)]) == ["more than 20 networks"]
    # A result with two strings, or an RSSI that isn't a number, fails instead of crashing.
    assert verdict([home, ["Guest", "-60"]]) == ["a result is not three strings: SSID, RSSI in dBm, YES or NO"]
    assert verdict([["Home", "strong", "YES"], guest]) == ["a result is not three strings: SSID, RSSI in dBm, YES or NO"]
    print("selftest ok")


async def run(name, timeout):
    from bleak import BleakClient, BleakScanner

    print("Looking for an Improv panel...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE in [u.lower() for u in adv.service_uuids]
        and (name is None or (d.name or "").startswith(name)),
        timeout=20,
    )
    if device is None:
        print("No panel advertising Improv found.")
        return 1
    print(f"Connecting to {device.name} ({device.address})")

    done = asyncio.Event()
    networks = []
    start = time.monotonic()

    def on_result(_, data):
        t = time.monotonic() - start
        try:
            command, strings = decode_result(bytes(data))
        except ValueError as e:
            print(f"{t:6.2f}s  undecodable result: {e}")
            return
        if command != GET_WIFI_NETWORKS:
            print(f"{t:6.2f}s  result for command 0x{command:02x}: {strings}")
            return
        if not strings:
            print(f"{t:6.2f}s  end of list")
            done.set()
            return
        networks.append(strings)
        print(f"{t:6.2f}s  {strings}")

    def on_error(_, data):
        if data and data[0] != 0:
            print(f"Improv error state 0x{data[0]:02x} (0x02 means this firmware does not know 0x04)")
            done.set()

    async with BleakClient(device) as client:
        await client.start_notify(RPC_RESULT, on_result)
        await client.start_notify(ERROR_STATE, on_error)
        start = time.monotonic()
        await client.write_gatt_char(RPC_COMMAND, build_command(GET_WIFI_NETWORKS), response=True)
        try:
            await asyncio.wait_for(done.wait(), timeout)
        except asyncio.TimeoutError:
            print(f"No end-of-list result within {timeout}s")
            return 1

    print(f"{len(networks)} networks")
    fails = verdict(networks)
    for f in fails:
        print(f"FAIL: {f}")
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--name", help="only a panel whose Bluetooth name starts with this")
    ap.add_argument("--timeout", type=float, default=20.0, help="seconds to wait for the list")
    ap.add_argument("--selftest", action="store_true", help="check the frame encoding only, no Bluetooth")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    return asyncio.run(run(args.name, args.timeout))


if __name__ == "__main__":
    sys.exit(main())
