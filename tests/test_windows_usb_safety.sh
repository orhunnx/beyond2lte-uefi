#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN="$ROOT/windows/b2lusb"
DSDT="$ROOT/silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl"
PARENT_INF="$WIN/b2lusb.inf.in"
HOST_INF="$WIN/b2lusb-host.inf.in"

URS_BLOCK="$(sed -n '/Device (URS0)/,/^[[:space:]]*\/\/ Device (UFS0)/p' "$DSDT")"
test "$(printf '%s\n' "$URS_BLOCK" | rg -c 'Memory32Fixed')" -eq 3
printf '%s\n' "$URS_BLOCK" | grep -q '0x10C00000, 0x00010000'
printf '%s\n' "$URS_BLOCK" | grep -q '0x10B00000, 0x00000200'
printf '%s\n' "$URS_BLOCK" | grep -q '0x1586072C, 0x00000004'
printf '%s\n' "$URS_BLOCK" | grep -Fq 'Name (_HID, "BYN0002")'
printf '%s\n' "$URS_BLOCK" | grep -q 'Name (_ADR, Zero)'
printf '%s\n' "$URS_BLOCK" | grep -q 'Name (_ADR, One)'
test "$(printf '%s\n' "$URS_BLOCK" | rg -c '0x00000111')" -eq 2
if printf '%s\n' "$URS_BLOCK" | rg -qi '0x10AE0000|0x10AF0000|0x15860704|Gpio|I2cSerialBus'; then
  echo 'HATA: URS0 ilk asama allow-list disi USB3/Type-C kaynagi iceriyor' >&2
  exit 1
fi

grep -Fq 'ACPI\BYN0002' "$PARENT_INF"
grep -q 'KmdfLibraryVersion=1.15' "$PARENT_INF"
grep -Fq 'URS\BYN0002&HOST' "$HOST_INF"
grep -qi 'Include=usbxhci.inf' "$HOST_INF"
grep -q 'Needs=Generic.Install.NT' "$HOST_INF"
grep -q 'Needs=Generic.Install.NT.Services' "$HOST_INF"
grep -q 'Needs=Generic.Install.NT.HW' "$HOST_INF"

