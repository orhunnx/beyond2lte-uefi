#!/usr/bin/env python3
"""Decode a 64 KiB B2LUFS warm-reset flight-recorder image."""

from __future__ import annotations

import argparse
import dataclasses
import struct
from pathlib import Path


FLIGHT_BYTES = 0x10000
FLIGHT_MAGIC = 0x46553242
ARMED_MAGIC = 0x41553242
FLIGHT_VERSION = 1
HEADER_SIZE = 64
RECORD_SIZE = 32
RECORD_COMMIT_MAGIC = 0xC17F10A7
IRQL_UNKNOWN = 0xFF

EVENT_NAMES = {
    0x0001: "DRIVER_ENTRY",
    0x0002: "STORPORT_RETURN",
    0x0100: "FIND_BEGIN",
    0x0101: "FIND_FEATURE",
    0x0102: "FIND_RANGE",
    0x0103: "FIND_ALLOCATE",
    0x010F: "FIND_RESULT",
    0x0200: "INIT_BEGIN",
    0x0201: "TRANSPORT",
    0x0202: "CAPACITY",
    0x0203: "LUNS",
    0x0204: "MEDIA",
    0x020F: "INIT_OK",
    0x0210: "INIT_FAIL",
    0x0211: "PASSIVE_INIT",
    0x0212: "DEFERRED_SCAN",
    0x0300: "START_IO",
    0x0301: "CACHED_CAPACITY",
    0x0302: "CACHED_GPT",
    0x0310: "SUBMIT_BEGIN",
    0x0311: "SUBMIT_DONE",
    0x0320: "UNIT_CONTROL",
    0x0321: "ADAPTER_CONTROL",
    0x0322: "RESET_BUS",
}


@dataclasses.dataclass(frozen=True)
class FlightHeader:
    magic: int
    version: int
    header_size: int
    record_size: int
    capacity: int
    write_index: int
    boot_id: int
    flags: int

    @property
    def armed(self) -> bool:
        return self.magic == ARMED_MAGIC


@dataclasses.dataclass(frozen=True)
class FlightRecord:
    sequence: int
    event: int
    irql: int
    flags: int
    arguments: tuple[int, int, int, int, int]

    @property
    def event_name(self) -> str:
        return EVENT_NAMES.get(self.event, f"EVENT_{self.event:04X}")


def _record_commit(
    sequence: int,
    event: int,
    irql: int,
    flags: int,
    arguments: tuple[int, int, int, int, int],
) -> int:
    value = RECORD_COMMIT_MAGIC ^ sequence ^ (event << 16) ^ (irql << 8) ^ flags
    for argument in arguments:
        value ^= argument
    return value & 0xFFFFFFFF


def format_irql(irql: int) -> str:
    return "?" if irql == IRQL_UNKNOWN else f"{irql:X}"


def decode_bytes(data: bytes) -> tuple[FlightHeader, list[FlightRecord]]:
    if len(data) < FLIGHT_BYTES:
        raise ValueError(f"kayit {len(data)} bayt; beklenen en az {FLIGHT_BYTES}")
    fields = struct.unpack_from("<IHHHHIIII", data, 0)
    header = FlightHeader(
        magic=fields[0],
        version=fields[1],
        header_size=fields[2],
        record_size=fields[3],
        capacity=fields[5],
        write_index=fields[6],
        boot_id=fields[7],
        flags=fields[8],
    )
    if header.magic not in (FLIGHT_MAGIC, ARMED_MAGIC):
        raise ValueError(f"B2UF/B2UA imzasi yok: 0x{header.magic:08X}")
    if (
        header.version != FLIGHT_VERSION
        or header.header_size != HEADER_SIZE
        or header.record_size != RECORD_SIZE
        or header.capacity != (FLIGHT_BYTES - HEADER_SIZE) // RECORD_SIZE
    ):
        raise ValueError("ucus kaydi ABI alanlari uyusmuyor")
    if header.armed:
        return header, []

    first = max(1, header.write_index - header.capacity + 1)
    records: list[FlightRecord] = []
    for sequence in range(first, header.write_index + 1):
        slot = (sequence - 1) % header.capacity
        offset = HEADER_SIZE + slot * RECORD_SIZE
        unpacked = struct.unpack_from("<IHBB5II", data, offset)
        arguments = tuple(unpacked[4:9])
        if unpacked[0] != sequence:
            continue
        if unpacked[9] != _record_commit(
            unpacked[0], unpacked[1], unpacked[2], unpacked[3], arguments
        ):
            continue
        records.append(
            FlightRecord(
                sequence=unpacked[0],
                event=unpacked[1],
                irql=unpacked[2],
                flags=unpacked[3],
                arguments=arguments,  # type: ignore[arg-type]
            )
        )
    return header, records


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path, help="0xCF050000'dan alinmis 64 KiB")
    parser.add_argument("--last", type=int, default=64, help="son kayit sayisi")
    args = parser.parse_args()

    header, records = decode_bytes(args.image.read_bytes())
    state = "ARMED" if header.armed else "RECORDED"
    print(
        f"B2LUFS flight v{header.version} state={state} "
        f"boot={header.boot_id} writes={header.write_index}"
    )
    for record in records[-max(0, args.last) :]:
        arguments = " ".join(f"{value:08X}" for value in record.arguments)
        print(
            f"{record.sequence:08X} {record.event_name:<16} "
            f"irql={format_irql(record.irql)} flags={record.flags:02X} {arguments}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
