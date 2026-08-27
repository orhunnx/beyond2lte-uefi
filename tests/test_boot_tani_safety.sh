#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LBootTani/B2LBootTani.c"
NSH="$ROOT/esp/startup-boot-tani.nsh"

forbidden='(Mmio(Read|Write)|CF0[0-9A-Fa-f]{5}|GICD|PMU_RST|TARA_KAYIT|SEH_(CANLI|ONCEKI))'
if grep -En "$forbidden" "$APP"; then
  echo "HATA: boot tani yolunda sabit fiziksel adres veya eski tani erisimi var."
  exit 1
fi
if grep -En '^[[:space:]]*(dmem|[^#]*B2LGOSTR)' "$NSH"; then
  echo "HATA: boot tani startup yolu eski fiziksel taniyi calistiriyor."
  exit 1
fi

grep -Fq 'FileDevicePath (Kendi->DeviceHandle, BOOT_YOLU)' "$APP"
grep -Fq 'gEfiEventExitBootServicesGuid' "$APP"
grep -Fq 'Status = mAsilExitBootServices (ImageHandle, MapKey);' "$APP"
grep -Fq '0x0040FF40U' "$APP"
grep -Fq 'TimerPeriodic' "$APP"
grep -Fq 'KORUMALI BELLEK: ERISIM YOK' "$APP"
grep -Fq -- '--no-touch) NO_TOUCH=1' "$ROOT/yap.sh"
grep -Fq 'B2LTouchDxe: KONTROL DERLEMESINDE KAPALI' "$ROOT/yap.sh"
grep -Fq 'uefi-boot-no-touch.tar' "$ROOT/yap.sh"

bash -n "$ROOT/cache_esp.sh" "$ROOT/yap.sh"
echo "B2LBootTani guvenlik sozlesmesi: TAMAM"
