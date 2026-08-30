#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.c"
INF="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.inf"
DSC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
MAP="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib.c"

grep -q 'BASE_NAME[[:space:]]*= B2LUsbTest' "$INF"
grep -q 'Applications/B2LUsbTest/B2LUsbTest.inf' "$DSC"
grep -q -- '--usb-test' "$ROOT/yap.sh"
grep -q 'B2LUSB.EFI' "$ROOT/esp/startup.nsh"
grep -q 'B2LUSB.EFI' "$ROOT/esp/startup-baseline.nsh"
grep -q 'beyond2lte-usb-diag.tar' "$ROOT/cache_esp.sh"
grep -q 'DWC3_IP_DWC31[[:space:]]*0x33310000U' "$APP"
grep -q 'DWC3_VER_NUMBER[[:space:]]*0xC1A0' "$APP"
grep -q 'B2L_USB_TEST_REVISION[[:space:]]*7' "$APP"
grep -q 'B2L_USB_PCS_ABORT_OFFSET_0[[:space:]]*0x0000' "$APP"
grep -q 'B2L_USB_PCS_ABORT_OFFSET_C[[:space:]]*0x000C' "$APP"
grep -q 'B2L_USB_PMA_ABORT_OFFSET_0[[:space:]]*0x0000' "$APP"
grep -q 'ShowSkippedComboPcs' "$APP"
grep -q 'ShowSkippedComboPma' "$APP"
grep -q 'Value = Read32 (B2L_USB_PHY_BASE, Offset);' "$APP"
grep -q 'bash yap.sh --touch --usb-test --clean' "$ROOT/BUILD.md"
grep -q 'bash yap.sh --touch --usb-test --clean' \
  "$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/README.md"
grep -q 'USB tani paketi Windows dokunmatigi korumali; b2ltouch.sys yok' "$ROOT/cache_esp.sh"

for Address in 10C00000 10B00000 10AE0000 10AF0000 15860000 15862200; do
  grep -q "$Address" "$APP"
done

grep -q 'reg = <0x00 0x10c00000 0x10000>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'interrupts = <0x00 0xf1 0x00>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'reg = <0x00 0x10b00000 0x200 0x00 0x10ae0000 0x2800 0x00 0x10af0000 0x800>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"

# DT'nin etkin USB pencereleri CPU sayfa tablolarinda yalniz gereken 4 KiB
# granuler kapsama yuvarlanmalidir. DRAM/reserved-memory descriptor'lari bu
# test tarafindan degistirilmez.
grep -Eq '\{"USB DRD PHY",[[:space:]]*0x10B00000,[[:space:]]*0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"USB Combo PCS",[[:space:]]*0x10AE0000,[[:space:]]*0x00003000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"USB Combo PMA",[[:space:]]*0x10AF0000,[[:space:]]*0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"
grep -Eq '\{"USB DWC3 xHCI",[[:space:]]*0x10C00000,[[:space:]]*0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,[[:space:]]*NS_DEVICE\}' "$MAP"

if rg -n 'MmioWrite|IoWrite|PciWrite|WriteRegister|MmioBitFieldWrite|MmioAnd|MmioOr' "$APP"; then
  echo 'HATA: ilk USB EFI tanisi register yazma islemi iceriyor' >&2
  exit 1
fi

if rg -n '#include.*I2c|gEfiI2c|HsI2c|I2cWrite|I2cTransfer' "$APP"; then
  echo 'HATA: ilk USB EFI tanisi MAX77705/I2C islemi iceriyor' >&2
  exit 1
fi

if rg -n 'B2L_PD_FSYS0A_BASE, 0x1[04]' "$APP"; then
  echo 'HATA: cihazda external abort veren FSYS0A duration alani okunuyor' >&2
  exit 1
fi

if rg -n 'Read32 \(B2L_USB_(PCS|PMA)_BASE|ShowPhyWindow' "$APP"; then
  echo 'HATA: kablosuz testte external abort veren combo PHY penceresi okunuyor' >&2
  exit 1
fi

echo 'B2LUsbTest safety checks passed'
