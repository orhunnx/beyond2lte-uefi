#!/usr/bin/env bash
#
# CACHE bolumu icin ESP (FAT32) imaji uretir ve Odin tar'ina paketler.
#
#   bash cache_esp.sh [iceri-konacak-dosya.efi]
#
# Argüman verilmezse Mu-Silicium build'indeki Shell.efi aranir.
#
# IKI KISIT VAR, ikisi de zorunlu:
#
# 1) -S 4096 : UFS'in mantiksal blok boyutu 4096 (log: "BlockSize : 4096").
#    FAT'in BPB'sindeki sektor boyutu BlockIo'nunkiyle esitlesmeli.
#
# 2) -F 16   : FAT32 DEGIL. FatPkg/EnhancedFatDxe/Init.c:
#         if (Volume->MaxCluster < FAT_MAX_FAT16_CLUSTER) return EFI_VOLUME_CORRUPTED;
#    FAT_MAX_FAT16_CLUSTER = 65525. CACHE 75 MB; 4K sektorle 19200 cluster
#    cikiyor, esigin altinda -> FAT32 sessizce reddediliyor.
#    4K sektorle FAT32 icin en az 256 MB lazim. FAT16'nin gecerli araligi
#    4085-65525, 19200 tam ortasinda.
#
set -euo pipefail
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MU="${MU_SILICIUM:-$HOME/Mu-Silicium}"
OUT="$PROJ/out"; mkdir -p "$OUT"

APP="${1:-}"
if [ -z "$APP" ]; then
  APP="$(find "$MU/Build" -name 'Shell.efi' -path '*ShellPkg*' 2>/dev/null | head -1)"
  [ -n "$APP" ] || APP="$(find "$MU/Build" -maxdepth 4 -name 'Shell.efi' 2>/dev/null | head -1)"
fi
[ -f "$APP" ] || { echo "EFI uygulamasi bulunamadi. Yolu argüman olarak ver."; exit 1; }
echo "Icerik: $APP"

# CACHE = 153600 blok x 512 = 78643200 bayt (PARTITIONS.md)
SZ=78643200
IMG="$OUT/cache.img"
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count=$((SZ/1048576)) status=none

# 4096 baytlik sektor - UFS BlockSize ile eslesmek zorunda
mkfs.vfat -F 16 -S 4096 -n UEFIESP "$IMG" >/dev/null

# mtools ile mount'suz yaz (sudo gerekmez)
if command -v mcopy >/dev/null; then
  export MTOOLS_SKIP_CHECK=1
  mmd   -i "$IMG" ::/EFI ::/EFI/BOOT
  mcopy -i "$IMG" "$APP" ::/EFI/BOOT/BOOTAA64.EFI
  echo "--- imaj icerigi:"; mdir -i "$IMG" ::/EFI/BOOT
else
  echo "mtools yok (sudo apt install -y mtools). Loop mount'a dusuluyor..."
  T=$(mktemp -d); sudo mount -o loop "$IMG" "$T"
  sudo mkdir -p "$T/EFI/BOOT"; sudo cp "$APP" "$T/EFI/BOOT/BOOTAA64.EFI"
  sudo umount "$T"; rmdir "$T"
fi

( cd "$OUT" && tar -H ustar -cf cache-esp.tar cache.img )

for p in /mnt/c/Users/*/OneDrive/Masa*st* /mnt/c/Users/*/OneDrive/Desktop \
         /mnt/c/Users/*/Masa*st* /mnt/c/Users/*/Desktop; do
  [ -d "$p" ] || continue
  mkdir -p "$p/beyond2lte-flash"
  cp -f "$OUT/cache-esp.tar" "$p/beyond2lte-flash/"
  echo "Kopyalandi: $p/beyond2lte-flash/cache-esp.tar  (Odin -> AP)"
  break
done
