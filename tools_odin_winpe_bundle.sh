#!/usr/bin/env bash
#
# UFS WinPE turu icin BOOT + SYSTEM + CACHE temizleme imajlarini tek Odin AP
# arsivinde birlestirir.
#
# NEDEN: Bu turda iki farkli katman degisiyor:
#   boot.img   -> UEFI + etkin UFS ACPI tanimi (ham firmware imaji)
#   system.img -> varsayilan: WinPE boot.wim tasiyan FAT ESP
#                 --disk: B2LUFS boot-start WinPE tasiyan NTFS sistem birimi
#   cache.img  -> varsayilan: bos FAT32 CACHE
#                 --disk: bootmgfw + disk BCD tasiyan FAT ESP
#
# Odin, FAT imajlarini ham halde reddedebiliyor. Bu betik SYSTEM ve CACHE
# kaynak imajlarini bizzat img2simg ile sparse'e cevirir; sparse basligini ve
# son tar'dan cikarilan baytlarin hash'ini dogrular. BOOT kasitli olarak ham
# kalir: daha once calisan uefi-boot.tar da boot.img'yi bu bicimde tasiyordu.
#
# Kullanim (yap.sh, tools_b2lfb.sh ve iki cache_esp.sh cagrisindan sonra):
#   bash cache_esp.sh --win --system
#   bash cache_esp.sh --win --bos
#   bash tools_odin_winpe_bundle.sh
# Diskten boot icin:
#   bash tools_odin_winpe_bundle.sh --disk
# Windows Desktop'ta CACHE korunur ve arsive alinmaz:
#   bash tools_odin_winpe_bundle.sh --desktop --target system
#   bash tools_odin_winpe_bundle.sh --desktop --target userdata
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
DISK_MODE=0
DESKTOP_MODE=0
TARGET=system
ARGS=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --disk) DISK_MODE=1; shift ;;
    --desktop) DISK_MODE=1; DESKTOP_MODE=1; shift ;;
    --target)
      [ "$#" -ge 2 ] || { echo "HATA: --target degeri eksik" >&2; exit 1; }
      TARGET="$2"; shift 2 ;;
    --target=*) TARGET="${1#*=}"; shift ;;
    *) ARGS+=("$1"); shift ;;
  esac
done
case "$TARGET" in system|userdata) ;; *) echo "HATA: hedef yalniz system veya userdata olabilir" >&2; exit 1 ;; esac
if [ "$DESKTOP_MODE" = 0 ] && [ "$TARGET" != system ]; then
  echo "HATA: --target userdata yalniz --desktop ile kullanilir" >&2
  exit 1
fi
BOOT_TAR="${ARGS[0]:-$OUT/uefi-boot.tar}"
TARGET_ENTRY="$TARGET.img"
ARCHIVE_TARGET_ENTRY="$TARGET_ENTRY"
if [ "$DESKTOP_MODE" = 1 ] && [ "$TARGET" = userdata ]; then
  # Odin/Samsung firmware paketleri LZ4 frame girdilerini .lz4 uzantisiyla
  # tanir. Bu ayni zamanda ustar'in 8 GiB tek-girdi sinirini asmayi engeller.
  ARCHIVE_TARGET_ENTRY="$TARGET_ENTRY.lz4"
fi
SYSTEM_RAW="${ARGS[1]:-$OUT/$TARGET_ENTRY}"
CACHE_RAW="${ARGS[2]:-$OUT/cache.img}"
NEUTRAL_RAW="${ARGS[2]:-$OUT/system-empty.img}"
if [ "$DESKTOP_MODE" = 1 ]; then
  BUNDLE="$OUT/beyond2lte-windows-desktop-$TARGET.tar"
elif [ "$DISK_MODE" = 1 ]; then
  BUNDLE="$OUT/beyond2lte-winpe-ufs-disk.tar"
else
  BUNDLE="$OUT/beyond2lte-winpe-ufs.tar"
fi

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { echo "HATA: $*" >&2; exit 1; }

[ -s "$BOOT_TAR" ] || fail "BOOT arsivi yok ya da bos: $BOOT_TAR"
[ -s "$SYSTEM_RAW" ] || fail "Windows bolum imaji yok ya da bos: $SYSTEM_RAW"
if [ "$DESKTOP_MODE" = 1 ]; then
  if [ "$TARGET" = userdata ]; then
    [ -s "$NEUTRAL_RAW" ] || fail "SYSTEM notrleyici imaji yok ya da bos: $NEUTRAL_RAW"
  fi
else
  [ -s "$CACHE_RAW" ] || fail "ham CACHE ESP imaji yok ya da bos: $CACHE_RAW"
fi

