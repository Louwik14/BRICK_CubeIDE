#!/usr/bin/env python3
"""Decode `x/Nwx &g_stream_rec_perf` output from GDB (ABI v4)."""
import argparse
import re
import struct
import sys
from pathlib import Path

CPU = (
    "audio_total", "audio_float_to_pcm24", "audio_peak_meter",
    "audio_ring_copy", "reader_need", "reader_lease",
    "reader_resolve", "protection_update_audio",
    "manager_pick", "manager_finish", "cache_reserve",
    "cache_recycle", "stream_command", "stream_submit", "stream_io_begin",
    "stream_io_finalize", "stream_read_start", "stream_read_complete",
    "stream_dma_launch", "rec_dma_launch",
    "rec_source_scratch_copy", "rec_source_pcm24_to_float",
    "rec_pack", "rec_prepare", "rec_service",
    "rec_write_start", "rec_write_complete",
)
WALL = ("stream_request_to_ready", "stream_submit_to_dma", "stream_dma",
        "stream_dma_to_io_finalize", "rec_write_dma", "rec_submit_to_dma")
COUNT = (
    "pages_requested", "pages_ready", "classic_ready", "multi_ready",
    "rec_ready", "cache_ready_observations", "cache_hit",
    "cache_loading_observations", "cache_miss", "cache_alloc", "cache_recycle",
    "cache_protected", "cache_alloc_fail", "page_failed", "lease_publish",
    "lease_check", "audio_page_missing", "reads", "read_bytes",
    "read_min_bytes", "read_max_bytes", "rec_frames",
    "audio_converted_frames", "rec_pcm_bytes",
    "rec_write_bytes", "rec_writes", "rec_write_min_bytes",
    "rec_write_max_bytes", "rec_ring_fill", "rec_ring_max",
    "rec_ring_min_free", "rec_ring_near_full", "rec_overflow",
    "rec_source_pages", "rec_source_frames",
    "protection_update_storage", "recyclable_candidates",
    "reserve_searches", "reserve_candidates_tested",
    "reserve_candidates_tested_max", "reserve_revalidation_fail",
    "reserve_no_candidate", "test_elapsed_ms",
)
MAGIC = 0x46505242
VERSION = 4
SIZE = 16 + 16 * (len(CPU) + len(WALL)) + 8 * len(COUNT)


