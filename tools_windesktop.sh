#!/usr/bin/env bash
#
# Bir ARM64 Windows Desktop install.wim/install.esd indeksini SM-G975F UFS
# bolumunde acilabilir NTFS imajina donusturur. UFS + touch paketleri ayni
# DISM turunda, framebuffer ise boot-start servis olarak offline eklenir.
#
# Kullanim:
#   bash tools_windesktop.sh <ARM64-ISO|install.wim|install.esd> list
#   bash tools_windesktop.sh <ARM64-ISO|install.wim|install.esd> <indeks>
#   bash tools_windesktop.sh <kaynak> <indeks> --target userdata --size-mib 24576
#
# SYSTEM tam 5700 MiB'dir. Varsayilan 5696 MiB imaj bu bolume sigar fakat
# stok Windows Desktop genellikle sigmaz. WSL wimlib System Compression
# uretemez; bu durumda betik gereken boyutu daha NTFS olusturmadan bildirir.
# USERDATA'yi silmeyi bilerek kabul eden kullanici ikinci komutu kullanir.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
TARGET="system"
SIZE_MIB=""
ARGS=()

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\nHATA: %s\n' "$*" >&2; exit 1; }

while [ "$#" -gt 0 ]; do
  case "$1" in
    --target)
      [ "$#" -ge 2 ] || die "--target degeri eksik"
      TARGET="$2"; shift 2 ;;
    --target=*) TARGET="${1#*=}"; shift ;;
    --size-mib)
      [ "$#" -ge 2 ] || die "--size-mib degeri eksik"
      SIZE_MIB="$2"; shift 2 ;;
    --size-mib=*) SIZE_MIB="${1#*=}"; shift ;;
    --help|-h)
      sed -n '2,15p' "$0"; exit 0 ;;
    --*) die "bilinmeyen secenek: $1" ;;
    *) ARGS+=("$1"); shift ;;
  esac
done

[ ${#ARGS[@]} -eq 2 ] || die "kullanim: bash tools_windesktop.sh <ISO|WIM|ESD> <indeks|list> [--target system|userdata] [--size-mib N]"
SOURCE="${ARGS[0]}"
INDEX="${ARGS[1]}"
[ -s "$SOURCE" ] || die "Windows kaynagi yok ya da bos: $SOURCE"
if [ "$INDEX" != list ]; then
  [[ "$INDEX" =~ ^[1-9][0-9]*$ ]] || die "WIM indeksi pozitif tam sayi veya 'list' olmali: $INDEX"
fi
case "$TARGET" in
  system)   : "${SIZE_MIB:=5696}" ;;
  userdata) : "${SIZE_MIB:=24576}" ;;
  *) die "hedef yalniz system veya userdata olabilir" ;;
esac
[[ "$SIZE_MIB" =~ ^[1-9][0-9]*$ ]] || die "--size-mib pozitif tam sayi olmali"
if [ "$TARGET" = system ] && [ "$SIZE_MIB" -gt 5700 ]; then
  die "SYSTEM bolumu tam 5700 MiB; ${SIZE_MIB} MiB imaj sigmaz"
fi
if [ "$TARGET" = userdata ] && [ "$SIZE_MIB" -lt 8192 ]; then
  die "Windows Desktop USERDATA imaji en az 8192 MiB olmali"
fi

for tool in 7z wimlib-imagex; do
  command -v "$tool" >/dev/null || die "$tool yok"
done

mkdir -p "$OUT"
WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

