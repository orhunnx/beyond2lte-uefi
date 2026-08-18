#!/usr/bin/env python3
"""B2LUFS sicak-reset ucus kaydi ABI/decoder regresyon testi."""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "tools_b2lufs_flight_decode.py"

spec = importlib.util.spec_from_file_location("b2lufs_flight_decode", DECODER)
if spec is None or spec.loader is None:
    raise AssertionError("decoder modulu yuklenemedi")
decoder = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = decoder
spec.loader.exec_module(decoder)


def make_record(sequence: int, event: int, *args: int) -> bytes:
    padded = tuple(args) + (0,) * (5 - len(args))
    word = (event << 16) | (2 << 8) | 1
    commit = decoder.RECORD_COMMIT_MAGIC ^ sequence ^ word
    for value in padded:
        commit ^= value
    return struct.pack("<IHBB5II", sequence, event, 2, 1, *padded, commit)


def main() -> None:
    assert struct.calcsize("<IHHHHIIII9I") == decoder.HEADER_SIZE
    assert struct.calcsize("<IHBB5II") == decoder.RECORD_SIZE
    image = bytearray(decoder.FLIGHT_BYTES)
    capacity = (decoder.FLIGHT_BYTES - decoder.HEADER_SIZE) // decoder.RECORD_SIZE
    struct.pack_into(
        "<IHHHHIIII",
        image,
        0,
        decoder.FLIGHT_MAGIC,
        decoder.FLIGHT_VERSION,
        decoder.HEADER_SIZE,
        decoder.RECORD_SIZE,
        0,
        capacity,
        2,
        7,
        0,
    )
    image[decoder.HEADER_SIZE : decoder.HEADER_SIZE + decoder.RECORD_SIZE] = (
        make_record(1, 0x0001, 0, 0xCF050000)
    )
    image[
        decoder.HEADER_SIZE
        + decoder.RECORD_SIZE : decoder.HEADER_SIZE
        + 2 * decoder.RECORD_SIZE
    ] = make_record(2, 0x020F, 1, 1, 1, 1, 1)

    header, records = decoder.decode_bytes(bytes(image))
    assert header.boot_id == 7
    assert [record.sequence for record in records] == [1, 2]
    assert records[-1].event_name == "INIT_OK"

    image[decoder.HEADER_SIZE + decoder.RECORD_SIZE + 31] ^= 0x80
    _, records = decoder.decode_bytes(bytes(image))
    assert [record.sequence for record in records] == [1]

    struct.pack_into("<I", image, 0, decoder.ARMED_MAGIC)
    header, records = decoder.decode_bytes(bytes(image))
    assert header.armed
    assert not records

    print("B2LUFS ucus kaydi decoder testleri BASARILI.")


if __name__ == "__main__":
    main()
