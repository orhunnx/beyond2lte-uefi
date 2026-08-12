#!/usr/bin/env python3
"""Select the PSCI SMC/HVC conduit in a compiled FADT without touching source.

The FADT used by this project is an iASL data-table source.  The build copies
and compiles it under Mu-Silicium, then this helper toggles only ARM_BOOT_ARCH
bit 1 (PSCI_USE_HVC) in the resulting FACP.aml.  It validates the table and
recomputes the ACPI checksum, so a failed or stale patch cannot silently enter
an Odin image.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


FADT_SIGNATURE = b"FACP"
FADT_LENGTH_OFFSET = 4
FADT_CHECKSUM_OFFSET = 9
FADT_ARM_BOOT_ARCH_OFFSET = 0x81
PSCI_COMPLIANT = 0x0001
PSCI_USE_HVC = 0x0002


def die(message: str) -> "None":
    print(f"HATA: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="FADT ARM_BOOT_ARCH PSCI conduit bitini ayarla."
    )
    parser.add_argument("fadt", type=Path, help="derlenmis FACP.aml yolu")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--smc", action="store_true", help="PSCI HVC bitini kapat")
    group.add_argument("--hvc", action="store_true", help="PSCI HVC bitini ac")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        data = bytearray(args.fadt.read_bytes())
    except OSError as exc:
        die(f"FADT okunamadi ({args.fadt}): {exc}")

    if len(data) < FADT_ARM_BOOT_ARCH_OFFSET + 2:
        die(f"FADT cok kisa: {len(data)} bayt")
    if data[:4] != FADT_SIGNATURE:
        die(f"FADT imzasi FACP degil: {data[:4]!r}")

    length = int.from_bytes(
        data[FADT_LENGTH_OFFSET : FADT_LENGTH_OFFSET + 4], "little"
    )
    if length < FADT_ARM_BOOT_ARCH_OFFSET + 2 or length > len(data):
        die(f"FADT uzunlugu gecersiz: {length} (dosya {len(data)} bayt)")

    flags = int.from_bytes(
        data[FADT_ARM_BOOT_ARCH_OFFSET : FADT_ARM_BOOT_ARCH_OFFSET + 2], "little"
    )
    if not flags & PSCI_COMPLIANT:
        die(
            "PSCI_COMPLIANT biti kapali; HVC modu PSCI olmayan FADT ile "
            "anlamli degil"
        )

    wanted = flags | PSCI_USE_HVC if args.hvc else flags & ~PSCI_USE_HVC
    data[FADT_ARM_BOOT_ARCH_OFFSET : FADT_ARM_BOOT_ARCH_OFFSET + 2] = wanted.to_bytes(
        2, "little"
    )
    data[FADT_CHECKSUM_OFFSET] = 0
    data[FADT_CHECKSUM_OFFSET] = (-sum(data[:length])) & 0xFF

    if sum(data[:length]) & 0xFF:
        die("FADT checksum hesaplamasi basarisiz")

    try:
        args.fadt.write_bytes(data)
    except OSError as exc:
        die(f"FADT yazilamadi ({args.fadt}): {exc}")

    conduit = "HVC" if wanted & PSCI_USE_HVC else "SMC"
    print(
        f"FADT dogrulandi: ARM_BOOT_ARCH=0x{wanted:04X}, PSCI conduit={conduit}, "
        f"checksum=0x{data[FADT_CHECKSUM_OFFSET]:02X}"
    )


if __name__ == "__main__":
    main()