command -v tar >/dev/null || fail "tar yok"
command -v img2simg >/dev/null || fail "img2simg yok -> sudo apt install -y android-sdk-libsparse-utils"
if [ "$ARCHIVE_TARGET_ENTRY" != "$TARGET_ENTRY" ]; then
  command -v lz4 >/dev/null || fail "buyuk USERDATA Odin paketi icin lz4 gerekli -> sudo apt install -y lz4"
fi

if [ "$DISK_MODE" = 1 ] && [ "$DESKTOP_MODE" = 0 ]; then
  command -v mdir >/dev/null || fail "--disk CACHE ESP dogrulamasi icin mtools/mdir gerekli"
  NTFS_MAGIC="$(dd if="$SYSTEM_RAW" bs=1 skip=3 count=8 status=none)"
  [ "$NTFS_MAGIC" = "NTFS    " ] || fail "--disk SYSTEM imaji NTFS degil"
  MTOOLS_SKIP_CHECK=1 mdir -i "$CACHE_RAW" ::/EFI/Microsoft/Boot/BCD >/dev/null 2>&1 || \
    fail "--disk CACHE imajinda EFI/Microsoft/Boot/BCD yok"
  if MTOOLS_SKIP_CHECK=1 mdir -i "$CACHE_RAW" ::/sources/boot.wim >/dev/null 2>&1; then
    fail "--disk CACHE ESP hala boot.wim tasiyor; RAM disk imaji reddedildi"
  fi
  say "DISK MODU dogrulandi"
  echo "  ${TARGET^^}: NTFS"
  echo "  CACHE: bootmgfw/BCD ESP, boot.wim yok"
fi
if [ "$DESKTOP_MODE" = 1 ]; then
  MARKER="$OUT/windows-desktop-target.txt"
  [ -s "$MARKER" ] || fail "Desktop hedef isareti yok: $MARKER"
  grep -Fqx 'B2L_WINDOWS_DESKTOP=1' "$MARKER" || fail "Desktop hedef isareti gecersiz"
  grep -Fqx "B2L_TARGET=$TARGET" "$MARKER" || fail "Desktop hedef isareti $TARGET ile uyusmuyor"
  grep -Fqx "B2L_IMAGE=$TARGET_ENTRY" "$MARKER" || fail "Desktop imaj adi hedef isaretiyle uyusmuyor"
  NTFS_MAGIC="$(dd if="$SYSTEM_RAW" bs=1 skip=3 count=8 status=none)"
  [ "$NTFS_MAGIC" = "NTFS    " ] || fail "Desktop $TARGET imaji NTFS degil"
  if [ "$TARGET" = userdata ]; then
    grep -Fqx 'B2L_SYSTEM_NEUTRALIZER=system-empty.img' "$MARKER" || fail "SYSTEM notrleyici isareti yok"
    NEUTRAL_MAGIC="$(dd if="$NEUTRAL_RAW" bs=1 skip=3 count=8 status=none)"
    [ "$NEUTRAL_MAGIC" = "NTFS    " ] || fail "SYSTEM notrleyici imaji NTFS degil"
  fi
  say "WINDOWS DESKTOP MODU dogrulandi"
  echo "  Windows birimi: ${TARGET^^} ($TARGET_ENTRY)"
  echo "  CACHE: arsive ALINMIYOR; cihazdaki mevcut ESP korunuyor"
fi

say "Girdiler"
printf '  BOOT arsivi: %s\n' "$BOOT_TAR"
printf '  %-12s %s\n' "${TARGET^^} ham:" "$SYSTEM_RAW"
if [ "$DESKTOP_MODE" = 1 ] && [ "$TARGET" = userdata ]; then
  printf '  SYSTEM bos:  %s\n' "$NEUTRAL_RAW"
elif [ "$DESKTOP_MODE" = 0 ]; then
  printf '  CACHE ham:    %s\n' "$CACHE_RAW"
fi

WORK="$(mktemp -d)"
CHECK="$WORK/check"
trap 'rm -rf "$WORK"' EXIT

only_entry() {
  local ARCH="$1" EXPECT="$2" ACTUAL
  ACTUAL="$(tar -tf "$ARCH" | sed '/^$/d')"
  [ "$ACTUAL" = "$EXPECT" ] || fail "$ARCH icinde yalniz '$EXPECT' bekleniyordu; bulunan: ${ACTUAL:-<bos>}"
}

sparse_magic() {
  od -An -tx4 -N4 "$1" | tr -d '[:space:]'
}

