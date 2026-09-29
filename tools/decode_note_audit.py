"""Decode the temporary note cut RAM rings dumped by docs/note_cut_audit.md."""

import argparse
import csv
import struct
import sys
from pathlib import Path


EVENTS = (
    "", "HALL_EDGE", "HALL_DROP", "HALL_POP", "KEY", "KEY_ON",
    "KEY_OFF", "OCCURRENCE", "INGRESS_FAIL", "OUTPUT", "VICTIM",
    "PUBLISH_FAIL", "PANIC", "AUDIO_COMMAND", "AUDIO_VOICE",
    "AUDIO_PANIC", "INGRESS", "WINDOW", "HALL_QUEUED", "OWNER",
    "AUDIO_TRANSPORT", "OUTPUT_DEATH",
)
RECORD = struct.Struct("<IIIIHBBBBH")


def rows(path: Path):
    data = path.read_bytes()
    if len(data) % RECORD.size:
        raise ValueError(f"{path}: size is not a multiple of {RECORD.size}")
    records = [RECORD.unpack_from(data, offset)
               for offset in range(0, len(data), RECORD.size)]
    records = [record for record in records if record[0]]
    records.sort(key=lambda record: record[0])
    for sequence, tick, identity, aux, event, track, note, detail, held_count, held_mask in records:
        yield (path.stem, sequence, tick,
               EVENTS[event] if event < len(EVENTS) else event,
               track, note, detail, held_count, f"0x{held_mask:04x}",
               f"0x{identity:08x}", f"0x{aux:08x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dumps", nargs="+", type=Path)
    args = parser.parse_args()
    writer = csv.writer(sys.stdout)
    writer.writerow(("ring", "sequence", "tick", "event", "track", "note",
                     "detail", "held_count", "held_mask", "id", "aux"))
    for path in args.dumps:
        writer.writerows(rows(path))


if __name__ == "__main__":
    main()
