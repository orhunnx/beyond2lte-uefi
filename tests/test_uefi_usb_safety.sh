#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.c"
INF="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.inf"
DSC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"

grep -q 'BASE_NAME[[:space:]]*= B2LUsbTest' "$INF"
grep -q 'Applications/B2LUsbTest/B2LUsbTest.inf' "$DSC"
grep -q -- '--usb-test' "$ROOT/yap.sh"
grep -q 'B2LUSB.EFI' "$ROOT/esp/startup.nsh"
grep -q 'B2LUSB.EFI' "$ROOT/esp/startup-baseline.nsh"

for Address in 10C00000 10B00000 10AE0000 10AF0000 15860000 15862200; do
  grep -q "$Address" "$APP"
done

grep -q 'reg = <0x00 0x10c00000 0x10000>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'interrupts = <0x00 0xf1 0x00>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"
grep -q 'reg = <0x00 0x10b00000 0x200 0x00 0x10ae0000 0x2800 0x00 0x10af0000 0x800>' \
  "$ROOT/mu-silicium-overlay/Resources/DTBs/beyond2lte.dts"

if rg -n 'MmioWrite|IoWrite|PciWrite|WriteRegister|MmioBitFieldWrite|MmioAnd|MmioOr' "$APP"; then
  echo 'HATA: ilk USB EFI tanisi register yazma islemi iceriyor' >&2
  exit 1
fi

if rg -n '#include.*I2c|gEfiI2c|HsI2c|I2cWrite|I2cTransfer' "$APP"; then
  echo 'HATA: ilk USB EFI tanisi MAX77705/I2C islemi iceriyor' >&2
  exit 1
fi

echo 'B2LUsbTest safety checks passed'
