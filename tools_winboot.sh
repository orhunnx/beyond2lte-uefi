#!/usr/bin/env bash
#
# Windows Boot Manager'i ISO'dan cikarip ESP'ye hazirlar.
#
#   bash tools_winboot.sh [/yol/Win11_..._Arm64.iso]
#
# AMAC: Windows'un ilk denemesi, HICBIR SEY SILMEDEN.
#
# Bu adimda Windows kurmuyoruz. Sadece Boot Manager'i calistirip ne oldugunu
# goruyoruz. Isletim sistemi bulamayip hata ekrani verecek - ama o ekrani
# CIZEBILMESI bile cok sey kanitliyor:
#   - GOP'umuz Windows'a yetiyor (kendi grafik cikisini kuruyor)
#   - ACPI tablolarimiz kabul edildi (bootmgr ACPI'yi okuyor)
#   - Bellek haritamiz gecerli (aksi halde daha once cokerdi)
#   - EFI cagri arayuzumuz Windows'un bekledigi gibi
#
# Beklenen hata: 0xc0000225 "boot device bulunamadi" ya da benzeri.
# BU BASARIDIR. Hicbir sey acilmazsa ya da cihaz yeniden baslarsa,
# sorun daha temelde demektir.
#
# NOT: boot.wim (606 MB) ESP'ye SIGMIYOR (CACHE 600 MB). Yani WinPE'yi
# RAM diskten acmak bu bolumle mumkun degil. Sonraki adimda ya daha buyuk
# bir bolum (SYSTEM 5.57 GB) kullanilacak ya da imaj dogrudan USERDATA'ya
# uygulanacak.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESP="$HERE/esp"

ISO="${1:-}"
if [ -z "$ISO" ]; then
  for c in "$HOME"/*.iso /mnt/c/Users/*/Downloads/*Arm64*.iso /mnt/c/Users/*/Downloads/*.iso; do
    [ -f "$c" ] && { ISO="$c"; break; }
  done
fi
[ -f "$ISO" ] || { echo "ISO bulunamadi. Yolu argüman olarak ver."; exit 1; }

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
command -v 7z >/dev/null || { echo "7z yok -> sudo apt install -y p7zip-full"; exit 1; }

say "ISO: $ISO"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

say "ISO icindeki boot dosyalari araniyor"
#
# ILK DENEMEDE BURASI SESSIZCE BASARISIZ OLDU. Cikarma satirlari "|| true" ile
# korunuyordu, BCD kopyalanmadi ve cihaz "0xc000000f - File: \EFI\Microsoft\
# Boot\BCD" verdi. Simdi yollar ISO listesinden BULUNUYOR (buyuk/kucuk harf
# ISO'dan ISO'ya degisiyor) ve eksik dosya varsa script DURUYOR.
#
LIST="$W/list.txt"
7z l -slt "$ISO" > "$LIST"

find_in_iso() {   # $1 = aranan yol deseni (buyuk/kucuk harf duyarsiz)
  grep -i '^Path = ' "$LIST" | sed 's/^Path = //' | grep -i -m1 -E "$1" || true
}

BM_PATH="$(find_in_iso '(^|[/\\])efi[/\\]boot[/\\]bootaa64\.efi$')"
BCD_PATH="$(find_in_iso '(^|[/\\])efi[/\\]microsoft[/\\]boot[/\\]bcd$')"
echo "  Boot Manager : ${BM_PATH:-BULUNAMADI}"
echo "  BCD          : ${BCD_PATH:-BULUNAMADI}"

[ -n "$BM_PATH" ]  || { echo "HATA: bootaa64.efi ISO'da yok."; exit 1; }
[ -n "$BCD_PATH" ] || { echo "HATA: BCD ISO'da yok."; exit 1; }

say "cikariliyor"
7z e -o"$W" "$ISO" "$BM_PATH"  >/dev/null
7z e -o"$W" "$ISO" "$BCD_PATH" >/dev/null
BM="$W/$(basename "$BM_PATH")"
BCD="$W/$(basename "$BCD_PATH")"

# Gercekten geldiler mi? Sessiz basarisizlik bir tur kaybettirdi.
[ -s "$BM" ]  || { echo "HATA: bootaa64.efi cikarilamadi."; exit 1; }
[ -s "$BCD" ] || { echo "HATA: BCD cikarilamadi."; exit 1; }

# BCD bir registry hive'i; "regf" ile baslar. Yanlis dosyayi almadigimizi dogrula.
MAGIC="$(head -c4 "$BCD")"
[ "$MAGIC" = "regf" ] || { echo "HATA: BCD registry hive gibi gorunmuyor (basi: $MAGIC)"; exit 1; }
echo "  BCD dogrulandi: regf, $(stat -c%s "$BCD") bayt"

# Yazi tipleri (hata ekranini duzgun cizmek icin)
FONTS="$(grep -i '^Path = ' "$LIST" | sed 's/^Path = //' | grep -i -E 'efi[/\\]microsoft[/\\]boot[/\\]fonts[/\\].*\.ttf$' || true)"
if [ -n "$FONTS" ]; then
  mkdir -p "$W/fonts"
  echo "$FONTS" | while read -r f; do 7z e -o"$W/fonts" "$ISO" "$f" >/dev/null 2>&1 || true; done
  echo "  yazi tipi: $(ls "$W/fonts" 2>/dev/null | wc -l) dosya"
fi

say "ESP agacina yerlestiriliyor"
mkdir -p "$ESP/EFI/Microsoft/Boot"
cp "$BM"  "$ESP/EFI/Microsoft/Boot/bootmgfw.efi"
cp "$BCD" "$ESP/EFI/Microsoft/Boot/BCD"
if [ -d "$W/fonts" ] && [ -n "$(ls -A "$W/fonts" 2>/dev/null)" ]; then
  mkdir -p "$ESP/EFI/Microsoft/Boot/Fonts"
  cp "$W"/fonts/* "$ESP/EFI/Microsoft/Boot/Fonts/" 2>/dev/null || true
fi

say "Sonuc"
find "$ESP/EFI/Microsoft" -type f -exec ls -lh {} \; | awk '{print "  "$5"\t"$9}'
echo
echo "Toplam: $(du -sh "$ESP" | cut -f1)  (CACHE bolumu 600 MB, imaj 75 MB)"
echo
echo "Sonra:"
echo "  bash cache_esp.sh"
echo "  cache'i flasla, ac, startup.nsh Windows Boot Manager'i baslatacak"
