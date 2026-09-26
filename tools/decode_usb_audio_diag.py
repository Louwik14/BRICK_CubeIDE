#!/usr/bin/env python3
"""Decode the two halted-target USB Audio diagnostic trace dumps."""

import argparse
import struct

CAPACITY = 1024
ENTRY = struct.Struct("<IHHII")
EVENTS = {
    1: "USB_RX",
    2: "AUDIO_READ",
    3: "AUDIO_BLOCK",
    4: "RING_RESET",
    5: "SET_INTERFACE",
}


def entries(path, write_index, domain):
    with open(path, "rb") as source:
        data = source.read()
    if len(data) != CAPACITY * ENTRY.size:
        raise ValueError(f"{path}: expected {CAPACITY * ENTRY.size} bytes, got {len(data)}")
    count = min(write_index, CAPACITY)
    first = write_index - count
    result = []
    for sequence in range(first, write_index):
        offset = (sequence % CAPACITY) * ENTRY.size
        tick, event, arg0, arg1, arg2 = ENTRY.unpack_from(data, offset)
        result.append((tick, sequence, domain, event, arg0, arg1, arg2))
    return result


def detail(event, arg0, arg1, arg2):
    if event == 1:
        return (f"bytes={arg0} frames={arg1 >> 16} written={arg1 & 0xffff} "
                f"fill={arg2}")
    if event == 2:
        return (f"flags=0x{arg0:x} requested={arg1 >> 16} read={arg1 & 0xffff} "
                f"fill_before={arg2 >> 16} fill_after={arg2 & 0xffff}")
    if event == 3:
        return f"half={arg0 & 1} recovering={(arg0 >> 1) & 1}"
    if event == 5:
        return f"interface={arg0 >> 8} alt={arg0 & 0xff}"
    return f"arg0={arg0} arg1={arg1} arg2={arg2}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--usb", default="usb_audio_diag_usb.bin")
    parser.add_argument("--audio", default="usb_audio_diag_audio.bin")
    parser.add_argument("--usb-write", required=True, type=int)
    parser.add_argument("--audio-write", required=True, type=int)
    args = parser.parse_args()
    rows = entries(args.usb, args.usb_write, "USB")
    rows += entries(args.audio, args.audio_write, "AUDIO")
    # Runs used for diagnosis are far shorter than the 32-bit media-tick wrap.
    rows.sort(key=lambda row: row[0])
    print("tick       domain sequence event          detail")
    for tick, sequence, domain, event, arg0, arg1, arg2 in rows:
        print(f"{tick:10d} {domain:6s} {sequence:8d} "
              f"{EVENTS.get(event, str(event)):14s} {detail(event, arg0, arg1, arg2)}")


if __name__ == "__main__":
    main()
