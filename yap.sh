#!/usr/bin/env bash
#
# beyond2lte UEFI — tek komutluk derleme.
#
# WSL'de calistir. Kendi konumunu bulur, hicbir yol elle yazilmaz:
#   bash /mnt/c/.../UEFI_beyond2lte/yap.sh
#
# Kullanim:
#   yap.sh                 DEBUG derle
#   yap.sh -r RELEASE      RELEASE derle
#   yap.sh --acpi          ACPI tablolarini derle + etkinlestir
#   (Odin .tar ve .img her zaman uretilir)
#   yap.sh --clean         temiz derleme (-c)
#
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MU="${MU_SILICIUM:-$HOME/Mu-Silicium}"
REL="DEBUG"; CLEAN=""; DO_ACPI=0; DO_TAR=0

while [ $# -gt 0 ]; do
  case "$1" in
    -r|--release) REL="$2"; shift 2 ;;
    --acpi)  DO_ACPI=1; shift ;;
    --tar)   shift ;;   # geriye donuk uyumluluk, artik varsayilan
    --clean) CLEAN="-c"; shift ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "Bilinmeyen secenek: $1"; exit 1 ;;
  esac
done

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -d "$MU" ] || { echo "Mu-Silicium bulunamadi: $MU"; echo "MU_SILICIUM=... ile yol verebilirsin."; exit 1; }

say "Proje: $PROJ"
say "Mu-Silicium: $MU"

# ---------------------------------------------------------------- ACPI
if [ "$DO_ACPI" = 1 ]; then
  say "ACPI tablolari derleniyor"
  command -v iasl >/dev/null || { echo "iasl yok -> sudo apt install -y acpica-tools"; exit 1; }
  cp -r "$PROJ/silicium-acpi-overlay/SoCs" "$MU/Silicium-ACPI/"
  ( cd "$MU/Silicium-ACPI/SoCs/Samsung/S5E9820" && for f in *.dsl; do iasl -ve "$f"; done )
  # ACPI.inc'i ac
  sed -i 's|^#\(FILE FREEFORM = 7E374E25\)|\1|; s|^#\(    SECTION \)|\1|; s|^#\(}\)|\1|' \
    "$PROJ/beyond2ltePkg/Include/ACPI.inc"
  cp "$PROJ/beyond2ltePkg/Include/ACPI.inc" \
     "$PROJ/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Include/ACPI.inc"
  echo "ACPI etkinlestirildi."
fi

# ---------------------------------------------------------------- overlay
say "Overlay kopyalaniyor"
cp -r "$PROJ/mu-silicium-overlay/." "$MU/"
echo "  Platforms/Samsung/beyond2ltePkg"
echo "  Silicon/Samsung/S5E9820Pkg"
echo "  Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.inf  (upstream yamasi)"
echo "  Resources/{Configs,DTBs}/beyond2lte.*"

# ---------------------------------------------------------------- derle
say "Derleniyor ($REL)"
cd "$MU"
[ -d .venv ] && source .venv/bin/activate || true
python3 build_uefi.py -d beyond2lte -r "$REL" $CLEAN

# ---------------------------------------------------------------- rapor
IMG="$MU/Mu-beyond2lte.img"
say "Sonuc"
if [ -f "$IMG" ]; then
  ls -lh "$IMG"
  RPT="$MU/Build/beyond2ltePkg/${REL}_CLANGPDB/BUILD_REPORT.TXT"
  if [ -f "$RPT" ]; then
    echo "--- FV kullanimi:"
    grep -A6 -i "FV Space Information" "$RPT" | head -12 || true
    echo "--- Shell FV'de mi:"
    if grep -qi '7C04A583' "$RPT"; then echo "  EVET (7C04A583 bulundu)"; else echo "  HAYIR"; fi
  fi
  # Odin tar'ini her zaman uret - flash icin gereken bu.
  ( cd "$MU" && cp -f Mu-beyond2lte.img boot.img \
      && tar -H ustar -cf uefi-boot.tar boot.img && rm -f boot.img )

  # Masaustunu bul. OneDrive yonlendirmesi ve Turkce klasor adi oncelikli
  # (ORTAM.md: C:\Users\orhun\OneDrive\Masaustu).
  DESK=""
  if [ -n "${OUTDIR:-}" ]; then
    DESK="$OUTDIR"
  else
    for p in /mnt/c/Users/*/OneDrive/Masa*st* \
             /mnt/c/Users/*/OneDrive/Desktop \
             /mnt/c/Users/*/Masa*st* \
             /mnt/c/Users/*/Desktop; do
      [ -d "$p" ] && { DESK="$p"; break; }
    done
  fi

  if [ -n "$DESK" ]; then
    mkdir -p "$DESK/beyond2lte-flash"
    cp -f "$IMG" "$DESK/beyond2lte-flash/"
    cp -f "$MU/uefi-boot.tar" "$DESK/beyond2lte-flash/"
    echo "Flash dosyalari: $DESK/beyond2lte-flash"
    echo "  uefi-boot.tar        -> Odin, AP slotu  (Re-Partition ISARETSIZ)"
    echo "  Mu-beyond2lte.img    -> heimdall flash --BOOT Mu-beyond2lte.img"
  else
    echo "Masaustu bulunamadi. OUTDIR=/mnt/c/... ile yol verebilirsin."
    echo "  $IMG"
    echo "  $MU/uefi-boot.tar"
  fi
else
  echo "Cikti uretilmedi."; exit 1
fi
