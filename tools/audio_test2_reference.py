#!/usr/bin/env python3
"""Recreate the BRICK6 Audio Test 2 PCM24 reference bit-for-bit."""

from __future__ import annotations

import argparse
import binascii
import csv
import struct
from dataclasses import dataclass
from pathlib import Path

RATE = 48_000
BYTES_PER_FRAME = 6
CRC_INIT = 0xFFFFFFFF
SWEEP_MUL_Q32 = 4_296_450_981
CORDIC = (
    0x20000000, 0x12E4051E, 0x09FB385B, 0x051111D4,
    0x028B0D43, 0x0145D7E1, 0x00A2F61E, 0x00517C55,
    0x0028BE53, 0x00145F2F, 0x000A2F98, 0x000517CC,
    0x00028BE6, 0x000145F3, 0x0000A2FA, 0x0000517D,
    0x000028BE, 0x0000145F, 0x00000A30, 0x00000518,
    0x0000028C, 0x00000146, 0x000000A3, 0x00000051,
)
PEAK = {-1000: 7476354, -3000: 5938679, -6000: 4204263,
        -12000: 2107123, -20000: 838861, -60000: 8389,
        -100000: 84}

SILENCE, SYNC, TONE, SWEEP, MULTITONE, IMPULSES, WHITE, PINK, STEPS, MUTES = range(10)
STEREO, LEFT, RIGHT, IDENTICAL, ANTIPHASE = range(5)


@dataclass(frozen=True)
class Section:
    name: str
    seconds: int
    kind: int
    frequency: int = 0
    level_mdb: int = 0
    channels: int = STEREO


SECTIONS = [
    Section("INITIAL SILENCE", 5, SILENCE),
    Section("SYNC IMPULSES", 5, SYNC, level_mdb=-12000),
    Section("MARKER 20", 1, SILENCE), Section("SINE 20", 4, TONE, 20, -12000),
    Section("MARKER 50", 1, SILENCE), Section("SINE 50", 4, TONE, 50, -12000),
    Section("MARKER 100", 1, SILENCE), Section("SINE 100", 4, TONE, 100, -12000),
    Section("MARKER 500", 1, SILENCE), Section("SINE 500", 4, TONE, 500, -12000),
    Section("MARKER 1K", 1, SILENCE), Section("SINE 1K", 4, TONE, 1000, -12000),
    Section("MARKER 5K", 1, SILENCE), Section("SINE 5K", 4, TONE, 5000, -12000),
    Section("MARKER 10K", 1, SILENCE), Section("SINE 10K", 4, TONE, 10000, -12000),
    Section("MARKER 15K", 1, SILENCE), Section("SINE 15K", 4, TONE, 15000, -12000),
    Section("MARKER 20K", 1, SILENCE), Section("SINE 20K", 4, TONE, 20000, -12000),
    Section("SWEEP MARKER", 2, SILENCE), Section("LOG SWEEP", 20, SWEEP, 20, -12000),
    Section("MULTI MARKER", 2, SILENCE), Section("MULTITONE", 10, MULTITONE, level_mdb=-12000),
    Section("LEVEL MARKER", 2, SILENCE),
    Section("1K -1DBFS", 6, TONE, 1000, -1000),
    Section("1K -6DBFS", 6, TONE, 1000, -6000),
    Section("1K -20DBFS", 6, TONE, 1000, -20000),
    Section("1K -60DBFS", 6, TONE, 1000, -60000),
    Section("1K -100DBFS", 6, TONE, 1000, -100000),
    Section("IMPULSE MARKER", 2, SILENCE),
    Section("SAFE IMPULSES", 5, IMPULSES, level_mdb=-6000),
    Section("NOISE MARKER", 2, SILENCE),
    Section("WHITE NOISE", 10, WHITE, level_mdb=-20000),
    Section("PINK NOISE", 10, PINK, level_mdb=-20000),
    Section("CHANNEL MARKER", 2, SILENCE),
    Section("LEFT ONLY", 8, TONE, 1000, -12000, LEFT),
    Section("RIGHT ONLY", 8, TONE, 1000, -12000, RIGHT),
    Section("IDENTICAL", 8, TONE, 1000, -12000, IDENTICAL),
    Section("OPPOSITE PHASE", 8, TONE, 1000, -12000, ANTIPHASE),
    Section("STEP MARKER", 2, SILENCE),
    Section("LEVEL STEPS", 12, STEPS, 1000, -3000),
    Section("MUTE SEQUENCE", 15, MUTES, 1000, -12000),
    Section("FINAL SYNC", 5, SYNC, level_mdb=-12000),
    Section("FINAL SILENCE", 30, SILENCE),
]
TOTAL_FRAMES = sum(s.seconds * RATE for s in SECTIONS)
assert TOTAL_FRAMES == 11_904_000


