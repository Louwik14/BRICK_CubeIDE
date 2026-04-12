#!/usr/bin/env python3
"""STM32CubeIDE memory report from GNU ld .map file.

Features:
- Global region usage summary (FLASH/DTCM/RAM_D1/RAM_D2/RAM_D3/SDRAM)
- Detailed symbol map for selected RAM regions (default: RAM_D1,RAM_D2)

Usage:
  python tools/report_mem.py --map <path/to/project.map>
  python tools/report_mem.py --map <path/to/project.map> --top 20
  python tools/report_mem.py --map <path/to/project.map> --csv out.csv
"""

from __future__ import annotations

import argparse
import csv
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


@dataclass
class SymbolEntry:
    region: str
    symbol: str
    size: int
    section: str
    source: str
    address: int


DEFAULT_DISPLAY_ORDER = ["FLASH", "DTCM", "RAM_D1", "RAM_D2", "RAM_D3", "SDRAM"]
DEFAULT_DETAIL_REGIONS = ["RAM_D1", "RAM_D2"]
REGION_ALIASES = {
    "DTCM": ["DTCM", "DTCMRAM"],
}


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


def parse_symbol_entries(map_text: str, regions: dict[str, Region], target_regions: set[str]) -> list[SymbolEntry]:
    lines = map_text.splitlines()

    linker_map_idx = None
    for i, line in enumerate(lines):
        if line.strip() == "Linker script and memory map":
            linker_map_idx = i
            break
    if linker_map_idx is None:
        raise RuntimeError("Section 'Linker script and memory map' introuvable dans le .map")

    contribution_re = re.compile(r"^\s*(\.[^\s]+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(.+?)\s*$")
    continuation_re = re.compile(r"^\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(.+?)\s*$")
    symbol_re = re.compile(r"^\s+0x([0-9A-Fa-f]+)\s+(.+?)\s*$")

    def looks_like_object_source(src: str) -> bool:
        return src.endswith('.o') or '.o)' in src or '.a(' in src

    entries: list[SymbolEntry] = []

    i = linker_map_idx + 1
    last_section = "(unknown)"
    while i < len(lines):
        line = lines[i]

        m = contribution_re.match(line)
        if m:
            section, addr_hex, size_hex, source = m.groups()
            last_section = section
        else:
            cm = continuation_re.match(line)
            if not cm:
                i += 1
                continue
            addr_hex, size_hex, source = cm.groups()
            section = last_section
        size = int(size_hex, 16)
        addr = int(addr_hex, 16)

        if source == "*fill*" or size == 0 or not looks_like_object_source(source):
            i += 1
            continue

        region = find_region_for_address(regions, addr)
        if region is None or region.name not in target_regions:
            i += 1
            continue

        symbol_name = "(anonymous)"

        j = i + 1
        while j < len(lines):
            next_line = lines[j]

            if contribution_re.match(next_line) or continuation_re.match(next_line):
                break

            sm = symbol_re.match(next_line)
            if sm:
                sym_addr_hex, sym_name = sm.groups()
                if int(sym_addr_hex, 16) == addr:
                    stripped = sym_name.strip()
                    if (
                        stripped
                        and " = " not in stripped
                        and not stripped.startswith('.')
                        and stripped != "__bss_start__"
                    ):
                        symbol_name = stripped
                        break
            j += 1

        entries.append(
            SymbolEntry(
                region=region.name,
                symbol=symbol_name,
                size=size,
                section=section,
                source=source,
                address=addr,
            )
        )
        i += 1

    entries.sort(key=lambda e: e.size, reverse=True)
    return entries


def print_region_usage(regions: dict[str, Region], order: list[str]) -> None:
    print("\nSTM32 Memory Usage (from .map)")
    print("-" * 62)
    print(f"{'Region':<10} {'Used / Total (bytes)':>30} {'Usage':>10}")
    print("-" * 62)

    for display_name in order:
        key = resolve_display_region_key(regions, display_name)
        if key is None:
            print(f"{display_name:<10} {'N/A':>30} {'N/A':>10}")
            continue
        region = regions[key]
        pct = (region.used / region.length * 100.0) if region.length else 0.0
        print(f"{display_name:<10} {region.used:>10} / {region.length:<10} {pct:>8.1f}%")


def print_symbol_tables(entries: list[SymbolEntry], detail_regions: list[str], top_n: int) -> None:
    by_region: dict[str, list[SymbolEntry]] = {r: [] for r in detail_regions}
    for e in entries:
        if e.region in by_region:
            by_region[e.region].append(e)

    for region in detail_regions:
        region_entries = by_region.get(region, [])
        top_entries = region_entries[:top_n]
        total_top = sum(e.size for e in top_entries)
        total_region = sum(e.size for e in region_entries)

        print(f"\nTop {min(top_n, len(region_entries))} occupants in {region}")
        print("-" * 120)
        print(f"{'Region':<8} {'Symbol':<34} {'Size(B)':>10} {'Section':<20} {'Source':<40}")
        print("-" * 120)
        for e in top_entries:
            symbol = (e.symbol[:33] + "…") if len(e.symbol) > 34 else e.symbol
            source = (e.source[:39] + "…") if len(e.source) > 40 else e.source
            print(f"{e.region:<8} {symbol:<34} {e.size:>10} {e.section:<20} {source:<40}")

        print("-" * 120)
        print(
            f"Total {region}: {total_region} B | Total top {min(top_n, len(region_entries))}: {total_top} B "
            f"({(100.0 * total_top / total_region) if total_region else 0.0:.1f}%)"
        )


def write_csv(entries: list[SymbolEntry], csv_path: pathlib.Path) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["region", "symbol", "size_bytes", "section", "source", "address_hex"])
        for e in entries:
            w.writerow([e.region, e.symbol, e.size, e.section, e.source, f"0x{e.address:08X}"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Report STM32 memory usage and RAM_D1/RAM_D2 occupants from GNU ld .map")
    parser.add_argument("--map", dest="map_file", required=True, help="Path to .map file")
    parser.add_argument(
        "--regions",
        default=",".join(DEFAULT_DISPLAY_ORDER),
        help="Comma-separated display order (default: FLASH,DTCM,RAM_D1,RAM_D2,RAM_D3,SDRAM)",
    )
    parser.add_argument(
        "--detail-regions",
        default=",".join(DEFAULT_DETAIL_REGIONS),
        help="Comma-separated regions for symbol table (default: RAM_D1,RAM_D2)",
    )
    parser.add_argument("--top", type=int, default=10, help="Top N entries per detailed region (default: 10)")
    parser.add_argument("--csv", type=str, default="", help="Optional CSV output path for all detailed entries")
    args = parser.parse_args()

    map_path = pathlib.Path(args.map_file)
    if not map_path.is_file():
        print(f"Erreur: .map introuvable: {map_path}", file=sys.stderr)
        return 2

    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    regions = parse_regions(map_text)
    accumulate_usage(map_text, regions)

    order = [x.strip() for x in args.regions.split(",") if x.strip()]
    detail_regions = [x.strip() for x in args.detail_regions.split(",") if x.strip()]
    target_regions = set(detail_regions)

    print_region_usage(regions, order)

    entries = parse_symbol_entries(map_text, regions, target_regions)

    if args.csv:
        csv_path = pathlib.Path(args.csv)
        write_csv(entries, csv_path)
        print(f"\nCSV écrit: {csv_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
