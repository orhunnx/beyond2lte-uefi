#!/usr/bin/env bash
#
# Daha once uretilmis tam Windows NTFS imajinda ilk-acilis Setup döngüsünü
# düzeltir. Büyük install.wim'i yeniden uygulamaz; offline SYSTEM hive içindeki
# ChildCompletion/UnattendFile değerlerini ve klavyesiz ARM64 oobeSystem cevap
# dosyasını yazar. OOBE sırasında çöken TabTip.exe'yi başlatan metin-giriş
# servisini geçici olarak karantinaya alır. Dokunmatik HID sürücüsüne dokunmaz
# ve Windows restart desteğine dayanmaz.
#
# Kullanim:
#   bash tools_windesktop_oobe_repair.sh
#   bash tools_windesktop_oobe_repair.sh out/userdata.img

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"
IMAGE="${1:-$OUT/userdata.img}"
MARKER="$OUT/windows-desktop-target.txt"
UNATTEND="$HERE/windows/desktop-unattend.xml"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { echo "HATA: $*" >&2; exit 1; }

[ -s "$IMAGE" ] || fail "Desktop NTFS imaji yok ya da bos: $IMAGE"
[ -s "$UNATTEND" ] || fail "Desktop OOBE answer file yok: $UNATTEND"
grep -Fq 'B2L remove temporary OOBE credentials' "$UNATTEND" || \
  fail "Desktop OOBE answer file gecici parola temizligini icermiyor"
[ -s "$MARKER" ] || fail "Desktop hedef isareti yok: $MARKER"
grep -Fqx 'B2L_WINDOWS_DESKTOP=1' "$MARKER" || fail "Desktop hedef isareti gecersiz"
grep -Fqx "B2L_IMAGE=$(basename "$IMAGE")" "$MARKER" || \
  fail "hedef isareti bu imaja ait degil: $(basename "$IMAGE")"

for tool in ntfscat ntfscp hivexget hivexregedit; do
  command -v "$tool" >/dev/null || \
    fail "$tool yok -> sudo apt install -y ntfs-3g libhivex-bin"
done

NTFS_MAGIC="$(dd if="$IMAGE" bs=1 skip=3 count=8 status=none)"
[ "$NTFS_MAGIC" = "NTFS    " ] || fail "Desktop imaji NTFS degil: $IMAGE"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HIVE="$WORK/SYSTEM"
VERIFY_HIVE="$WORK/SYSTEM.verify"
VERIFY_UNATTEND="$WORK/Unattend.verify.xml"
REG="$WORK/oobe.reg"
KEY='Setup\Status\ChildCompletion'
PREFIX='HKEY_LOCAL_MACHINE\SYSTEM'
UNATTEND_WIN='C:\Windows\System32\Sysprep\Unattend.xml'

say "Offline SYSTEM hive cikariliyor"
ntfscat "$IMAGE" /Windows/System32/config/SYSTEM > "$HIVE" || \
  fail "SYSTEM hive NTFS imajindan okunamadi"
[ -s "$HIVE" ] || fail "cikarilan SYSTEM hive bos"

BACKUP="$OUT/$(basename "$IMAGE").SYSTEM-before-oobe"
if [ ! -e "$BACKUP" ]; then
  cp --reflink=auto "$HIVE" "$BACKUP"
  echo "  geri donus hive'i: ${BACKUP#$HERE/}"
fi

BEFORE="$(hivexget "$HIVE" "$KEY" setup.exe 2>/dev/null | tr -d '[:space:]' || true)"
printf '%s\n\n[%s\\Setup]\n"UnattendFile"="C:\\\\Windows\\\\System32\\\\Sysprep\\\\Unattend.xml"\n\n[%s\\Setup\\Status\\ChildCompletion]\n"setup.exe"=dword:00000003\n' \
  'Windows Registry Editor Version 5.00' "$PREFIX" "$PREFIX" > "$REG"