grep -q 'UrsDeviceInitInitialize' "$WIN/b2lusb.c"
grep -q 'URS_CONFIG_INIT' "$WIN/b2lusb.c"
grep -q 'UrsHostInterfaceTypeXhci' "$WIN/b2lusb.c"
grep -q 'UrsDeviceInitialize' "$WIN/b2lusb.c"
grep -q 'UrsSetHardwareEventSupport(Device, TRUE)' "$WIN/b2lusb.c"
grep -q 'UrsReportHardwareEvent' "$WIN/b2lusb.c"
grep -q 'UrsHardwareEventIdGround' "$WIN/b2lusb.c"
grep -q 'UrsHardwareEventIdFloat' "$WIN/b2lusb.c"
grep -q 'UrsHardwareEventDetach' "$WIN/b2lusb.c"
grep -q 'UrsIoResourceListAppendDescriptor' "$WIN/b2lusb.c"
test "$(rg -c 'MmMapIoSpaceEx' "$WIN/b2lusb.c")" -eq 3
grep -q 'MemoryCount != 3' "$WIN/b2lusb.c"
grep -q 'Resource->u.Memory.Length == B2L_DWC3_LENGTH' "$WIN/b2lusb.c"
grep -q 'Resource->u.Memory.Length == B2L_USBPHY_LENGTH' "$WIN/b2lusb.c"
grep -q 'Resource->u.Memory.Length == B2L_USBPMU_LENGTH' "$WIN/b2lusb.c"
if rg -n 'MmMapIoSpaceEx.*0x|SetLdo|SetBuck|S2MPS|firmware.update|flash' "$WIN"/*.[ch]; then
  echo 'HATA: Windows USB surucusu PnP kaynagini atliyor veya PMIC/firmware yaziyor' >&2
  exit 1
fi

grep -q '#define B2L_DWC3_PHYSICAL_BASE 0x10C00000ULL' "$WIN/b2lusb.h"
grep -q '#define B2L_USBPHY_PHYSICAL_BASE 0x10B00000ULL' "$WIN/b2lusb.h"
grep -q '#define B2L_USBPMU_PHYSICAL_BASE 0x1586072CULL' "$WIN/b2lusb.h"
if rg -ni '10AE0000|10AF0000|15D60000' "$WIN"/*.[ch]; then
  echo 'HATA: ilk Windows USB surucusu combo PHY veya MAX77705 MMIO alanina genislemis' >&2
  exit 1
fi

grep -q 'B2L_LINK_PORT_U3_DISABLE' "$WIN/b2lusb_hw.c"
grep -q 'B2L_LINK_PORT_U2_DISABLE' "$WIN/b2lusb_hw.c"
grep -Fq 'B2L_HSP_TUNE_TXRES((Role == B2lUsbRoleHost) ? 1 : 3)' "$WIN/b2lusb_hw.c"
grep -Fq 'B2L_HSP_TUNE_SQRX((Role == B2lUsbRoleHost) ? 5 : 3)' "$WIN/b2lusb_hw.c"
grep -q 'B2L_HSP_TUNE_TXVREF(12)' "$WIN/b2lusb_hw.c"
grep -q 'B2L_HSP_TUNE_TXPREEMPA(3)' "$WIN/b2lusb_hw.c"
grep -q 'B2L_HSP_TUNE_COMPDIS(7)' "$WIN/b2lusb_hw.c"
grep -q 'B2lRequireXhciHalted' "$WIN/b2lusb_hw.c"
grep -q 'B2lStopDeviceController' "$WIN/b2lusb_hw.c"

grep -Fq 'Urscxstub.lib' "$WIN/build-b2lusb.ps1"
grep -Fq 'km\urs\1.0\UrsCx.h' "$WIN/build-b2lusb.ps1"
grep -Fq '#include <urs/1.0/UrsCx.h>' "$WIN/b2lusb.h"
grep -Fq 'km/urs/1.0/UrsCx.h' "$ROOT/tools_b2lusb.sh"
grep -Fq 'setupapi.lib' "$WIN/build-b2lusb.ps1"
grep -q '/machine:arm64' "$WIN/build-b2lusb.ps1"
grep -q '/MT ' "$WIN/build-b2lusb.ps1"
grep -q 'build-b2lusb.ps1' "$ROOT/tools_b2lusb.sh"
grep -q 'UrsCx01000.sys' "$ROOT/tools_b2lusb.sh"
grep -q 'USBXHCI.SYS' "$ROOT/tools_b2lusb.sh"
grep -q 'USBHUB3.SYS' "$ROOT/tools_b2lusb.sh"
grep -q 'B2LUSB_SUPERSPEED=DISABLED_UNTIL_COMBO_PMA_SAFE' "$ROOT/tools_b2lusb.sh"
grep -q 'tools_b2lusb.sh' "$ROOT/yap.sh"
grep -q -- '--usb-test) USB_TEST=1; DO_ACPI=1' "$ROOT/yap.sh"
grep -q 'B2L_SKIP_WINDOWS_USB_BUILD' "$ROOT/yap.sh"
grep -q 'verify_winpe_b2lusb_package' "$ROOT/cache_esp.sh"
grep -q 'B2LGOSTR.EFI|B2LUSB.EFI|B2LTOUCH.EFI|EBSTEST.EFI' "$ROOT/cache_esp.sh"
grep -q 'call :B2LUsbInstall' "$ROOT/windows/startnet.cmd"
grep -A1 'call :B2LUsbInstall' "$ROOT/windows/startnet.cmd" | grep -q 'if errorlevel 1 goto :Finish'
grep -q 'b2lusbctl.exe" status' "$ROOT/windows/startnet.cmd"
if grep -q 'b2lusbctl.exe" host' "$ROOT/windows/startnet.cmd"; then
  echo 'HATA: WinPE startnet acilista otomatik host role geciyor' >&2
  exit 1
fi

grep -q 'beslenen bir USB2 hub' "$WIN/README.md"
grep -q 'Function stack' "$WIN/README.md"
grep -q 'KDNET-EEM-USB' "$WIN/README.md"
grep -q 'henüz eklenmemiştir' "$WIN/README.md"

echo 'Windows USB safety checks: PASS'