def sine_q30(phase: int) -> int:
    quadrant = phase >> 30
    angle = phase & 0x3FFFFFFF
    sign = 1
    if quadrant in (1, 3):
        angle = 0x40000000 - angle
    if quadrant >= 2:
        sign = -1
    x, y, z = 652032874, 0, angle
    for i, atan in enumerate(CORDIC):
        old_x = x
        if z >= 0:
            x -= y >> i
            y += old_x >> i
            z -= atan
        else:
            x += y >> i
            y -= old_x >> i
            z += atan
    return y if sign > 0 else -y


def phase_inc(frequency: int) -> int:
    return (frequency << 32) // RATE


def cdiv(numerator: int, denominator: int) -> int:
    """C99 signed integer division (truncate toward zero)."""
    return abs(numerator) // abs(denominator) * (-1 if numerator * denominator < 0 else 1)


def pack24(value: int) -> bytes:
    value &= 0xFFFFFF
    return bytes((value & 255, (value >> 8) & 255, (value >> 16) & 255))


class Generator:
    def __init__(self) -> None:
        self.frame = self.section = self.section_frame = 0
        self.phase = [0, 0, 0]
        self.noise = 0x6D2B79F5
        self.pink_counter = 0
        self.pink_rows = [0] * 16
        self.sweep_freq_q16 = 20 << 16

    def xorshift(self) -> int:
        x = self.noise
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.noise = x & 0xFFFFFFFF
        return self.noise

    def tone(self, osc: int, frequency: int, peak: int) -> int:
        sample = (sine_q30(self.phase[osc]) * peak) >> 30
        self.phase[osc] = (self.phase[osc] + phase_inc(frequency)) & 0xFFFFFFFF
        return sample

    def fade(self, section: Section) -> int:
        length = section.seconds * RATE
        fade = 65536
        if self.section_frame < 480:
            fade = self.section_frame * 65536 // 480
        remaining = length - self.section_frame
        if remaining < 480:
            fade = min(fade, remaining * 65536 // 480)
        return fade

    def next(self) -> tuple[int, int]:
        if self.section >= len(SECTIONS):
            return 0, 0
        sec = SECTIONS[self.section]
        peak = PEAK.get(sec.level_mdb, 0)
        sample = 0
        if sec.kind == SYNC:
            pos, second = self.section_frame % RATE, self.section_frame // RATE
            if pos < 48 or (second == 0 and 4800 <= pos < 4848):
                sample = peak
        elif sec.kind == TONE:
            sample = self.tone(0, sec.frequency, peak) * self.fade(sec) >> 16
        elif sec.kind == SWEEP:
            if self.section_frame and self.section_frame % 48 == 0:
                self.sweep_freq_q16 = self.sweep_freq_q16 * SWEEP_MUL_Q32 >> 32
            inc = (self.sweep_freq_q16 << 16) // RATE
            sample = sine_q30(self.phase[0]) * peak >> 30
            self.phase[0] = (self.phase[0] + inc) & 0xFFFFFFFF
            sample = sample * self.fade(sec) >> 16
        elif sec.kind == MULTITONE:
            sample = (self.tone(0, 997, peak // 3)
                      + self.tone(1, 2003, peak // 3)
                      + self.tone(2, 5003, peak // 3))
            sample = sample * self.fade(sec) >> 16
        elif sec.kind == IMPULSES:
            if self.section_frame % 12000 < 24:
                sample = -peak if (self.section_frame // 12000) & 1 else peak
        elif sec.kind == WHITE:
            sample = cdiv(((self.xorshift() >> 8) - 8388608) * peak, 8388608)
            sample = sample * self.fade(sec) >> 16
        elif sec.kind == PINK:
            self.pink_counter += 1
            changed, row = self.pink_counter, 0
            while not changed & 1 and row < 15:
                changed >>= 1
                row += 1
            self.pink_rows[row] = (self.xorshift() >> 16) - 32768
            sample = cdiv(sum(self.pink_rows) * peak, 16 * 32768)
            sample = sample * self.fade(sec) >> 16
        elif sec.kind == STEPS:
            step_peak = 838861 if (self.section_frame // 24000) & 1 else peak
            sample = self.tone(0, sec.frequency, step_peak)
        elif sec.kind == MUTES and not ((self.section_frame // RATE) & 1):
            sample = self.tone(0, sec.frequency, peak)

        if sec.channels == LEFT:
            left, right = sample, 0
        elif sec.channels == RIGHT:
            left, right = 0, sample
        elif sec.channels == ANTIPHASE:
            left, right = sample, -sample
        else:
            left = right = sample

        self.frame += 1
        self.section_frame += 1
        if self.section_frame >= sec.seconds * RATE:
            self.section += 1
            self.section_frame = 0
            self.phase = [0, 0, 0]
            self.sweep_freq_q16 = 20 << 16
        return left, right


def wav_header(frames: int) -> bytes:
    data_bytes = frames * BYTES_PER_FRAME
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + data_bytes,
                       b"WAVE", b"fmt ", 16, 1, 2, RATE,
                       RATE * BYTES_PER_FRAME, BYTES_PER_FRAME, 24,
                       b"data", data_bytes)


def write_manifest(path: Path) -> None:
    start = 0
    labels = {STEREO: "STEREO", LEFT: "L", RIGHT: "R",
              IDENTICAL: "L=R", ANTIPHASE: "L=-R"}
    with path.open("w", newline="", encoding="ascii") as stream:
        writer = csv.writer(stream)
        writer.writerow(("name", "start_frame", "start_s", "duration_frames",
                         "duration_s", "frequency_hz", "level_dbfs", "channels"))
        for sec in SECTIONS:
            writer.writerow((sec.name, start, start // RATE, sec.seconds * RATE,
                             sec.seconds, sec.frequency,
                             f"{sec.level_mdb / 1000:.3f}", labels[sec.channels]))
            start += sec.seconds * RATE


def generate(path: Path) -> int:
    gen, crc = Generator(), 0
    with path.open("wb") as stream:
        stream.write(wav_header(TOTAL_FRAMES))
        remaining = TOTAL_FRAMES
        while remaining:
            count = min(4096, remaining)
            block = bytearray()
            for _ in range(count):
                left, right = gen.next()
                block += pack24(left) + pack24(right)
            payload = bytes(block)
            stream.write(payload)
            crc = binascii.crc32(payload, crc)
            remaining -= count
    return crc


def wav_crc(path: Path) -> tuple[int, int]:
    raw = path.read_bytes()
    if len(raw) < 44 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: invalid WAV")
    data_at = raw.find(b"data", 12)
    if data_at < 0:
        raise ValueError(f"{path}: data chunk missing")
    size = struct.unpack_from("<I", raw, data_at + 4)[0]
    data = raw[data_at + 8:data_at + 8 + size]
    return binascii.crc32(data), len(data) // BYTES_PER_FRAME


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("REFERENCE.WAV"))
    parser.add_argument("--manifest", type=Path, default=Path("MANIFEST.CSV"))
    parser.add_argument("--verify", type=Path,
                        help="verify an existing firmware REFERENCE.WAV")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    crc = generate(args.output)
    write_manifest(args.manifest)
    print(f"{args.output}: frames={TOTAL_FRAMES} crc32={crc:08X}")
    if args.verify:
        other_crc, frames = wav_crc(args.verify)
        if frames != TOTAL_FRAMES or other_crc != crc:
            raise SystemExit(
                f"FAIL {args.verify}: frames={frames} crc32={other_crc:08X}")
        print(f"OK {args.verify}: firmware reference is bit-identical")


if __name__ == "__main__":
    main()
