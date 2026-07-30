#!/usr/bin/env python3
"""Align and compare BRICK6 Audio Test 2 digital and analog captures."""

from __future__ import annotations

import argparse
import csv
import math
import struct
from pathlib import Path

try:
    import numpy as np
except ImportError as exc:
    raise SystemExit("audio_test2_analyze.py requires numpy") from exc

RATE = 48_000


def read_wav(path: Path) -> tuple[int, np.ndarray]:
    raw = path.read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos, channels = 12, 0
    bits = rate = audio_format = 0
    data = b""
    while pos + 8 <= len(raw):
        tag, size = raw[pos:pos + 4], struct.unpack_from("<I", raw, pos + 4)[0]
        body = raw[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            audio_format, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", body)
        elif tag == b"data":
            data = body
            break
        pos += 8 + size + (size & 1)
    if channels != 2 or rate != RATE or audio_format not in (1, 3):
        raise ValueError(f"{path}: expected stereo 48 kHz PCM/float")
    if audio_format == 3 and bits == 32:
        samples = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif audio_format == 1 and bits == 24:
        u = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3)
        samples = (u[:, 0].astype(np.int32)
                   | (u[:, 1].astype(np.int32) << 8)
                   | (u[:, 2].astype(np.int32) << 16))
        samples = ((samples ^ 0x800000) - 0x800000).astype(np.float64) / 8388608.0
    elif audio_format == 1 and bits == 16:
        samples = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    elif audio_format == 1 and bits == 32:
        samples = np.frombuffer(data, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: unsupported format {audio_format}/{bits}")
    return rate, samples.reshape(-1, 2)


def load_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="ascii") as stream:
        return list(csv.DictReader(stream))


def mono(x: np.ndarray) -> np.ndarray:
    return np.mean(x, axis=1)


def valid_correlation(search: np.ndarray, template: np.ndarray) -> np.ndarray:
    n = 1 << (len(search) + len(template) - 1).bit_length()
    convolution = np.fft.irfft(np.fft.rfft(search, n)
                               * np.fft.rfft(template[::-1], n), n)
    count = max(1, len(search) - len(template) + 1)
    return convolution[len(template) - 1:len(template) - 1 + count]


def marker_offset(reference: np.ndarray, capture: np.ndarray,
                  expected: int = 5 * RATE, window: int = 3 * RATE) -> int:
    """Locate the first sync impulse using a bounded normalized correlation."""
    template = mono(reference[expected:expected + RATE])
    template -= np.mean(template)
    start = max(0, expected - window)
    stop = min(len(capture), expected + window + RATE)
    search = mono(capture[start:stop])
    # FFT correlation keeps multi-minute captures inexpensive.
    valid = valid_correlation(search, template)
    return start + int(np.argmax(np.abs(valid))) - expected


