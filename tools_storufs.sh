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
RAW="$W/out/$(basename "$INF")"

#
# Windows INF dosyalari genelde UTF-16LE. ASCII desenli grep onlarda hicbir
# sey bulamaz - ilk denemede "ACPI kimlikleri" bolumu bos cikti. Ayrica
# eslesme bulamayan grep 1 donuyor ve "set -e" script'i oracikta olduruyor,
# o yuzden asagida hepsi "|| true" ile.
#
DEST="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/windows/storufs.inf"
mkdir -p "$(dirname "$DEST")"

case "$(od -An -tx1 -N2 "$RAW" | tr -d ' ')" in
  fffe) echo "  (UTF-16LE tespit edildi, UTF-8'e cevriliyor)"
        iconv -f UTF-16LE -t UTF-8 "$RAW" > "$DEST" 2>/dev/null || strings "$RAW" > "$DEST" ;;
  *)    cp "$RAW" "$DEST" ;;
esac
# Windows satir sonlarini temizle
sed -i 's/\r$//' "$DEST"

say "ACPI / donanim kimlikleri"
grep -i -E 'ACPI\\|PCI\\|_HID|HardwareID' "$DEST" | grep -v '^;' | sed 's/^/  /' || echo "  (desen eslesmedi)"

say "Models bolumleri (asil kimlikler burada)"
awk '/^\[.*\.NT|^\[Standard|^\[.*Models/{p=1} /^\[/{if(!/Models|Standard|\.NT/)p=0} p' "$DEST" | sed 's/^/  /' || true

say "tam INF"
cat "$DEST"

echo
echo "Kopyalandi: $DEST"