def decode(source):
    words = []
    for line in source.splitlines():
        if ":" not in line:
            continue
        address, rest = line.split(":", 1)
        if not re.fullmatch(r"\s*0x[0-9a-fA-F]+\s*", address):
            continue
        words.extend(int(x, 16) for x in re.findall(r"(?<!\w)0x[0-9a-fA-F]{1,8}(?!\w)", rest))
    if len(words) < 4:
        raise ValueError("dump absent ou tronque: en-tete de 4 words requis")
    magic, version, size, hz = words[:4]
    if magic != MAGIC:
        raise ValueError(f"MAGIC incorrect: 0x{magic:08x} (attendu 0x{MAGIC:08x})")
    if version != VERSION:
        raise ValueError(f"VERSION incorrecte: {version} (attendue {VERSION})")
    if size != SIZE:
        raise ValueError(f"SIZE incorrecte: {size} (attendue {SIZE})")
    if len(words) < size // 4:
        raise ValueError(f"dump tronque: {len(words)} words, {size // 4} requis")
    if hz == 0:
        raise ValueError("cpu_hz vaut 0: appeler brick_perf_diag_reset avant le test")
    data = struct.pack("<" + "I" * (size // 4), *words[:size // 4])
    offset = 16
    cpu, wall = {}, {}
    for names, dest in ((CPU, cpu), (WALL, wall)):
        for name in names:
            calls, maximum, total = struct.unpack_from("<IIQ", data, offset)
            dest[name] = (calls, total, maximum)
            offset += 16
    counts = dict(zip(COUNT, struct.unpack_from("<" + "Q" * len(COUNT), data, offset)))
    return hz, cpu, wall, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    args = parser.parse_args()
    try:
        hz, cpu, wall, counts = decode(args.dump.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    def cycles(name):
        calls, total, maximum = cpu[name]
        return f"calls={calls} cycles={total} avg={total / calls if calls else 0:.1f} max={maximum}"

    def latency(name):
        calls, total, maximum = wall[name]
        return (f"count={calls} avg_us={total * 1e6 / hz / calls if calls else 0:.2f} "
                f"max_us={maximum * 1e6 / hz:.2f}")

    stream_cpu = ["reader_need", "reader_lease", "reader_resolve",
                  "protection_update_audio", "manager_pick",
                  "manager_finish", "cache_reserve", "cache_recycle", "stream_command",
                  "stream_submit", "stream_io_begin", "stream_io_finalize",
                  "stream_read_start", "stream_read_complete", "stream_dma_launch"]
    rec_cpu = ["audio_total", "audio_float_to_pcm24", "audio_peak_meter",
               "audio_ring_copy", "rec_pack", "rec_prepare",
               "rec_service", "rec_write_start", "rec_write_complete", "rec_dma_launch"]
    print(f"BRICK stream/rec perf v{VERSION}; CPU {hz} Hz; {SIZE} bytes")
    print("\nSTREAMER\n--------")
    for name in COUNT[:21]:
        print(f"{name}: {counts[name]}")
    total = sum(cpu[n][1] for n in stream_cpu
                if n not in ("stream_dma_launch", "protection_update_audio"))
    ready = counts["pages_ready"]
    print(f"sampled_cpu_cycles_excl_nested_dma_launch: {total}; cycles/ready_page: {total / ready if ready else 0:.1f}")
    print(f"physical_bytes/read: {counts['read_bytes'] / counts['reads'] if counts['reads'] else 0:.1f}")
    seconds = counts["test_elapsed_ms"] / 1000
    if seconds:
        print(f"pages/s: {ready / seconds:.2f}; physical_MB/s: {counts['read_bytes'] / 1e6 / seconds:.3f}")
    for name in stream_cpu:
        if cpu[name][0]: print(f"CPU {name}: {cycles(name)}")
    searches = counts["reserve_searches"]
    tested = counts["reserve_candidates_tested"]
    print(f"reserve_candidates/search: {tested / searches if searches else 0:.2f}; "
          f"max={counts['reserve_candidates_tested_max']}; "
          f"revalidation_fail={counts['reserve_revalidation_fail']}; "
          f"no_candidate={counts['reserve_no_candidate']}")
    for name in WALL[:4]: print(f"wall {name}: {latency(name)}")
    print("\nRECORDER AUDIO\n--------------")
    for name in ("rec_frames", "audio_converted_frames", "rec_pcm_bytes"):
        print(f"{name}: {counts[name]}")
    frames = counts["audio_converted_frames"]
    for name in ("audio_total", "audio_float_to_pcm24", "audio_peak_meter", "audio_ring_copy"):
        print(f"CPU {name}: {cycles(name)}")
    print(f"FLOAT_to_PCM24_cycles/frame: {cpu['audio_float_to_pcm24'][1] / frames if frames else 0:.2f}")
    print("\nRECORDER STORAGE\n----------------")
    for name in COUNT[24:33]: print(f"{name}: {counts[name]}")
    for name in rec_cpu[4:]:
        if cpu[name][0]: print(f"CPU {name}: {cycles(name)}")
    print(f"wall rec_write_dma: {latency('rec_write_dma')}")
    print(f"wall rec_submit_to_dma: {latency('rec_submit_to_dma')}")
    print("\nREC_SOURCE\n----------")
    for name in COUNT[33:35]: print(f"{name}: {counts[name]}")
    print(f"CPU rec_source_scratch_copy: {cycles('rec_source_scratch_copy')}")
    pages = counts["rec_source_pages"]
    print(f"scratch_copy_cycles/page: {cpu['rec_source_scratch_copy'][1] / pages if pages else 0:.1f}")
    total = cpu["rec_source_pcm24_to_float"][1]
    print(f"PCM24_to_FLOAT_cycles/page: {total / pages if pages else 0:.1f}")
    print(f"PCM24_to_FLOAT_cycles/frame: {total / counts['rec_source_frames'] if counts['rec_source_frames'] else 0:.2f}")
    print(f"CPU rec_source_pcm24_to_float: {cycles('rec_source_pcm24_to_float')}")


if __name__ == "__main__":
    main()
