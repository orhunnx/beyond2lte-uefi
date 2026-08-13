#!/usr/bin/env python3
"""Validate the fixed physical layout shared by BootShim and beyond2lte UEFI.

BootShim copies exactly ``[uefi_fd].size`` bytes to ``[uefi_fd].base`` before
branching into it.  The memory map and the primary CPU stack must therefore
describe a non-overlapping region outside that copy.
"""

from __future__ import annotations

import re
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / "mu-silicium-overlay/Resources/Configs/beyond2lte.toml"
MEMORY_MAP = (
    ROOT
    / "mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib.c"
)
DSC = ROOT / "mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"


def fail(message: str) -> None:
    print(f"HATA: UEFI bellek yerlesimi gecersiz: {message}", file=sys.stderr)
    raise SystemExit(1)


def region(source: str, name: str) -> tuple[int, int]:
    match = re.search(
        rf'\{{"{re.escape(name)}",\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)',
        source,
    )
    if not match:
        fail(f'MemoryMapLib icinde "{name}" bolgesi bulunamadi')
    return int(match.group(1), 0), int(match.group(2), 0)


def overlaps(first: tuple[int, int], second: tuple[int, int]) -> bool:
    first_base, first_size = first
    second_base, second_size = second
    return first_base < second_base + second_size and second_base < first_base + first_size


def main() -> None:
    with CONFIG.open("rb") as config_file:
        config = tomllib.load(config_file)

    fd_config = config.get("uefi_fd")
    if not isinstance(fd_config, dict):
        fail("[uefi_fd] bolumu yok")

    try:
        fd = (int(fd_config["base"]), int(fd_config["size"]))
    except (KeyError, TypeError, ValueError):
        fail("[uefi_fd].base/size tamsayi degil")

    if fd[1] == 0:
        fail("FD boyutu sifir")

    memory_map = MEMORY_MAP.read_text(encoding="utf-8")
    dsc = DSC.read_text(encoding="utf-8")
    mapped_fd = region(memory_map, "UEFI FD")
    stack = region(memory_map, "UEFI Stack")
    display = region(memory_map, "Display Reserved")

    pcd_match = re.search(
        r"gArmPlatformTokenSpaceGuid\.PcdCPUCoresStackBase\|(0x[0-9A-Fa-f]+)",
        dsc,
    )
    if not pcd_match:
        fail("PcdCPUCoresStackBase bulunamadi")
    pcd_stack_base = int(pcd_match.group(1), 0)

    if mapped_fd != fd:
        fail(
            "config FD=0x%X+0x%X, MemoryMapLib FD=0x%X+0x%X"
            % (*fd, *mapped_fd)
        )
    if stack[0] != pcd_stack_base:
        fail(
            "stack tabani MemoryMapLib=0x%X, DSC PCD=0x%X"
            % (stack[0], pcd_stack_base)
        )
    if overlaps(fd, stack):
        fail("FD ve CPU stack cakiliyor")
    if overlaps(fd, display) or overlaps(stack, display):
        fail("FD veya CPU stack framebuffer rezerviyle cakisiyor")

    print(
        "UEFI yerlesimi dogrulandi: "
        f"FD=0x{fd[0]:08X}..0x{fd[0] + fd[1]:08X}, "
        f"stack=0x{stack[0]:08X}..0x{stack[0] + stack[1]:08X}"
    )


if __name__ == "__main__":
    main()