require_sparse() {
  local IMAGE="$1" MAGIC
  MAGIC="$(sparse_magic "$IMAGE")"
  [ "$MAGIC" = "ed26ff3a" ] || fail "sparse basligi gecersiz: $IMAGE (0x${MAGIC:-<bos>})"
  printf '  %s sparse sihirli sayisi: 0x%s\n' "$(basename "$IMAGE")" "$MAGIC"
}

USTAR_MAX=8589934591
require_ustar_size() {
  local IMAGE="$1" SIZE
  SIZE="$(stat -c %s "$IMAGE")"
  [ "$SIZE" -le "$USTAR_MAX" ] || \
    fail "$(basename "$IMAGE") ${SIZE} bayt; Odin ustar tek-girdi siniri ${USTAR_MAX} bayt"
}

require_samsung_lz4() {
  local IMAGE="$1" HEADER
  HEADER="$(od -An -tx1 -N6 "$IMAGE" | tr -d '[:space:]')"
  [ "$HEADER" = "04224d186c60" ] || \
    fail "Samsung/Odin LZ4 basligi gecersiz: $IMAGE (0x${HEADER:-<bos>}; beklenen 0x04224d186c60)"
  printf '  %s Samsung LZ4 basligi: 0x%s\n' "$(basename "$IMAGE")" "$HEADER"
}

only_entry "$BOOT_TAR" boot.img
tar -xf "$BOOT_TAR" -C "$WORK"
[ -s "$WORK/boot.img" ] || fail "BOOT arsivinden boot.img cikarilamadi"

if [ "$DESKTOP_MODE" = 1 ]; then
  say "CACHE haric Desktop bolumleri Android sparse'e cevriliyor"
  img2simg "$SYSTEM_RAW" "$WORK/$TARGET_ENTRY"
  require_sparse "$WORK/$TARGET_ENTRY"
  if [ "$TARGET" = userdata ]; then
    img2simg "$NEUTRAL_RAW" "$WORK/system.img"
    require_sparse "$WORK/system.img"

    say "USERDATA Odin uyumlu LZ4 frame'e sikistiriliyor"
    TARGET_SPARSE_SHA="$(sha256sum "$WORK/$TARGET_ENTRY" | awk '{print $1}')"
    # Samsung download akisi 1 MiB bagimsiz bloklu, content-size tasiyan
    # frame profilini bekler: magic+FLG+BD = 04 22 4d 18 6c 60.
    lz4 -12 -B6 --content-size "$WORK/$TARGET_ENTRY" "$WORK/$ARCHIVE_TARGET_ENTRY"
    [ -s "$WORK/$ARCHIVE_TARGET_ENTRY" ] || fail "LZ4 USERDATA girdisi uretilemedi"
    require_samsung_lz4 "$WORK/$ARCHIVE_TARGET_ENTRY"
    require_ustar_size "$WORK/$ARCHIVE_TARGET_ENTRY"
    LZ4_ROUNDTRIP_SHA="$(lz4 -dc "$WORK/$ARCHIVE_TARGET_ENTRY" | sha256sum | awk '{print $1}')"
    [ "$LZ4_ROUNDTRIP_SHA" = "$TARGET_SPARSE_SHA" ] || fail "LZ4 USERDATA acma hash'i sparse kaynakla uyusmuyor"
    printf '  %-20s %s\n' "sparse SHA-256:" "$TARGET_SPARSE_SHA"
    printf '  %-20s %s bayt\n' "LZ4 boyutu:" "$(stat -c %s "$WORK/$ARCHIVE_TARGET_ENTRY")"
    rm -f "$WORK/$TARGET_ENTRY"
  fi
else
  say "${TARGET^^} ve CACHE Android sparse'e cevriliyor"
  img2simg "$SYSTEM_RAW" "$WORK/$TARGET_ENTRY"
  img2simg "$CACHE_RAW" "$WORK/cache.img"
  require_sparse "$WORK/$TARGET_ENTRY"
  require_sparse "$WORK/cache.img"
fi

say "Tek AP arsivi uretiliyor"
rm -f "$BUNDLE"
require_ustar_size "$WORK/boot.img"
require_ustar_size "$WORK/$ARCHIVE_TARGET_ENTRY"
if [ "$DESKTOP_MODE" = 1 ] && [ "$TARGET" = userdata ]; then
  require_ustar_size "$WORK/system.img"
elif [ "$DESKTOP_MODE" = 0 ]; then
  require_ustar_size "$WORK/cache.img"
fi
(
  cd "$WORK"
  if [ "$DESKTOP_MODE" = 1 ]; then
    if [ "$TARGET" = userdata ]; then
      tar -H ustar -cf "$BUNDLE" boot.img system.img userdata.img.lz4
    else
      tar -H ustar -cf "$BUNDLE" boot.img system.img
    fi
  else
    tar -H ustar -cf "$BUNDLE" boot.img "$TARGET_ENTRY" cache.img
  fi
)

