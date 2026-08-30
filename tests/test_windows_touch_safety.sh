#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN="$ROOT/windows/b2ltouch"
DSDT="$ROOT/silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl"
INF="$WIN/b2ltouch.inf.in"

TSP_BLOCK="$(sed -n '/Device (TSP0)/,/^[[:space:]]*\/\/ Device (UFS0)/p' "$DSDT")"
test "$(printf '%s\n' "$TSP_BLOCK" | rg -c 'Memory32Fixed')" -eq 2
printf '%s\n' "$TSP_BLOCK" | grep -q '0x104B0000, 0x00001000'
printf '%s\n' "$TSP_BLOCK" | grep -q '0x10421000, 0x00001000'
if printf '%s\n' "$TSP_BLOCK" | rg -q 'Interrupt|Gpio|I2cSerialBus|0x(8|9|A|B|C|D|E|F)[0-9A-Fa-f]{7}'; then
  echo "HATA: TSP0 allowlist disi kaynak veya kisitli bellek tanimli" >&2
  exit 1
fi

grep -q 'ACPI\\BYN0001' "$INF"
grep -q '"LowerFilters",0x00010000,"vhf"' "$INF"
grep -q 'KmdfLibraryVersion=1.15' "$INF"
grep -q 'VHF_CONFIG_INIT' "$WIN/b2ltouch.c"
grep -q 'VhfReadReportSubmit' "$WIN/b2ltouch.c"
grep -Fq "'shared\\vhf.h'" "$WIN/build-b2ltouch.ps1"
grep -Fq 'shared/vhf.h' "$ROOT/tools_b2ltouch.sh"
grep -Fq "'libcntpr.lib'" "$WIN/build-b2ltouch.ps1"
grep -q 'VhfKm.lib libcntpr.lib ntoskrnl.lib' "$WIN/build-b2ltouch.ps1"
grep -q 'Usage (Contact Count)' "$WIN/b2ltouch.c"
grep -q 'Usage (Contact Count Maximum)' "$WIN/b2ltouch.c"
grep -q '#define B2L_TOUCH_MAX_CONTACTS   10U' "$WIN/b2ltouch.h"
test "$(rg -c 'B2L_TOUCH_CONTACT_DESCRIPTOR,' "$WIN/b2ltouch.c")" -eq 10
grep -q 'B2lTouchSubmitFrame' "$WIN/b2ltouch.c"
grep -q 'PendingRelease' "$WIN/b2ltouch.c"
grep -q 'B2L_TOUCH_STAGE_VHF_CREATED' "$WIN/b2ltouch.c"
grep -q 'B2L_TOUCH_STAGE_ADD_ENTERED' "$WIN/b2ltouch.c"
grep -q 'B2L_TOUCH_STAGE_DEVICE_CREATE' "$WIN/b2ltouch.c"
grep -q 'B2lTouchPublishDiagnostic' "$WIN/b2ltouch.c"
grep -q 'B2lTouchPublishServiceDiagnostic' "$WIN/b2ltouch.c"
grep -q 'B2LDriverStatus' "$WIN/b2ltouch.c"
grep -q 'B2LTouchStage' "$ROOT/windows/startnet.cmd"
grep -q 'VHF_SYS=' "$ROOT/windows/startnet.cmd"
grep -q 'call :B2LTouchEnsureVhfService' "$ROOT/windows/startnet.cmd"
grep -Fq 'reg.exe add "%B2LVHFSVC%" /v Type /t REG_DWORD /d 1 /f' "$ROOT/windows/startnet.cmd"
grep -Fq 'reg.exe add "%B2LVHFSVC%" /v Start /t REG_DWORD /d 3 /f' "$ROOT/windows/startnet.cmd"
grep -Fq 'reg.exe add "%B2LVHFSVC%" /v ImagePath /t REG_EXPAND_SZ /d "\SystemRoot\System32\drivers\vhf.sys" /f' "$ROOT/windows/startnet.cmd"
if rg -q 'sc\.exe' "$ROOT/windows/startnet.cmd"; then
  echo "HATA: WinPE imajinda bulunmayan sc.exe kullaniliyor" >&2
  exit 1
fi
grep -q 'Services\\b2ltouch" /v B2LTouchStage' "$ROOT/windows/startnet.cmd"
grep -q 'DriverVer=08/30/2026,0.2.0.0' "$INF"
grep -q 'B2LTOUCH_DRIVER_VERSION=0.2.0.0' "$ROOT/tools_b2ltouch.sh"
grep -q 'B2LTOUCH_PACKAGE=sec-ts-kmdf-vhf-ten-contact' "$ROOT/tools_b2ltouch.sh"
grep -q 'B2LTouchLaunchKeyboard' "$ROOT/windows/startnet.cmd"
grep -q 'TabTip.exe' "$ROOT/windows/startnet.cmd"
grep -q 'System32\\osk.exe' "$ROOT/windows/startnet.cmd"
if rg -q 'b2losk|B2LTOUCH_OSK_SHA256' "$ROOT/windows" "$ROOT/tools_b2ltouch.sh"; then
  echo "HATA: yerel Windows klavyesi yerine paketlenmis ozel OSK bulundu" >&2
  exit 1
fi

grep -q '#define B2L_HSI2C_PHYSICAL_BASE 0x104B0000ULL' "$WIN/b2ltouch.h"
grep -q '#define B2L_USI_PHYSICAL_BASE    0x10421000ULL' "$WIN/b2ltouch.h"
grep -q 'Resource->u.Memory.Length != B2L_DEVICE_PAGE_LENGTH' "$WIN/b2ltouch.c"
grep -q 'Start != B2L_HSI2C_PHYSICAL_BASE' "$WIN/b2ltouch.c"
grep -q 'Start != B2L_USI_PHYSICAL_BASE' "$WIN/b2ltouch.c"
test "$(rg -c 'MmMapIoSpaceEx' "$WIN/b2ltouch.c")" -eq 2
if rg -n 'MmMapIoSpaceEx\([^\n]*0x|SetLdo|SetBuck|S2MPS|firmware.update|flash' "$WIN"/*.[ch]; then
  echo "HATA: Windows touch surucusu MMIO kaynagini atliyor veya PMIC/firmware yaziyor" >&2
  exit 1
fi

grep -q '#define B2L_HSI2C_TIMEOUT_US    100000UL' "$WIN/b2ltouch_hw.c"
grep -q 'WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lTouchEvtTimer)' "$WIN/b2ltouch.c"
if rg -q 'WDF_TIMER_CONFIG_INIT_PERIODIC' "$WIN/b2ltouch.c"; then
  echo "HATA: passive-level timer periyodik yapilandirilmis" >&2
  exit 1
fi
test "$(rg -c 'WdfTimerStart\(' "$WIN/b2ltouch.c")" -eq 2
grep -q 'B2lHsI2cQuiesce(Context)' "$WIN/b2ltouch.c"
grep -q 'B2L_TOUCH_MAX_EVENTS - 1U' "$WIN/b2ltouch.c"
grep -q 'SEC_TS_CMD_CLEAR_EVENT_STACK' "$WIN/b2ltouch.c"
test "$(rg -c 'B2lTouchWriteCommand\(Context, SEC_TS_CMD_CLEAR_EVENT_STACK\)' "$WIN/b2ltouch.c")" -eq 1

echo "Windows touch safety checks: PASS"
