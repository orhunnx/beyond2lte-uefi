#!/usr/bin/env python3
"""
PPTT (Processor Properties Topology Table) uretici - Exynos 9820 / beyond2lte.

NEDEN iasl DEGIL DE PYTHON:
iasl'in veri tablosu sozdiziminde PPTT alan adlari surumden surume degisiyor
ve yanlis yazarsan hatayi ancak WSL'de derlerken goruyorsun. Burada bayt
duzeni dogrudan ACPI 6.4 s5.2.29'a gore kuruluyor ve script kendi ciktisini
geri ayristirip dogruluyor. Tur kaybi yok.

TOPOLOJI - dogrudan DTB'nin cpu-map'inden:
    cluster0
      coregroup0 : cpu@0000..0003  "arm,ananke"      Cortex-A55  (kucuk)
      coregroup1 : cpu@0004..0005  "arm,prometheus"  Cortex-A75  (orta)
    cluster1
      coregroup0 : cpu@0100..0101  "arm,cheetah"     Mongoose M4 (buyuk)

ONBELLEK YOK: DTB'de cache-size ozellikleri bulunmuyor (Samsung cekirdegi
bunlari CCSIDR'dan okuyor). Uydurma degerler yazmaktansa Type 1 cache
yapilarini hic koymuyoruz - PPTT'de bunlar zorunlu degil. Windows'un ihtiyaci
olan sey topoloji, o da burada.
"""
import struct, sys, os

OEM_ID       = b"SAMSUN"
OEM_TABLE_ID = b"S5E9820 "
OEM_REV      = 0x9820
CREATOR_ID   = b"INTL"
CREATOR_REV  = 0x20230628

# Processor hierarchy node flags (ACPI 6.4, Tablo 5.148)
F_PHYSICAL_PACKAGE = 1 << 0
F_ACPI_ID_VALID    = 1 << 1
F_IS_THREAD        = 1 << 2
F_IS_LEAF          = 1 << 3

NODE_LEN = 20   # 1+1+2+4+4+4+4, ozel kaynak yokken


class Pptt:
    def __init__(self):
        self.body = b""
        self.names = []

    def node(self, name, flags, parent_off, acpi_id=0):
        """Bir Type 0 dugumu ekler, tablo basindan olcülen offsetini dondurur."""
        off = 36 + len(self.body)          # 36 = ACPI baslik uzunlugu
        self.body += struct.pack("<BBHIII", 0, NODE_LEN, 0,
                                 flags, parent_off, acpi_id) + struct.pack("<I", 0)
        self.names.append((off, name))
        return off

    def build(self):
        length = 36 + len(self.body)
        hdr = (b"PPTT" + struct.pack("<I", length) + bytes([3, 0])   # Revision 3
               + OEM_ID + OEM_TABLE_ID + struct.pack("<I", OEM_REV)
               + CREATOR_ID + struct.pack("<I", CREATOR_REV))
        t = bytearray(hdr + self.body)
        t[9] = (-sum(t)) & 0xFF                                      # Checksum
        return bytes(t)


def make():
    p = Pptt()
    # Kok: fiziksel paket. Parent 0 = yok.
    pkg = p.node("package", F_PHYSICAL_PACKAGE, 0)

    cl0 = p.node("cluster0", 0, pkg)
    cg_a55 = p.node("cluster0/coregroup0 (A55 x4)", 0, cl0)
    cg_a75 = p.node("cluster0/coregroup1 (A75 x2)", 0, cl0)

    cl1 = p.node("cluster1", 0, pkg)
    cg_m4 = p.node("cluster1/coregroup0 (M4 x2)", 0, cl1)

    leaf = F_ACPI_ID_VALID | F_IS_LEAF
    # ACPI Processor ID, MADT'deki Processor UID ile ayni olmak ZORUNDA.
    for uid in (0, 1, 2, 3):
        p.node(f"cpu{uid} A55", leaf, cg_a55, uid)
    for uid in (4, 5):
        p.node(f"cpu{uid} A75", leaf, cg_a75, uid)
    for uid in (6, 7):
        p.node(f"cpu{uid} M4", leaf, cg_m4, uid)
    return p


def verify(blob, p):
    """Uretileni geri ayristir. acpiview'in gorecegi seyi burada goruyoruz."""
    sig, length, rev, csum = blob[:4], *struct.unpack("<IBB", blob[4:10])
    assert sig == b"PPTT", sig
    assert length == len(blob), (length, len(blob))
    assert sum(blob) & 0xFF == 0, "checksum tutmuyor"

    print(f"  Signature {sig.decode()}  Length {length}  Revision {rev}  Checksum 0x{csum:02X} OK")
    print(f"  Oem {blob[10:16].decode()} / {blob[16:24].decode()}\n")

    byname = dict(p.names)
    off, n, leaves = 36, 0, 0
    while off < length:
        typ, ln = blob[off], blob[off + 1]
        flags, parent, aid, npr = struct.unpack("<IIII", blob[off + 4:off + 20])
        assert typ == 0 and ln == NODE_LEN and npr == 0
        # her parent ya 0 ya da gercek bir dugum offseti olmali
        assert parent == 0 or parent in byname, f"gecersiz parent {parent}"
        f = [s for b, s in ((F_PHYSICAL_PACKAGE, "package"), (F_ACPI_ID_VALID, "id_valid"),
                            (F_IS_THREAD, "thread"), (F_IS_LEAF, "leaf")) if flags & b]
        if flags & F_IS_LEAF:
            leaves += 1
        print(f"  @{off:<4} {byname[off]:<30} parent=@{parent:<4} "
              f"acpi_id={aid} flags={'|'.join(f) or '-'}")
        off += ln
        n += 1

    assert leaves == 8, f"8 yaprak bekleniyordu, {leaves} var"
    print(f"\n  {n} dugum, {leaves} yaprak (CPU). Tutarli.")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "silicium-acpi-overlay/SoCs/Samsung/S5E9820/PPTT.aml")
    p = make()
    blob = p.build()
    verify(blob, p)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(blob)
    print(f"\n  yazildi: {out} ({len(blob)} bayt)")
