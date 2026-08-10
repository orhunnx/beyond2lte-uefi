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

say "Boot Manager ve BCD cikariliyor"
# ISO'daki efi/boot/bootaa64.efi = Windows Boot Manager (bootmgfw ile ayni ikili)
7z e -o"$W" "$ISO" efi/boot/bootaa64.efi         >/dev/null
7z e -o"$W" "$ISO" efi/microsoft/boot/BCD        >/dev/null 2>&1 || true
# Yazi tipleri: hata ekranini duzgun cizebilmesi icin
7z e -o"$W/fonts" "$ISO" "efi/microsoft/boot/fonts/*" >/dev/null 2>&1 || true

ls -l "$W" | sed 's/^/  /'

say "ESP agacina yerlestiriliyor"
mkdir -p "$ESP/EFI/Microsoft/Boot"
cp "$W/bootaa64.efi" "$ESP/EFI/Microsoft/Boot/bootmgfw.efi"
[ -f "$W/BCD" ] && cp "$W/BCD" "$ESP/EFI/Microsoft/Boot/BCD"
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