INSTALL="$SOURCE"
if ! wimlib-imagex info "$SOURCE" >/dev/null 2>&1; then
  say "ISO icinde install.wim/install.esd araniyor"
  LIST="$WORK/iso-list.txt"
  7z l -slt "$SOURCE" > "$LIST"
  INSTALL_PATH="$(grep -i '^Path = ' "$LIST" | sed 's/^Path = //' | grep -i -m1 -E '(^|[/\\])sources[/\\]install\.(wim|esd)$' || true)"
  [ -n "$INSTALL_PATH" ] || die "ISO'da sources/install.wim veya install.esd yok"
  7z e -y -o"$WORK/source" "$SOURCE" "$INSTALL_PATH" >/dev/null
  INSTALL="$WORK/source/$(basename "${INSTALL_PATH//\\//}")"
  [ -s "$INSTALL" ] || die "ISO'dan install goruntusu cikarilamadi"
fi

if [ "$INDEX" = list ]; then
  say "Windows edition indeksleri"
  wimlib-imagex info "$INSTALL"
  exit 0
fi

for tool in mkntfs powershell.exe wslpath; do
  command -v "$tool" >/dev/null || die "$tool yok"
done
INFO="$WORK/image-info.txt"
wimlib-imagex info "$INSTALL" "$INDEX" > "$INFO" || die "WIM indeksi okunamadi: $INDEX"
if ! grep -Eqi '^Architecture:[[:space:]]*(arm64|9)[[:space:]]*$' "$INFO"; then
  sed -n '1,35p' "$INFO"
  die "secilen Windows goruntusu ARM64 degil"
fi
NAME="$(sed -n 's/^Name:[[:space:]]*//p' "$INFO" | head -n1)"
TOTAL_BYTES="$(sed -n 's/^Total Bytes:[[:space:]]*//p' "$INFO" | head -n1 | tr -cd '0-9')"
say "Windows Desktop kaynagi"
echo "  indeks : $INDEX"
echo "  ad     : ${NAME:-<okunamadi>}"
echo "  hedef  : ${TARGET^^}.img (${SIZE_MIB} MiB)"
if [ -n "$TOTAL_BYTES" ]; then
  TOTAL_MIB=$(( (TOTAL_BYTES + 1048575) / 1048576 ))
  USABLE_MIB=$(( SIZE_MIB - 512 ))
  echo "  acilmis: yaklasik ${TOTAL_MIB} MiB (+ NTFS/OOBE payi)"
  if [ "$TOTAL_MIB" -gt "$USABLE_MIB" ]; then
    NEEDED_MIB=$(( TOTAL_MIB + 1024 ))
    if [ "$TARGET" = system ]; then
      die "edition SYSTEM'e sigmaz (${TOTAL_MIB} MiB > ${USABLE_MIB} MiB). USERDATA kabul ediliyorsa: --target userdata --size-mib $NEEDED_MIB"
    fi
    die "USERDATA imaji kucuk; --size-mib en az $NEEDED_MIB kullan"
  fi
fi

for required in \
  "$OUT/b2lufs/b2lufs.inf" "$OUT/b2lufs/b2lufs.sys" "$OUT/b2lufs/b2lufs.cat" \
  "$OUT/b2ltouch/b2ltouch.inf" "$OUT/b2ltouch/b2ltouch.sys" "$OUT/b2ltouch/b2ltouch.cat" \
  "$OUT/b2lfb/b2lfb.sys"; do
  [ -s "$required" ] || die "hazir surucu paketi yok: ${required#$HERE/}"
done

DESKTOP_WIM="$WORK/desktop.wim"
say "Secilen edition tek indeksli WIM'e aktariliyor"
wimlib-imagex export "$INSTALL" "$INDEX" "$DESKTOP_WIM" --compress=LZX --rebuild
wimlib-imagex info "$DESKTOP_WIM" 1 >/dev/null || die "tek indeksli Desktop WIM dogrulanamadi"

say "UFS + touch + framebuffer offline servisleniyor"
powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$(wslpath -w "$HERE/windows/stage-winpe-driverstore.ps1")" \
  -ProjectRoot "$(wslpath -w "$HERE")" \
  -WimPath "$(wslpath -w "$DESKTOP_WIM")" \
  -Package TouchUfs -DisableUsb -IncludeFramebuffer -PrepareDesktopOobe || \
  die "Windows Desktop offline driver servislemesi basarisiz"

VERIFY="$WORK/verify"
mkdir -p "$VERIFY/ufs" "$VERIFY/desktop" "$VERIFY/oobe"
wimlib-imagex extract "$DESKTOP_WIM" 1 /Windows/B2LUFS/offline-driverstore.txt \
  --dest-dir="$VERIFY/ufs" --no-acls >/dev/null
wimlib-imagex extract "$DESKTOP_WIM" 1 /Windows/B2LDESKTOP/offline-drivers.txt \
  --dest-dir="$VERIFY/desktop" --no-acls >/dev/null
wimlib-imagex extract "$DESKTOP_WIM" 1 /Windows/B2LDESKTOP/oobe-state.txt \
  --dest-dir="$VERIFY/oobe" --no-acls >/dev/null
for expected in \
  B2L_OFFLINE_PACKAGE=TOUCHUFS B2L_BOOT_START=1 \
  'B2L_LOAD_ORDER_GROUP=SCSI Miniport' B2L_USB_TESTS=DISABLED; do
  tr -d '\r' < "$VERIFY/ufs/offline-driverstore.txt" | grep -Fqx "$expected" || \
    die "Desktop WIM UFS isareti eksik: $expected"
done
for expected in B2L_DESKTOP_STAGE=1 B2L_FRAMEBUFFER_BOOT_START=1; do
  tr -d '\r' < "$VERIFY/desktop/offline-drivers.txt" | grep -Fqx "$expected" || \
    die "Desktop WIM framebuffer isareti eksik: $expected"
done
for expected in \
  B2L_DESKTOP_OOBE_REPAIR=1 B2L_CHILD_COMPLETION_SETUP=3 \
  B2L_OOBE_UNATTEND=1 B2L_OOBE_LOCAL_USER=b2l B2L_OOBE_AUTOLOGON_COUNT=1 \
  B2L_OOBE_CREDENTIAL_CLEANUP=1 B2L_OOBE_TABTIP_QUARANTINE=1 \
  B2L_OOBE_REBOOT_REQUIRED=0; do
  tr -d '\r' < "$VERIFY/oobe/oobe-state.txt" | grep -Fqx "$expected" || \
    die "Desktop WIM OOBE isareti eksik: $expected"
done

IMAGE="$OUT/$TARGET.img"
say "NTFS Windows birimi olusturuluyor"
rm -f "$IMAGE"
truncate -s "${SIZE_MIB}M" "$IMAGE"
mkntfs -Q -F -s 4096 -c 4096 -L WINDOWS "$IMAGE" 2>&1 | \
  grep -vi "not a block device\|has been set to 0\|was not specified\|will not be able to boot\|forced anyway" || true

say "Windows Desktop NTFS'e aciliyor"
echo "  Kaynak edition'a gore uzun surebilir."
if ! wimlib-imagex apply "$DESKTOP_WIM" 1 "$IMAGE"; then
  rm -f "$IMAGE"
  if [ "$TARGET" = system ]; then
    die "Windows 5696 MiB SYSTEM'e sigmadi. USERDATA silmeyi kabul ediyorsan --target userdata kullan"
  fi
  die "Windows Desktop USERDATA imajina uygulanamadi"
fi

say "Uygulanan Desktop dosyalari dogrulaniyor"
if command -v ntfscat >/dev/null; then
  for file in \
    /Windows/System32/winload.efi \
    /Windows/System32/ntoskrnl.exe \
    /Windows/explorer.exe; do
    # ntfsls varsayilan olarak hidden/system dosyalarini gostermiyor;
    # winload.efi bu nedenle basarili apply sonrasinda yanlislikla YOK
    # sayilmisti. ntfscat dosyanin gercek veri akisini dogrudan okur.
    if ntfscat "$IMAGE" "$file" >/dev/null 2>&1; then
      echo "  + $file"
    else
      die "uygulanan Desktop imajinda dosya okunamadi: $file (imaj korundu: $IMAGE)"
    fi
  done
elif command -v ntfsls >/dev/null; then
  # -a hidden/system dosyalarini da listeler. Bu geri donus yolu yalniz
  # isim dogrular; ntfscat kadar guclu degildir ama yanlis negatif uretmez.
  ntfsls -a -p /Windows/System32 "$IMAGE" 2>/dev/null | grep -Fqi 'winload.efi' || \
    die "winload.efi NTFS listesinde yok (imaj korundu: $IMAGE)"
  ntfsls -a -p /Windows/System32 "$IMAGE" 2>/dev/null | grep -Fqi 'ntoskrnl.exe' || \
    die "ntoskrnl.exe NTFS listesinde yok (imaj korundu: $IMAGE)"
  ntfsls -a -p /Windows "$IMAGE" 2>/dev/null | grep -Fqi 'explorer.exe' || \
    die "explorer.exe NTFS listesinde yok (imaj korundu: $IMAGE)"
  echo "  + winload.efi, ntoskrnl.exe, explorer.exe (-a listesi)"
else
  echo "  UYARI: ntfscat/ntfsls yok; uygulanmis dosya dogrulamasi atlandi"
fi

MARKER="$OUT/windows-desktop-target.txt"
{
  echo "B2L_WINDOWS_DESKTOP=1"
  echo "B2L_TARGET=$TARGET"
  echo "B2L_SIZE_MIB=$SIZE_MIB"
  echo "B2L_IMAGE=$(basename "$IMAGE")"
  echo "B2L_EDITION_INDEX=$INDEX"
  echo "B2L_EDITION_NAME=$NAME"
  echo "B2L_COMPACT=NONE"
  echo "B2L_OOBE_CHILD_COMPLETION=3"
  echo "B2L_OOBE_UNATTEND=1"
  echo "B2L_OOBE_LOCAL_USER=b2l"
  echo "B2L_OOBE_CREDENTIAL_CLEANUP=1"
  echo "B2L_OOBE_TABTIP_QUARANTINE=1"
  echo "B2L_OOBE_REBOOT_REQUIRED=0"
} > "$MARKER"

if [ "$TARGET" = userdata ]; then
  # CACHE'teki BCD locate= ile birimleri tararken eski SYSTEM WinPE'yi
  # Desktop sanmasin. SYSTEM'i Windows dizini bulunmayan kucuk bir NTFS ile
  # notrleriz; CACHE'e dokunulmaz.
  NEUTRAL="$OUT/system-empty.img"
  say "Eski SYSTEM WinPE icin bos NTFS notrleyici olusturuluyor"
  rm -f "$NEUTRAL"
  truncate -s 64M "$NEUTRAL"
  mkntfs -Q -F -s 4096 -c 4096 -L B2LNOOS "$NEUTRAL" 2>&1 | \
    grep -vi "not a block device\|has been set to 0\|was not specified\|will not be able to boot\|forced anyway" || true
  echo "B2L_SYSTEM_NEUTRALIZER=$(basename "$NEUTRAL")" >> "$MARKER"
fi

say "Windows Desktop imaji hazir"
ls -lh "$IMAGE" "$MARKER"
cat <<EOF

SONRAKI ADIMLAR:
  1) Mevcut disk-WinPE'yi bir kez ac ve CACHE BCD'sini yerinde Desktop'a cevir
     (DEVIR.md "CACHE'e flash atmadan Desktop gecisi" bolumu).
  2) WinPE'yi kapat; yeniden baslatmadan Odin'e gec.
  3) bash tools_odin_winpe_bundle.sh --desktop --target $TARGET

Desktop Odin arsivinde cache.img YOKTUR; CACHE partitionina flash atilmaz.
DIKKAT: --target userdata secildi ise USERDATA ve eski WinPE SYSTEM silinir.
EOF
