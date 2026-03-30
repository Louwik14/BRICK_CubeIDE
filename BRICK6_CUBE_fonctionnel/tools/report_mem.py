#!/usr/bin/env python3
"""STM32CubeIDE post-build memory report from GNU ld .map file.

Usage:
  python tools/report_mem.py --map <path/to/project.map>
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


@dataclass
class Region:
    name: str
    origin: int
    length: int
    used: int = 0


DEFAULT_DISPLAY_ORDER = ["FLASH", "DTCM", "RAM_D1", "RAM_D2", "RAM_D3", "SDRAM"]
REGION_ALIASES = {
    "DTCM": ["DTCM", "DTCMRAM"],
}


def parse_int(token: str) -> int:
    token = token.strip()
    if token.lower().startswith("0x"):
        return int(token, 16)
    return int(token, 10)


def parse_regions(map_text: str) -> dict[str, Region]:
    lines = map_text.splitlines()
    regions: dict[str, Region] = {}

    mem_header = None
    for i, line in enumerate(lines):
        if line.strip() == "Memory Configuration":
            mem_header = i
            break
    if mem_header is None:
        raise RuntimeError("Section 'Memory Configuration' introuvable dans le .map")

    region_re = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)")
    for line in lines[mem_header + 1 :]:
        s = line.strip()
        if not s:
            if regions:
                break
            continue
        if s.startswith("Name") and "Origin" in s:
            continue
        m = region_re.match(line)
        if not m:
            if regions:
                break
            continue
        name, origin_hex, length_hex = m.groups()
        regions[name] = Region(name=name, origin=int(origin_hex, 16), length=int(length_hex, 16))

    if not regions:
        raise RuntimeError("Aucune région mémoire trouvée dans 'Memory Configuration'")
    return regions


def find_region_for_address(regions: dict[str, Region], addr: int) -> Region | None:
    for region in regions.values():
        if region.origin <= addr < (region.origin + region.length):
            return region
    return None


def accumulate_usage(map_text: str, regions: dict[str, Region]) -> None:
    lines = map_text.splitlines()

    linker_map_idx = None
    for i, line in enumerate(lines):
        if line.strip() == "Linker script and memory map":
            linker_map_idx = i
            break
    if linker_map_idx is None:
        raise RuntimeError("Section 'Linker script and memory map' introuvable dans le .map")

    section_re = re.compile(
        r"^(\.[^\s]+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)(?:\s+load\s+address\s+0x([0-9A-Fa-f]+))?"
    )

    for line in lines[linker_map_idx + 1 :]:
        m = section_re.match(line)
        if not m:
            continue

        _name, vma_hex, size_hex, lma_hex = m.groups()
        vma = int(vma_hex, 16)
        size = int(size_hex, 16)
        if size == 0:
            continue

        vma_region = find_region_for_address(regions, vma)
        if vma_region is not None:
            vma_region.used += size

        if lma_hex is not None:
            lma = int(lma_hex, 16)
            lma_region = find_region_for_address(regions, lma)
            if lma_region is not None and lma_region.name != (vma_region.name if vma_region else None):
                lma_region.used += size


def resolve_display_region_key(regions: dict[str, Region], display_name: str) -> str | None:
    if display_name in regions:
        return display_name
    for alias in REGION_ALIASES.get(display_name, []):
        if alias in regions:
            return alias
    return None


def print_report(regions: dict[str, Region], order: list[str]) -> None:
    print("\nSTM32 Memory Usage (from .map)")
    print("-" * 60)
    print(f"{'Region':<10} {'Used / Total (bytes)':>30} {'Usage':>10}")
    print("-" * 60)

    for display_name in order:
        key = resolve_display_region_key(regions, display_name)
        if key is None:
            print(f"{display_name:<10} {'N/A':>30} {'N/A':>10}")
            continue
        region = regions[key]
        pct = (region.used / region.length * 100.0) if region.length else 0.0
        print(f"{display_name:<10} {region.used:>10} / {region.length:<10} {pct:>8.1f}%")


def main() -> int:
    parser = argparse.ArgumentParser(description="Report STM32 memory usage from GNU ld .map file")
    parser.add_argument("--map", dest="map_file", required=True, help="Path to .map file")
    parser.add_argument(
        "--regions",
        default=",".join(DEFAULT_DISPLAY_ORDER),
        help="Comma-separated display order (default: FLASH,DTCM,RAM_D1,RAM_D2,RAM_D3,SDRAM)",
    )
    args = parser.parse_args()

    map_path = pathlib.Path(args.map_file)
    if not map_path.is_file():
        print(f"Erreur: .map introuvable: {map_path}", file=sys.stderr)
        return 2

    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    regions = parse_regions(map_text)
    accumulate_usage(map_text, regions)

    order = [x.strip() for x in args.regions.split(",") if x.strip()]
    print_report(regions, order)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