say "Dogrulama"
if [ "$DESKTOP_MODE" = 1 ]; then
  if [ "$TARGET" = userdata ]; then
    EXPECTED=$'boot.img\nsystem.img\nuserdata.img.lz4'
  else
    EXPECTED=$'boot.img\nsystem.img'
  fi
else
  EXPECTED="$(printf 'boot.img\n%s\ncache.img' "$TARGET_ENTRY")"
fi
ACTUAL="$(tar -tf "$BUNDLE" | sed '/^$/d')"
[ "$ACTUAL" = "$EXPECTED" ] || fail "cikti arsiv hedef listesi beklenenden farkli: $ACTUAL"
if [ "$DESKTOP_MODE" = 1 ] && printf '%s\n' "$ACTUAL" | grep -Fqx 'cache.img'; then
  fail "Desktop arsivine cache.img sizdi; CACHE flash'i reddedildi"
fi
mkdir "$CHECK"
tar -xf "$BUNDLE" -C "$CHECK"
FILES=(boot.img "$ARCHIVE_TARGET_ENTRY")
if [ "$DESKTOP_MODE" = 1 ] && [ "$TARGET" = userdata ]; then
  FILES=(boot.img system.img userdata.img.lz4)
elif [ "$DESKTOP_MODE" = 0 ]; then
  FILES+=(cache.img)
fi
for F in "${FILES[@]}"; do
  BEFORE="$(sha256sum "$WORK/$F" | awk '{print $1}')"
  AFTER="$(sha256sum "$CHECK/$F" | awk '{print $1}')"
  [ "$BEFORE" = "$AFTER" ] || fail "tar icinde $F degismis"
  printf '  %-10s hash esit: %s\n' "$F" "$AFTER"
done
if [ "$DESKTOP_MODE" = 1 ] && [ "$TARGET" = userdata ]; then
  require_sparse "$CHECK/system.img"
  require_samsung_lz4 "$CHECK/$ARCHIVE_TARGET_ENTRY"
  LZ4_TAR_ROUNDTRIP_SHA="$(lz4 -dc "$CHECK/$ARCHIVE_TARGET_ENTRY" | sha256sum | awk '{print $1}')"
  [ "$LZ4_TAR_ROUNDTRIP_SHA" = "$TARGET_SPARSE_SHA" ] || fail "tar icindeki LZ4 USERDATA acma hash'i sparse kaynakla uyusmuyor"
  printf '  %-10s acilmis sparse hash esit: %s\n' "$ARCHIVE_TARGET_ENTRY" "$LZ4_TAR_ROUNDTRIP_SHA"
elif [ "$DESKTOP_MODE" = 0 ]; then
  require_sparse "$CHECK/$TARGET_ENTRY"
  require_sparse "$CHECK/cache.img"
else
  require_sparse "$CHECK/$TARGET_ENTRY"
fi
tar -tvf "$BUNDLE"
sha256sum "$BUNDLE"

for DESK in /mnt/c/Users/*/OneDrive/Masa*st* \
            /mnt/c/Users/*/OneDrive/Desktop \
            /mnt/c/Users/*/Masa*st* \
            /mnt/c/Users/*/Desktop; do
  [ -d "$DESK" ] || continue
  mkdir -p "$DESK/beyond2lte-flash"
  cp -f "$BUNDLE" "$DESK/beyond2lte-flash/"
  echo
  echo "Masaustune kopyalandi: $DESK/beyond2lte-flash/$(basename "$BUNDLE")"
  break
done

echo
echo "Hazir: ${BUNDLE#$HERE/}"
echo "  Odin -> AP: bu TEK dosya"
echo "  Re-Partition: KAPALI"
echo
echo "Arsiv girdileri: $(tar -tf "$BUNDLE" | tr '\n' ' ')"
if [ "$DESKTOP_MODE" = 1 ]; then
  echo "Windows Desktop ${TARGET^^} NTFS dogrulandi; cache.img bilerek paketlenmedi."
  if [ "$TARGET" = userdata ]; then
    echo "UYARI: Bu arsiv USERDATA bolumundeki mevcut veriyi siler."
    echo "USERDATA Odin uyumlu LZ4 frame olarak paketlendi; acilmis sparse hash'i dogrulandi."
  fi
elif [ "$DISK_MODE" = 1 ]; then
  echo "SYSTEM NTFS, CACHE disk-BCD ESP ve iki sparse imaj dogrulandi."
else
  echo "SYSTEM ve CACHE'nin Android sparse oldugu dogrulandi."
fi
