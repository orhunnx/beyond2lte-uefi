#!/usr/bin/env python3
"""Create a low-chunk-count Android sparse image from a raw image.

NTFS images converted at 4 KiB granularity can contain thousands of alternating
RAW/DONT_CARE chunks. This writer classifies 1 MiB windows, bridges short zero
gaps, then proves the sparse expansion is byte-equivalent to the raw source.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct


MAGIC = 0xED26FF3A
FILE_HEADER_SIZE = 28
CHUNK_HEADER_SIZE = 12
BLOCK_SIZE = 4096
CHUNK_RAW = 0xCAC1
CHUNK_DONT_CARE = 0xCAC3


def fail(message: str) -> None:
    raise SystemExit(f"HATA: {message}")


class SparseWriter:
    def __init__(self, stream, total_blocks: int, max_raw_bytes: int) -> None:
        self.stream = stream
        self.max_raw_bytes = max_raw_bytes
        self.chunk_count = 0
        self.raw_header_offset: int | None = None
        self.raw_bytes = 0
        self.raw_total = 0
        self.skipped_total = 0
        self.stream.write(
            struct.pack(
                "<IHHHHIIII",
                MAGIC, 1, 0, FILE_HEADER_SIZE, CHUNK_HEADER_SIZE,
                BLOCK_SIZE, total_blocks, 0, 0,
            )
        )

    def _start_raw(self) -> None:
        self.raw_header_offset = self.stream.tell()
        self.stream.write(b"\0" * CHUNK_HEADER_SIZE)
        self.raw_bytes = 0

    def _finish_raw(self) -> None:
        if self.raw_header_offset is None:
            return
        if self.raw_bytes <= 0 or self.raw_bytes % BLOCK_SIZE:
            fail("gecersiz RAW chunk uzunlugu")
        end = self.stream.tell()
        blocks = self.raw_bytes // BLOCK_SIZE
        self.stream.seek(self.raw_header_offset)
        self.stream.write(
            struct.pack("<HHII", CHUNK_RAW, 0, blocks,
                        CHUNK_HEADER_SIZE + self.raw_bytes)
        )
        self.stream.seek(end)
        self.chunk_count += 1
        self.raw_total += self.raw_bytes
        self.raw_header_offset = None
        self.raw_bytes = 0

    def append_data(self, data: bytes) -> None:
        if len(data) % BLOCK_SIZE:
            fail("RAW veri blok boyuna hizali degil")
        view = memoryview(data)
        while view:
            if self.raw_header_offset is None:
                self._start_raw()
            capacity = self.max_raw_bytes - self.raw_bytes
            piece = view[:capacity]
            self.stream.write(piece)
            written = len(piece)
            self.raw_bytes += written
            view = view[written:]
            if self.raw_bytes == self.max_raw_bytes:
                self._finish_raw()

    def append_zeros(self, size: int, zero_buffer: bytes) -> None:
        while size:
            piece_size = min(size, len(zero_buffer))
            self.append_data(zero_buffer[:piece_size])
            size -= piece_size

    def finish_raw(self) -> None:
        self._finish_raw()

    def append_skip(self, blocks: int) -> None:
        if blocks <= 0:
            return
        self._finish_raw()
        self.stream.write(
            struct.pack("<HHII", CHUNK_DONT_CARE, 0, blocks, CHUNK_HEADER_SIZE)
        )
        self.chunk_count += 1
        self.skipped_total += blocks * BLOCK_SIZE

    def finish(self) -> None:
        self._finish_raw()
        end = self.stream.tell()
        self.stream.seek(20)
        self.stream.write(struct.pack("<I", self.chunk_count))
        self.stream.seek(end)


def create_sparse(raw_path: Path, output_path: Path, window_mib: int,
                  bridge_mib: int, max_raw_mib: int) -> tuple[int, int, int]:
    raw_size = raw_path.stat().st_size
    if raw_size <= 0 or raw_size % BLOCK_SIZE:
        fail(f"ham imaj boyu {BLOCK_SIZE} bayta hizali degil: {raw_size}")
    window_bytes = window_mib * 1024 * 1024
    bridge_bytes = bridge_mib * 1024 * 1024
    max_raw_bytes = max_raw_mib * 1024 * 1024
    for label, value in (("window", window_bytes), ("bridge", bridge_bytes),
                         ("max RAW", max_raw_bytes)):
        if value <= 0 or value % BLOCK_SIZE:
            fail(f"{label} boyu blok boyuna hizali olmali")
    if max_raw_bytes + CHUNK_HEADER_SIZE > 0xFFFFFFFF:
        fail("max RAW chunk 32-bit sparse total_sz alanina sigmiyor")

    zero_window = b"\0" * window_bytes
    temporary = output_path.with_name(f".{output_path.name}.tmp-{os.getpid()}")
    pending_zero_bytes = 0
    skip_blocks = 0
    try:
        with raw_path.open("rb") as source, temporary.open("w+b") as destination:
            writer = SparseWriter(destination, raw_size // BLOCK_SIZE, max_raw_bytes)
            while True:
                data = source.read(window_bytes)
                if not data:
                    break
                if len(data) % BLOCK_SIZE:
                    fail("son okuma blok boyuna hizali degil")
                if data == zero_window:
                    if writer.raw_header_offset is not None:
                        pending_zero_bytes += len(data)
                        if pending_zero_bytes > bridge_bytes:
                            writer.finish_raw()
                            skip_blocks += pending_zero_bytes // BLOCK_SIZE
                            pending_zero_bytes = 0
                    else:
                        skip_blocks += len(data) // BLOCK_SIZE
                    continue

                if writer.raw_header_offset is not None and pending_zero_bytes:
                    writer.append_zeros(pending_zero_bytes, zero_window)
                    pending_zero_bytes = 0
                elif writer.raw_header_offset is None and skip_blocks:
                    writer.append_skip(skip_blocks)
                    skip_blocks = 0
                writer.append_data(data)

            if pending_zero_bytes:
                writer.finish_raw()
                skip_blocks += pending_zero_bytes // BLOCK_SIZE
            if skip_blocks:
                writer.append_skip(skip_blocks)
            writer.finish()
            destination.flush()
            os.fsync(destination.fileno())
        os.replace(temporary, output_path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    return writer.chunk_count, writer.raw_total, writer.skipped_total


def read_exact(stream, size: int) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        fail("sparse imaj beklenmeden bitti")
    return data


def verify_sparse(raw_path: Path, sparse_path: Path) -> int:
    raw_size = raw_path.stat().st_size
    zero_buffer = b"\0" * (8 * 1024 * 1024)
    with raw_path.open("rb") as raw, sparse_path.open("rb") as sparse:
        values = struct.unpack("<IHHHHIIII", read_exact(sparse, FILE_HEADER_SIZE))
        magic, major, minor, file_header, chunk_header, block_size = values[:6]
        total_blocks, total_chunks, checksum = values[6:]
        if (magic, major, minor, file_header, chunk_header, block_size) != (
            MAGIC, 1, 0, FILE_HEADER_SIZE, CHUNK_HEADER_SIZE, BLOCK_SIZE
        ):
            fail("uretilen sparse basligi gecersiz")
        if total_blocks * block_size != raw_size or checksum != 0:
            fail("sparse mantiksal boyu/checksum alani gecersiz")
        expanded = 0
        for _ in range(total_chunks):
            chunk_type, reserved, blocks, total_size = struct.unpack(
                "<HHII", read_exact(sparse, CHUNK_HEADER_SIZE)
            )
            if reserved != 0:
                fail("sparse chunk reserved alani sifir degil")
            byte_count = blocks * block_size
            if chunk_type == CHUNK_RAW:
                if total_size != CHUNK_HEADER_SIZE + byte_count:
                    fail("RAW chunk total_sz gecersiz")
                remaining = byte_count
                while remaining:
                    amount = min(remaining, len(zero_buffer))
                    if read_exact(sparse, amount) != read_exact(raw, amount):
                        fail(f"RAW chunk ham imajla farkli (offset={expanded})")
                    expanded += amount
                    remaining -= amount
            elif chunk_type == CHUNK_DONT_CARE:
                if total_size != CHUNK_HEADER_SIZE:
                    fail("DONT_CARE chunk total_sz gecersiz")
                remaining = byte_count
                while remaining:
                    amount = min(remaining, len(zero_buffer))
                    if read_exact(raw, amount) != zero_buffer[:amount]:
                        fail(f"DONT_CARE bolgesinde veri var (offset={expanded})")
                    expanded += amount
                    remaining -= amount
            else:
                fail(f"beklenmeyen sparse chunk turu: 0x{chunk_type:04x}")
        if expanded != raw_size or sparse.read(1) or raw.read(1):
            fail("sparse/ham imaj uzunluk dogrulamasi basarisiz")
    return total_chunks


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--window-mib", type=int, default=1)
    parser.add_argument("--bridge-mib", type=int, default=4)
    parser.add_argument("--max-raw-mib", type=int, default=1024)
    args = parser.parse_args()
    if not args.raw.is_file():
        fail(f"ham imaj bulunamadi: {args.raw}")
    chunks, raw_bytes, skipped_bytes = create_sparse(
        args.raw, args.output, args.window_mib, args.bridge_mib, args.max_raw_mib
    )
    if verify_sparse(args.raw, args.output) != chunks:
        fail("uretim ve dogrulama chunk sayilari farkli")
    print(f"B2L_SPARSE_PROFILE=COARSE_{args.window_mib}M_BRIDGE_{args.bridge_mib}M")
    print(f"B2L_SPARSE_TOTAL_CHUNKS={chunks}")
    print(f"B2L_SPARSE_RAW_BYTES={raw_bytes}")
    print(f"B2L_SPARSE_SKIPPED_BYTES={skipped_bytes}")
    print("B2L_SPARSE_RAW_EQUIVALENT=1")


if __name__ == "__main__":
    main()
