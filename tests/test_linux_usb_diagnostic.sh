#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COLLECTOR="$ROOT/linux_test/usb-diagnostic.sh"
INIT="$ROOT/linux_test/init"
PACKER="$ROOT/linux_test/kur.sh"

sh -n "$COLLECTOR"
sh -n "$INIT"
bash -n "$PACKER"
test -x "$COLLECTOR"

grep -q 'snapshot BEFORE_UNPLUGGED' "$COLLECTOR"
grep -q 'snapshot AFTER_PLUGGED' "$COLLECTOR"
grep -q '0x15D60000' "$COLLECTOR"
grep -q '0x10B000' "$COLLECTOR"
grep -q '0x10C00000' "$COLLECTOR"
grep -q 'PMU_USB_PHY_CTRL' "$COLLECTOR"
grep -q 'm_vdd_ldo1\[012\]' "$COLLECTOR"
grep -q '/b2l-usb-diagnostic --guided' "$INIT"
grep -q 'usb-diagnostic.sh.*b2l-usb-diagnostic' "$PACKER"

# The Linux run is an observer: no PMIC/I2C writes and no devmem write form.
if grep -Ev '^[[:space:]]*#' "$COLLECTOR" | \
   rg -n '\bi2c(set|write)\b|\bi2cget\b|\bi2cset\b|devmem[^\n]*(=|write)'; then
  echo 'HATA: Linux USB tani araci salt-okunur sinirini asti' >&2
  exit 1
fi

echo 'Linux USB diagnostic safety checks passed.'
