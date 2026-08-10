#!/usr/bin/env bash
#
# Cihazdan geri okunan cache imajindan Windows'un ACILIS DURUM KAYDINI cikarir.
#
#   bash tools_bootstat.sh cache_read.img
#
# FIKIR NEREDEN GELDI: "dosyalari RAM'e degil baska bolume yazsak logu
# alamaz miyiz?" - dogru soru, ve Windows bunu ZATEN yapiyor.
#
# bootmgr her acilisin durumunu ESP'ye "bootstat.dat" olarak yaziyor
# (Boot Status Data Log). Bir sonraki aciliste onu okuyup "Windows failed to
# start" ekranini gosteriyor. Yani cihaz bize soylemese bile bolumde kayit
# birakiyor olabilir.
#
# Bu script iki sey yapiyor:
#   1) bootstat.dat'i bulup icindeki NTSTATUS benzeri degerleri tariyor
#   2) imajda BIZIM koymadigimiz her dosyayi listeliyor - Windows'un yazdigi
#      her sey ilgi cekici (WINLOG.TXT, bootstat.dat, baska ne varsa)
#
# AYRICA: bu dosyalardan herhangi biri varsa, UEFI'nin UFS'e YAZMA yolunun
# calistigi da kanitlanmis olur (STATUS.md'de acik madde).
#
set -uo pipefail
IMG="${1:-}"
[ -f "$IMG" ] || { echo "Kullanim: bash tools_bootstat.sh cache_read.img"; exit 1; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

say "imaj icerigi"
python3 "$HERE/tools_fatread.py" "$IMG" 2>/dev/null || {
  echo "tools_fatread.py calistirilamadi"; exit 1; }

OUT="$(mktemp -d)"
python3 "$HERE/tools_fatread.py" "$IMG" "$OUT" >/dev/null 2>&1

say "BIZIM KOYMADIGIMIZ dosyalar (Windows'un yazdiklari)"
BIZIM='BOOTAA64.EFI|STARTUP.NSH|bootmgfw.efi|BCD|boot.sdi|boot.wim|vmlinuz|initramfs|\.ttf$'
FOUND=0
while IFS= read -r f; do
  echo "$f" | grep -qE "$BIZIM" && continue
  printf '  %8s  %s\n' "$(stat -c%s "$f")" "${f#$OUT/}"
  FOUND=1
done < <(find "$OUT" -type f | sort)
[ "$FOUND" = 0 ] && echo "  (yok - Windows bolume hicbir sey yazmamis)"

say "bootstat.dat"
BS="$(find "$OUT" -iname 'bootstat.dat' | head -1)"
if [ -n "$BS" ]; then
  echo "  bulundu: ${BS#$OUT/}  ($(stat -c%s "$BS") bayt)"
  echo "  --- icindeki 0xC00000xx benzeri degerler:"
  python3 - "$BS" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read()
hits = {}
for i in range(0, len(d) - 4, 4):
    v = struct.unpack('<I', d[i:i+4])[0]
    # NTSTATUS hata araligi
    if 0xC0000000 <= v <= 0xC0FFFFFF:
        hits.setdefault(v, []).append(i)
if hits:
    for v, offs in sorted(hits.items()):
        print(f"    0x{v:08X}   {len(offs)} kez  (ilk offset {offs[0]})")
    print("\n    Windows'ta:  certutil -error 0x<deger>")
else:
    print("    (hata kodu bulunamadi - acilis basarili sayilmis olabilir)")
PY
else
  echo "  YOK."
  echo "  Iki anlama gelebilir:"
  echo "    a) bootmgr yazamadi -> UEFI'nin UFS'e yazma yolu calismiyor"
  echo "    b) bootstatuspolicy kapali -> 'hatagoster' varyantiyla dene"
fi

say "cikarilan dosyalar"
echo "  $OUT"