# Build'e göre görülen servis adı TabletInputService veya
# TextInputManagementService olabilir. Yalnız etkin/varsayılan control set'lerde
# gerçekten bulunan anahtarı değiştir; böylece varsayıma dayalı bir kayıt
# oluşturmayız. Start=4 yalnız TabTip/metin-giriş sürecini ilk açılışta
# karantinaya alır; b2ltouch/VHF HID yığını bağımsız kalır.
CONTROL_SETS=()
for select_name in Current Default LastKnownGood; do
  select_value="$(hivexget "$HIVE" Select "$select_name" 2>/dev/null | tr -d '[:space:]' || true)"
  [ -n "$select_value" ] || continue
  case "$select_value" in
    0x*|0X*|[0-9]*) control_set="$(printf 'ControlSet%03d' "$((select_value))")" ;;
    *) fail "SYSTEM Select\\$select_name sayisal degil: $select_value" ;;
  esac
  already=0
  for known_control_set in "${CONTROL_SETS[@]:-}"; do
    [ "$known_control_set" = "$control_set" ] && already=1
  done
  [ "$already" = 1 ] || CONTROL_SETS+=("$control_set")
done
[ "${#CONTROL_SETS[@]}" -gt 0 ] || fail "SYSTEM etkin control set icermiyor"

TABTIP_KEYS=()
TABTIP_BEFORE=()
TABTIP_SERVICE_NAMES=()
for control_set in "${CONTROL_SETS[@]}"; do
  for service_name in TabletInputService TextInputManagementService; do
    service_key="$control_set\\Services\\$service_name"
    service_start="$(hivexget "$HIVE" "$service_key" Start 2>/dev/null | tr -d '[:space:]' || true)"
    [ -n "$service_start" ] || continue
    TABTIP_KEYS+=("$service_key")
    TABTIP_BEFORE+=("$service_start")
    printf '\n[%s\\%s]\n"Start"=dword:00000004\n' "$PREFIX" "$service_key" >> "$REG"
    if [[ " ${TABTIP_SERVICE_NAMES[*]:-} " != *" $service_name "* ]]; then
      TABTIP_SERVICE_NAMES+=("$service_name")
    fi
  done
done
[ "${#TABTIP_KEYS[@]}" -gt 0 ] || \
  fail "TabTip'i baslatan TabletInputService/TextInputManagementService bulunamadi"

say "ChildCompletion ve ilk-acilis TabTip karantinasi yaziliyor"
hivexregedit --merge --prefix "$PREFIX" "$HIVE" "$REG"
AFTER="$(hivexget "$HIVE" "$KEY" setup.exe | tr -d '[:space:]')"
case "$AFTER" in 3|0x3|0X3|00000003) ;; *) fail "gecici hive geri okuma basarisiz: ${AFTER:-<bos>}" ;; esac
UNATTEND_AFTER="$(hivexget "$HIVE" Setup UnattendFile | tr -d '\r\n')"
[ "$UNATTEND_AFTER" = "$UNATTEND_WIN" ] || \
  fail "gecici hive UnattendFile geri okuma basarisiz: ${UNATTEND_AFTER:-<bos>}"
echo "  once: ${BEFORE:-<yok>}"
echo "  sonra: $AFTER"
for index in "${!TABTIP_KEYS[@]}"; do
  tabtip_after="$(hivexget "$HIVE" "${TABTIP_KEYS[$index]}" Start | tr -d '[:space:]')"
  case "$tabtip_after" in 4|0x4|0X4|00000004) ;;
    *) fail "TabTip servis karantinasi geri okunamadi: ${TABTIP_KEYS[$index]}=${tabtip_after:-<bos>}" ;;
  esac
  echo "  TabTip: ${TABTIP_KEYS[$index]} Start ${TABTIP_BEFORE[$index]} -> 4"
done

say "Onarilan hive ve klavyesiz OOBE answer file NTFS imajina yaziliyor"
ntfscp -f "$IMAGE" "$HIVE" /Windows/System32/config/SYSTEM || \
  fail "onarilan SYSTEM hive NTFS imajina yazilamadi"