def estimate_drift(reference: np.ndarray, capture: np.ndarray,
                   offset: int) -> float:
    """Estimate only linear clock drift from the dedicated final sync marker."""
    ref_pos = 217 * RATE
    radius = 3 * RATE // 4
    template = mono(reference[ref_pos:ref_pos + RATE // 5])
    predicted = ref_pos + offset
    lo, hi = max(0, predicted - radius), min(len(capture), predicted + RATE + radius)
    search = mono(capture[lo:hi])
    valid = valid_correlation(search, template)
    observed = lo + int(np.argmax(np.abs(valid)))
    return (observed - offset) / ref_pos


def time_correct(capture: np.ndarray, frames: int, offset: int,
                 scale: float) -> np.ndarray:
    """Correct delay and linear sample-clock drift; apply no gain or EQ."""
    source_positions = offset + np.arange(frames, dtype=np.float64) * scale
    base = np.arange(len(capture), dtype=np.float64)
    out = np.empty((frames, 2), dtype=np.float64)
    for channel in range(2):
        out[:, channel] = np.interp(source_positions, base, capture[:, channel],
                                    left=0.0, right=0.0)
    return out


def db(value: float, floor: float = -300.0) -> float:
    return 20.0 * math.log10(value) if value > 0.0 else floor


def tone_metrics(x: np.ndarray, frequency: float) -> tuple[float, float, float]:
    trim = min(RATE // 2, len(x) // 8)
    y = mono(x[trim:len(x) - trim])
    if len(y) < 1024:
        return math.nan, math.nan, math.nan
    window = np.hanning(len(y))
    spectrum = np.fft.rfft(y * window)
    freqs = np.fft.rfftfreq(len(y), 1.0 / RATE)
    fundamental = int(np.argmin(np.abs(freqs - frequency)))
    amplitude = 2.0 * abs(spectrum[fundamental]) / np.sum(window)
    harmonic_power = 0.0
    for harmonic in range(2, 11):
        target = frequency * harmonic
        if target >= RATE / 2:
            break
        index = int(np.argmin(np.abs(freqs - target)))
        harmonic_power += abs(spectrum[index]) ** 2
    thd = math.sqrt(harmonic_power) / max(abs(spectrum[fundamental]), 1e-30)
    phase = math.degrees(np.angle(spectrum[fundamental]))
    return db(amplitude), db(thd), phase


def interchannel_phase(capture: np.ndarray, frequency: float) -> float:
    trim = min(RATE // 2, len(capture) // 8)
    y = capture[trim:len(capture) - trim]
    if len(y) < 1024:
        return math.nan
    window = np.hanning(len(y))
    index = int(round(frequency * len(y) / RATE))
    left = np.fft.rfft(y[:, 0] * window)[index]
    right = np.fft.rfft(y[:, 1] * window)[index]
    return math.degrees(np.angle(right) - np.angle(left))


def section_metrics(name: str, reference: np.ndarray,
                    capture: np.ndarray, frequency: float,
                    channels: str) -> dict[str, object]:
    eps = 1e-30
    peak = np.max(np.abs(capture), axis=0)
    rms = np.sqrt(np.mean(capture * capture, axis=0) + eps)
    dc = np.mean(capture, axis=0)
    ref_rms = np.sqrt(np.mean(reference * reference, axis=0) + eps)
    ref_mono, capture_mono = mono(reference), mono(capture)
    corr = float(np.corrcoef(ref_mono, capture_mono)[0, 1]) \
        if np.std(ref_mono) > 0 and np.std(capture_mono) > 0 else math.nan
    level_l = db(float(rms[0] / max(ref_rms[0], eps)))
    level_r = db(float(rms[1] / max(ref_rms[1], eps)))
    if channels == "L":
        crosstalk = db(float(rms[1] / max(rms[0], eps)))
    elif channels == "R":
        crosstalk = db(float(rms[0] / max(rms[1], eps)))
    else:
        crosstalk = math.nan
    result: dict[str, object] = {
        "name": name,
        "peak_l_dbfs": db(float(peak[0])),
        "peak_r_dbfs": db(float(peak[1])),
        "rms_l_dbfs": db(float(rms[0])),
        "rms_r_dbfs": db(float(rms[1])),
        "level_delta_l_db": level_l,
        "level_delta_r_db": level_r,
        "frequency_response_db": (level_l + level_r) * 0.5,
        "crosstalk_db": crosstalk,
        "dc_l": float(dc[0]), "dc_r": float(dc[1]),
        "balance_db": db(float(rms[0] / max(rms[1], eps))),
        "correlation": corr,
        "click_count": int(np.count_nonzero(
            np.abs(np.diff(capture, axis=0)) > 0.25)),
        "clip_samples": int(np.count_nonzero(np.abs(capture) >= 0.999)),
    }
    if frequency > 0:
        level, thd, phase = tone_metrics(capture, frequency)
        result.update(tone_level_dbfs=level, thd_db=thd, phase_deg=phase,
                      interchannel_phase_deg=interchannel_phase(capture, frequency))
    else:
        result.update(tone_level_dbfs=math.nan, thd_db=math.nan,
                      phase_deg=math.nan, interchannel_phase_deg=math.nan)
    return result


def warning_for(row: dict[str, object]) -> str:
    warnings = []
    if max(float(row["peak_l_dbfs"]), float(row["peak_r_dbfs"])) > -0.5:
        warnings.append("near ADC/full-scale limit")
    if int(row["clip_samples"]):
        warnings.append("digital clipping")
    if abs(float(row["dc_l"])) > 0.005 or abs(float(row["dc_r"])) > 0.005:
        warnings.append("high DC")
    rms_min = min(float(row["rms_l_dbfs"]), float(row["rms_r_dbfs"]))
    if rms_min < -115:
        warnings.append("near typical interface noise-floor limit")
    return "; ".join(warnings)


def analyze_one(label: str, reference: np.ndarray, capture: np.ndarray,
                manifest: list[dict[str, str]]) -> tuple[list[dict[str, object]], dict[str, float]]:
    offset = marker_offset(reference, capture)
    scale = estimate_drift(reference, capture, offset)
    aligned = time_correct(capture, len(reference), offset, scale)
    rows = []
    for entry in manifest:
        start = int(entry["start_frame"])
        stop = start + int(entry["duration_frames"])
        row = section_metrics(entry["name"], reference[start:stop],
                              aligned[start:stop], float(entry["frequency_hz"]),
                              entry["channels"])
        row["capture"] = label
        row["warning"] = warning_for(row)
        rows.append(row)
    return rows, {
        "latency_samples": float(offset),
        "latency_ms": 1000.0 * offset / RATE,
        "clock_scale": scale,
        "clock_drift_ppm": (scale - 1.0) * 1_000_000.0,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--internal", type=Path, required=True)
    parser.add_argument("--line", type=Path, required=True)
    parser.add_argument("--headphone", type=Path, required=True)
    parser.add_argument("--loopback", type=Path)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("AUDIO_TEST2_RESULTS.csv"))
    parser.add_argument("--report", type=Path, default=Path("AUDIO_TEST2_REPORT.txt"))
    args = parser.parse_args()

    _, reference = read_wav(args.reference)
    manifest = load_manifest(args.manifest)
    captures = [("INTERNAL", args.internal), ("LINE", args.line),
                ("HEADPHONE", args.headphone)]
    if args.loopback:
        captures.append(("SCARLETT_LOOPBACK", args.loopback))

    rows, summaries = [], {}
    for label, path in captures:
        _, capture = read_wav(path)
        measured, summary = analyze_one(label, reference, capture, manifest)
        rows.extend(measured)
        summaries[label] = summary

    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    with args.report.open("w", encoding="utf-8") as report:
        report.write("BRICK6 AUDIO TEST 2\n\n")
        report.write("Only time offset and linear clock drift were corrected; "
                     "no gain normalization or equalization was applied.\n\n")
        for label, summary in summaries.items():
            report.write(
                f"{label}: latency={summary['latency_samples']:.1f} samples "
                f"({summary['latency_ms']:.3f} ms), "
                f"clock drift={summary['clock_drift_ppm']:.2f} ppm\n")
        report.write("\nWarnings:\n")
        for row in rows:
            if row["warning"]:
                report.write(f"- {row['capture']} / {row['name']}: "
                             f"{row['warning']}\n")
        report.write("\nThe interface-limit warnings are conservative generic "
                     "Scarlett-class thresholds, not a calibration certificate "
                     "for a specific model or gain setting.\n")
    print(f"wrote {args.output} and {args.report}")


if __name__ == "__main__":
    main()
