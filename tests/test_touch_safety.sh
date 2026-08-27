#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DXE_INC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Include/DXE.inc"
DSC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
SOC_DSC="$ROOT/mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/S5E9820Pkg.dsc.inc"
TOUCH_DIR="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LTouchDxe"

grep -qx '  # INF beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf' "$DXE_INC"
grep -qx '  # HsI2cLib|S5E9820Pkg/Library/HsI2cLib/HsI2cLib.inf' "$SOC_DSC"
grep -qx '  beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf' "$DSC"
grep -q -- '--touch) TOUCH=1; DO_ACPI=1' "$ROOT/yap.sh"

if rg -n 'APRIORI|0x(8|9|A|B|C|D|E|F)[0-9A-Fa-f]{7}' "$TOUCH_DIR"/*.[ch]; then
  echo "HATA: touch surucusunde APRIORI veya DRAM benzeri sabit adres bulundu"
  exit 1
fi

if rg -n 'MmioWrite32 \(Reg \(HSI2C_INT_ENABLE\), [^0]' "$TOUCH_DIR/B2LHsI2c.c"; then
  echo "HATA: HSI2C kesmesi aciliyor"
  exit 1
fi

for function_name in TouchGetState TouchWaitForInput; do
  if sed -n "/^${function_name} (/,/^}/p" "$TOUCH_DIR/B2LTouchDxe.c" | rg -q 'PollEvents|ReadCommand|WriteCommand|B2LHsI2c'; then
    echo "HATA: $function_name donanima erisiyor"
    exit 1
  fi
done

grep -q 'gEfiEventExitBootServicesGuid' "$TOUCH_DIR/B2LTouchDxe.c"
grep -q 'B2LHsI2cQuiesce' "$TOUCH_DIR/B2LTouchDxe.c"

echo "touch safety checks: PASS"