ntfscp -f "$IMAGE" "$UNATTEND" /Windows/System32/Sysprep/Unattend.xml || \
  fail "Desktop OOBE answer file NTFS imajina yazilamadi"
ntfscat "$IMAGE" /Windows/System32/config/SYSTEM > "$VERIFY_HIVE" || \
  fail "yazilan SYSTEM hive yeniden okunamadi"
ON_IMAGE="$(hivexget "$VERIFY_HIVE" "$KEY" setup.exe | tr -d '[:space:]')"
case "$ON_IMAGE" in 3|0x3|0X3|00000003) ;; *) fail "imaj ici ChildCompletion dogrulanamadi: ${ON_IMAGE:-<bos>}" ;; esac
ON_IMAGE_UNATTEND="$(hivexget "$VERIFY_HIVE" Setup UnattendFile | tr -d '\r\n')"
[ "$ON_IMAGE_UNATTEND" = "$UNATTEND_WIN" ] || \
  fail "imaj ici UnattendFile dogrulanamadi: ${ON_IMAGE_UNATTEND:-<bos>}"
for service_key in "${TABTIP_KEYS[@]}"; do
  on_image_tabtip="$(hivexget "$VERIFY_HIVE" "$service_key" Start | tr -d '[:space:]')"
  case "$on_image_tabtip" in 4|0x4|0X4|00000004) ;;
    *) fail "imaj ici TabTip servis karantinasi dogrulanamadi: $service_key=${on_image_tabtip:-<bos>}" ;;
  esac
done
ntfscat "$IMAGE" /Windows/System32/Sysprep/Unattend.xml > "$VERIFY_UNATTEND" || \
  fail "imaj ici OOBE answer file yeniden okunamadi"
cmp -s "$UNATTEND" "$VERIFY_UNATTEND" || fail "imaj ici OOBE answer file kaynakla ayni degil"

MARKER_TMP="$WORK/windows-desktop-target.txt"
grep -Fv -e 'B2L_OOBE_CHILD_COMPLETION=3' \
  -e 'B2L_OOBE_UNATTEND=1' -e 'B2L_OOBE_LOCAL_USER=b2l' \
  -e 'B2L_OOBE_CREDENTIAL_CLEANUP=1' -e 'B2L_OOBE_REBOOT_REQUIRED=0' \
  -e 'B2L_OOBE_TABTIP_QUARANTINE=1' -e 'B2L_OOBE_TABTIP_SERVICES=' \
  "$MARKER" > "$MARKER_TMP" || true
printf '%s\n' 'B2L_OOBE_CHILD_COMPLETION=3' >> "$MARKER_TMP"
printf '%s\n' 'B2L_OOBE_UNATTEND=1' 'B2L_OOBE_LOCAL_USER=b2l' \
  'B2L_OOBE_CREDENTIAL_CLEANUP=1' 'B2L_OOBE_REBOOT_REQUIRED=0' >> "$MARKER_TMP"
TABTIP_SERVICES_CSV="$(IFS=,; echo "${TABTIP_SERVICE_NAMES[*]}")"
printf '%s\n' 'B2L_OOBE_TABTIP_QUARANTINE=1' \
  "B2L_OOBE_TABTIP_SERVICES=$TABTIP_SERVICES_CSV" >> "$MARKER_TMP"
mv "$MARKER_TMP" "$MARKER"

say "Desktop ilk-acilis onarimi hazir"
echo "  imaj: $IMAGE"
echo "  dogrulanan: HKLM\\SYSTEM\\$KEY\\setup.exe=3"
echo "  answer file: $UNATTEND_WIN"
echo "  ilk oturum: b2l / beyond2lte (tek sefer otomatik giris)"
echo "  TabTip karantinasi: $TABTIP_SERVICES_CSV (dokunmatik HID etkin)"
echo "  Windows yeniden baslatmasi: GEREKMIYOR"
echo
echo "Simdi yeniden paketle ve Odin AP ile flashla:"
echo "  bash tools_odin_winpe_bundle.sh --desktop --target $(sed -n 's/^B2L_TARGET=//p' "$MARKER")"
