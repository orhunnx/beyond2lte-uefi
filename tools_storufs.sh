#!/usr/bin/env bash
#
# Windows ARM64 ISO'sundan storufs.inf'i cikarir ve bagladigi ACPI
# kimliklerini yazar.
#
#   bash tools_storufs.sh /yol/Win11_25H2_English_Arm64_v2.iso
#
# NEDEN: Windows'un kutudan cikan UFS surucusu storufs.sys, UFSHCI standardini
# destekliyor ve "Boot from UFS" yapabiliyor. Ama hangi ACPI _HID'ine
# bagladigi hicbir yerde belgelenmemis. DSDT'deki UFS0 dugumunun _HID'i
# su an GECICI ("SMSN9820", bilerek eslesmeyen). Dogru kimlik buradan cikacak.
#
# Sandbox'ta 7z/wimlib yok ve apt aga cikamiyor, o yuzden bu is WSL'de.
#
set -euo pipefail

ISO="${1:-}"
if [ -z "$ISO" ]; then
  for c in "$HOME"/*.iso /mnt/c/Users/*/Downloads/*Arm64*.iso /mnt/c/Users/*/Downloads/*.iso; do
    [ -f "$c" ] && { ISO="$c"; break; }
  done
fi
[ -f "$ISO" ] || { echo "ISO bulunamadi. Yolu argüman olarak ver."; exit 1; }

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
say "ISO: $ISO ($(du -h "$ISO" | cut -f1))"

for t in 7z wimlib-imagex; do
  command -v $t >/dev/null || { echo "$t yok -> sudo apt install -y p7zip-full wimtools"; exit 1; }
done

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

#
# boot.wim tercih ediliyor: WinPE'nin UFS'ten acilabilmesi gerektigi icin
# storufs orada bulunmak zorunda, ve install.wim'den cok daha kucuk.
#
say "sources/boot.wim cikariliyor"
7z e -o"$W" "$ISO" sources/boot.wim >/dev/null || {
  echo "boot.wim alinamadi. ISO icerigi:"; 7z l "$ISO" | grep -i '\.wim\|\.esd'; exit 1; }
ls -lh "$W/boot.wim"

say "WIM icinde storufs araniyor"
HIT="$(wimlib-imagex dir "$W/boot.wim" 1 2>/dev/null | grep -i storufs || true)"
if [ -z "$HIT" ]; then
  echo "boot.wim'de yok. install.wim deneniyor (buyuk, sabirli ol)..."
  7z e -o"$W" "$ISO" sources/install.wim >/dev/null
  HIT="$(wimlib-imagex dir "$W/install.wim" 1 2>/dev/null | grep -i storufs || true)"
  WIM="$W/install.wim"
else
  WIM="$W/boot.wim"
fi
[ -n "$HIT" ] || { echo "storufs hicbir WIM'de bulunamadi."; exit 1; }
echo "$HIT"

INF="$(echo "$HIT" | grep -i '\.inf$' | head -1 | sed 's|^/||')"
[ -n "$INF" ] || { echo "storufs.inf yok (sadece .sys var). Surucu deposuna bak."; exit 1; }

say "cikariliyor: $INF"
wimlib-imagex extract "$WIM" 1 "/$INF" --dest-dir="$W/out" --no-acls >/dev/null
OUT="$W/out/$(basename "$INF")"

say "ACPI kimlikleri"
grep -i -E 'ACPI\\|PCI\\VEN|%.*%=.*,' "$OUT" | grep -v '^;' | sed 's/^/  /'

say "tam INF"
cat "$OUT"

# Depoya kopyala ki bir daha ISO gerekmesin
DEST="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/windows/storufs.inf"
mkdir -p "$(dirname "$DEST")"
cp "$OUT" "$DEST"
echo
echo "Kopyalandi: $DEST"
