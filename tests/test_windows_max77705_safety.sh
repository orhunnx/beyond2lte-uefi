#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN="$ROOT/windows/b2lusb"
MAIN="$WIN/b2lmax77705.c"
HW="$WIN/b2lmax77705_hw.c"
HDR="$WIN/b2lmax77705.h"
IRQ="$WIN/b2lmax77705_irq.c"
INF="$WIN/b2lmax77705.inf.in"
DSDT="$ROOT/silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl"
DXE="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.c"
DXE_INF="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf"
DXE_INC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Include/DXE.inc"
DSC="$ROOT/mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
HSI2C="$ROOT/mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/HsI2cLib/HsI2c.c"

# Stock beyond2lte HSI2C45/USI ownership is isolated in ACPI\BYN0004.
grep -Eq 'B2L_MAX_HSI2C_PHYSICAL_BASE[[:space:]]+0x15D60000ULL' "$HDR"
grep -Eq 'B2L_MAX_USI_PHYSICAL_BASE[[:space:]]+0x15C22000ULL' "$HDR"
grep -q 'B2L_MAX_CMGP_CMU_PHYSICAL_BASE     0x15C00000ULL' "$HDR"
grep -q 'B2L_MAX_CMGP_PINCTRL_PHYSICAL_BASE 0x15C30000ULL' "$HDR"
grep -q 'B2L_MAX_CMGP_CMU_LENGTH            0x00008000UL' "$HDR"
grep -Fq 'ACPI\BYN0004' "$INF"
grep -Fq 'Name (_HID, "BYN0004")' "$DSDT"
grep -Fq '\_SB.USBC' "$DSDT"
test "$(rg -c 'MmMapIoSpaceEx' "$MAIN")" -eq 5
if rg -n '10C00000|10B00000|10AE0000|10AF0000|1586072C' "$MAIN" "$HW" "$HDR"; then
  echo 'HATA: MAX77705 surucusu DWC3/PHY/PMU kaynagina genislemis' >&2
  exit 1
fi

# Samsung MFD logical-client addresses: TOP, USBC/MUIC and charger.
grep -q 'B2L_MAX_TOP_ADDRESS          0x66U' "$HDR"
grep -q 'B2L_MAX_USBC_ADDRESS         0x25U' "$HDR"
grep -q 'B2L_MAX_CHARGER_ADDRESS      0x69U' "$HDR"
if rg -n 'B2L_MAX_FUEL|0x36U' "$MAIN" "$HW" "$HDR"; then
  echo 'HATA: USB policy surucusu kullanmadigi fuel-gauge istemcisini sahiplenmemeli' >&2
  exit 1
fi
grep -q 'MAX77705_TOP_PMIC_ID1      0x00U' "$MAIN"
grep -q 'MAX77705_TOP_PMIC_REV      0x01U' "$MAIN"
grep -q 'MAX77705_TOP_INTSRC        0x22U' "$MAIN"
grep -q 'MAX77705_TOP_INTSRC_MASK   0x23U' "$MAIN"
grep -q 'Context->PmicId == 0x05U' "$MAIN"
grep -q 'Context->PmicId == 0x15U' "$MAIN"

# CC is read from the MAX77705 firmware state machine.  Connector VBUS is
# enabled only for SOURCE; no-connection and SINK undo OTG.
grep -q 'MAX77705_USBC_CC_STATUS0   0x0AU' "$MAIN"
grep -q 'B2L_MAX_CC_STATUS_MASK       0x07U' "$HDR"
grep -q 'B2L_MAX_CC_SOURCE            0x02U' "$HDR"
grep -q 'CcState == B2L_MAX_CC_SOURCE' "$MAIN"
grep -q 'CcState != B2L_MAX_CC_SOURCE' "$MAIN"
grep -q 'Context->OtgEnabled || Context->OtgHardwareActive' "$MAIN"

# Preserve Samsung charger OTG mode-state and interrupt-mask transitions.
grep -q 'MAX77705_CHG_INT_MASK      0xB1U' "$MAIN"
grep -q 'MAX77705_CHG_CNFG_00       0xB7U' "$MAIN"
grep -q 'MAX77705_CHG_CNFG_06       0xBDU' "$MAIN"
grep -q 'MAX77705_CHG_OTG_INT_MASK  0x51U' "$MAIN"
grep -q 'MAX77705_CHG_OTG_INT_VALUE 0x50U' "$MAIN"
grep -q 'MAX77705_CHG_PROTECT_MASK  0x0CU' "$MAIN"
grep -q 'MAX77705_CHG_PROTECT_CLEAR_MASK 0xF3U' "$MAIN"
grep -q 'MAX77705_CHG_PROTECT_OPEN  0x0CU' "$MAIN"
if rg -n '\(UCHAR\)~MAX77705_CHG_PROTECT_MASK' "$MAIN"; then
  echo 'HATA: MSVC C4310 uretecek sabit daraltma kalibi geri geldi' >&2
  exit 1
fi
grep -q 'MAX77705_CHG_UNLOCK_RETRIES 10UL' "$MAIN"
grep -q 'MAX77705_CHG_UNLOCK_WAIT_US 20000UL' "$MAIN"
grep -q 'B2lMaxUnlockCharger' "$MAIN"
grep -q 'Context->ChargerUnlocked = TRUE' "$MAIN"
grep -q 'Context->ChargerConfig06' "$MAIN"
for Mode in \
  MAX77705_MODE_ALL_OFF MAX77705_MODE_BUCK MAX77705_MODE_BUCK_CHG \
  MAX77705_MODE_BOOST_UNO MAX77705_MODE_BOOST_OTG \
  MAX77705_MODE_BUCK_UNO MAX77705_MODE_BUCK_CHG_UNO \
  MAX77705_MODE_BUCK_OTG MAX77705_MODE_BUCK_CHG_OTG; do
  grep -q "$Mode" "$MAIN"
done
grep -q 'B2lMaxDelayMicroseconds(1500U)' "$MAIN"
grep -q 'B2lMaxDelayMicroseconds(50000U)' "$MAIN"
grep -q 'KeDelayExecutionThread' "$MAIN"
if rg -n 'KeStallExecutionProcessor\((1500|50000)U\)' "$MAIN"; then
  echo 'HATA: Samsung sleep araligi Windows CPU aktif beklemesine donusmus' >&2
  exit 1
fi
grep -q 'Context->ChargerIntMaskBaseline' "$MAIN"
grep -q 'Context->ChargerModeBaseline' "$MAIN"

# Mutable charger registers go through masked read/modify/write + readback.
# The only direct USBC write is Samsung's bounded factory opcode transport.
grep -q 'Wanted = (UCHAR)((Current &' "$MAIN"
grep -Fq '((*ReadBack) & Mask) == (Wanted & Mask)' "$MAIN"
grep -q 'MAX77705_OPCODE_FACTORY_TEST     0x54U' "$MAIN"
grep -q 'MAX77705_FACTORY_AUTO_VBUS_OFF   0x00U' "$MAIN"
grep -q 'MAX77705_OPCODE_FW_AUTOIBUS      0x57U' "$MAIN"
grep -q 'MAX77705_AUTOIBUS_AT_OFF         0x01U' "$MAIN"
grep -q 'MAX77705_OPCODE_CCCTRL1_READ     0x0BU' "$MAIN"
grep -q 'MAX77705_OPCODE_CCCTRL1_WRITE    0x0CU' "$MAIN"
grep -q 'MAX77705_OPCODE_CONTROL1_READ    0x05U' "$MAIN"
grep -q 'MAX77705_OPCODE_CONTROL1_WRITE   0x06U' "$MAIN"
grep -q 'MAX77705_OPCODE_CONTROL3_READ    0x1CU' "$MAIN"
grep -q 'MAX77705_OPCODE_CONTROL3_WRITE   0x1DU' "$MAIN"
grep -q 'MAX77705_CONTROL3_LOCK_ERROR_EN  (1U << 1)' "$MAIN"
grep -q 'MAX77705_CONTROL3_LOCK_ERROR_EN_CLEAR_MASK 0xFDU' "$MAIN"
if grep -q '~MAX77705_CONTROL3_LOCK_ERROR_EN' "$MAIN"; then
  echo 'HATA: MSVC C4310 ureten promoted bitwise complement geri gelmemeli' >&2
  exit 1
fi
grep -q 'MAX77705_MUIC_COM_USB_AP         0x09U' "$MAIN"
grep -q 'MAX77705_MUIC_COM_OPEN           0x3FU' "$MAIN"
grep -q 'MAX77705_CCCTRL1_SOURCE_SINK     (1U << 4)' "$MAIN"
grep -q 'MAX77705_CCCTRL1_SINK_SOURCE     (1U << 3)' "$MAIN"
grep -q 'MAX77705_CCCTRL1_DEBUG_ENABLE    (1U << 2)' "$MAIN"
grep -q 'MAX77705_CCCTRL1_AUDIO_ENABLE    (1U << 1)' "$MAIN"
grep -q 'MAX77705_CCCTRL1_DETECT_ENABLE   (1U << 0)' "$MAIN"
grep -q 'MAX77705_USBC_UIC_INT_MASK 0x0EU' "$MAIN"
grep -q 'MAX77705_USBC_CC_INT_MASK  0x0FU' "$MAIN"
grep -q 'MAX77705_USBC_PD_INT_MASK  0x10U' "$MAIN"
grep -q 'MAX77705_USBC_VDM_INT_MASK 0x11U' "$MAIN"
grep -q 'MAX77705_UIC_INT_MASK_LINUX      0x00U' "$MAIN"
grep -q 'MAX77705_CC_INT_MASK_LINUX       0x20U' "$MAIN"
grep -q 'MAX77705_PD_INT_MASK_PROBE       0x1CU' "$MAIN"
grep -q 'MAX77705_VDM_INT_MASK_PROBE      0xFFU' "$MAIN"
grep -q 'MAX77705_PD_INT_MASK_RUNTIME     0x00U' "$MAIN"
grep -q 'MAX77705_VDM_INT_MASK_RUNTIME    0xF0U' "$MAIN"
grep -q 'MAX77705_USBC_OPCODE_WRITE 0x21U' "$MAIN"
grep -q 'MAX77705_USBC_OPCODE_END   0x41U' "$MAIN"
grep -q 'MAX77705_USBC_OPCODE_READ  0x51U' "$MAIN"
grep -q 'MAX77705_USBC_INT_APCMDRES' "$MAIN"
# Real opcode responses are owned by APCMDRES and the passive MFD lock.
grep -Fq 'Response[0] != Request->Opcode' "$IRQ"
grep -Fq 'B2lMaxQueueOpcodeLocked' "$MAIN"
grep -Fq 'B2lMaxWaitOpcode' "$MAIN"
grep -Fq 'B2lMaxNormalizeCcOpen' "$MAIN"
grep -Fq 'B2lMaxReadCcStatus' "$MAIN"
grep -Fq 'B2lMaxSetMuicPath' "$MAIN"
grep -Fq 'Context->MuicControl1 != Path' "$MAIN"
grep -Fq 'ReadBack != MAX77705_CCCTRL1_BEYOND2LTE' "$MAIN"
grep -Fq 'InitialDetect && (CcState != B2L_MAX_CC_SINK)' "$MAIN"
grep -Fq 'Frame->Active[1] & MAX77705_CC_INT_STATE' "$MAIN"
grep -q 'DriverVer=09/05/2026,0.1.0.35' "$INF"
# Probe ordering, response ownership, cancellation and masks execute in C
# fixtures (test_max77705_irq.py), rather than line-order approximations.
if rg -n 'B2lMaxReadAndClearUsbcInterrupts|B2lMaxConfigureRuntimeUsbcMasks|B2lMaxSendOpcode' "$MAIN"; then
  echo 'HATA: eski opcode polling/probe maskesi yolu geri geldi' >&2
  exit 1
fi
# Power/data role and BYP ordering executes in test_max77705_connection.py.
grep -Fq 'MAX77705_USBC_PD_STATUS1   0x0DU' "$MAIN"
grep -Fq 'MAX77705_PD_INT_DATAROLE   (1U << 5)' "$MAIN"
grep -Fq 'B2lMaxHandleDataRole' "$MAIN"
grep -Fq 'B2lMaxHandleBypass' "$MAIN"
grep -Fq 'Context->PowerRole != B2L_MAX_CC_SOURCE' "$MAIN"
grep -Fq 'Frame.DetachRecheck' "$IRQ"
test "$(rg -c 'Status = B2lMaxUnlockCharger\(Context\);' "$MAIN")" -ge 3
if rg -ni 'firmware.*(write|update|flash)|SW_RESET|USBC_RESET' "$MAIN" "$HDR"; then
  echo 'HATA: MAX77705 Windows yolu firmware/reset politikasina genislemis' >&2
  exit 1
fi

# HSI2C transfer path is bounded, serialized and uses repeated START for
# one-byte register reads, matching Samsung's register transport.
grep -q 'B2L_HSI2C_IRQ_TIMEOUT_100NS 10000000ULL' "$HW"
grep -q 'WdfInterruptCreate(Device, &InterruptConfig' "$MAIN"
grep -q 'B2L_MAX_HSI2C_GSIV                 114UL' "$HDR"
grep -q '0x00000072' "$DSDT"
grep -q 'InterruptCount != 2UL' "$MAIN"
grep -q 'B2L_MAX_HSI2C_GSIV : B2L_MAX_GPIO_GSIV' "$MAIN"
grep -q 'KeWaitForSingleObject(&Context->HsI2cCompletion' "$HW"
grep -q 'WdfInterruptQueueDpcForIsr(Interrupt)' "$HW"
if rg -n 'B2lMaxPollingDeadline|B2L_HSI2C_POLL_DELAY_US' "$HW"; then
  echo 'HATA: HSI2C aktarimi gercek IRQ yerine polling yoluna dondu' >&2
  exit 1
fi
grep -q 'KeQueryInterruptTime()' "$HW"
grep -q 'KeDelayExecutionThread(KernelMode, FALSE, &Interval)' "$HW"
if grep -q 'KeStallExecutionProcessor' "$HW"; then
  echo 'HATA: tek cekirdekli WinPE MAX77705 polling yolunda busy-spin yapamaz' >&2
  exit 1
fi
grep -Fq 'B2lMaxWriteRegister(Context, HSI2C_CTRL, HSI2C_MASTER);' "$HW"
if grep -q 'HSI2C_FUNC_MODE_I2C' "$HW"; then
  echo 'HATA: universal9820 HSI2C45 CTRL durumuna kullanilmayan FUNC_MODE_I2C biti eklendi' >&2
  exit 1
fi
grep -q 'HSI2C_FAST_MASTER_ID    (0x7UL << 24)' "$HW"
grep -q 'TargetAddress |= HSI2C_FAST_MASTER_ID' "$HW"
grep -q 'B2L_HSI2C_RETRIES       2UL' "$HW"
grep -q 'HSI2C_INT_NO_DEVICE' "$HW"
grep -q 'HSI2C_MASTER_NO_ACK' "$HW"
grep -q 'HSI2C_MASTER_LOST' "$HW"
grep -q 'HSI2C_USI_CON           0xC4U' "$HW"
grep -q 'HSI2C_USI_RESET_RELEASE 0x00000000UL' "$HW"
if grep -q 'B2lMaxWaitBusIdle\|HSI2C_MASTER_BUSY' "$HW"; then
  echo 'HATA: hsi2c_45 transferi stokta olmayan BUS_IDLE kapisiyla engellenmis' >&2
  exit 1
fi
grep -q 'B2L_HSI2C_FS1_400KHZ    0x00F00000UL' "$HW"
grep -q 'B2L_HSI2C_FS2_400KHZ    0x000000E0UL' "$HW"
grep -q 'B2L_HSI2C_FS3_400KHZ    0x00210000UL' "$HW"
grep -q 'B2lMaxWriteRegister(Context, HSI2C_TIMING_FS1, Timing.TimingFs1)' "$HW"
grep -q 'B2L_CMGP_MUX_USI3       0x1020U' "$HW"
grep -q 'B2L_CMGP_DIV_USI3       0x1824U' "$HW"
grep -q 'B2L_CMGP_QCH_USI3       0x304CU' "$HW"
grep -q 'B2L_GPM12_CON           0x180U' "$HW"
grep -q 'B2L_GPM13_CON           0x1A0U' "$HW"
grep -q 'B2L_PIN_FUNCTION_I2C    0x00000002UL' "$HW"
grep -q 'B2lMaxPreparePlatform(Context)' "$HW"
grep -q 'Context->PlatformPrepared = TRUE' "$HW"
grep -q 'B2L_MAX_HSI2C_PHASE_PLATFORM 0x08UL' "$HDR"
PLATFORM_SECTION="$(sed -n '/B2lMaxPreparePlatform(PB2L_MAX_CONTEXT Context)/,/^}/p' "$HW")"
DIV_LINE="$(printf '%s\n' "$PLATFORM_SECTION" | grep -n 'B2L_CMGP_DIV_USI3,' | head -n1 | cut -d: -f1)"
MUX_LINE="$(printf '%s\n' "$PLATFORM_SECTION" | grep -n 'B2L_CMGP_MUX_USI3,' | head -n1 | cut -d: -f1)"
test "$DIV_LINE" -lt "$MUX_LINE"
test "$(printf '%s\n' "$PLATFORM_SECTION" | rg -c 'B2L_GPM1[23]_(PUD|DRV)')" -ge 8
if grep -q 'Context->HsI2cClockControl == 0UL' "$HW"; then
  echo 'HATA: Exynos9820 HSI2C45 CLK_CTRL=0 durumu sahte hazirlik hatasi sayilamaz' >&2
  exit 1
fi
# Stock IRQ mode fills TX from the FIFO interrupt; the passive message
# function only configures the hardware and waits for completion.
TRANSFER_SECTION="$(sed -n '/B2lMaxTransferMessage(/,/B2lMaxHsI2cWriteRead(/p' "$HW")"
if printf '%s\n' "$TRANSFER_SECTION" | grep -Eq 'HSI2C_TX_DATA|HSI2C_RX_DATA'; then
  echo 'HATA: HSI2C FIFO ISR disindan ilerletiliyor' >&2
  exit 1
fi
test "$(grep -n 'HSI2C_ADDRESS, TargetAddress' "$HW" | head -n1 | cut -d: -f1)" \
  -lt "$(grep -n 'HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN' "$HW" | head -n1 | cut -d: -f1)"
grep -q 'Context->HsI2cAttemptedAddress = TargetAddress' "$HW"
grep -q 'WdfWaitLockAcquire(Context->BusLock' "$MAIN"
grep -q 'B2lMaxTransferMessage' "$HW"
grep -q 'FALSE,' "$HW"
grep -q '(BOOLEAN)(ReadLength == 0UL)' "$HW"
# No connection or opcode polling. The one-shot timer only cancels a missing
# response after its deadline; actual advancement remains in the physical ISR.
# Behavioral timeout/no-I2C/ordering checks execute in test_max77705_irq.py.
grep -Fq 'WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lMaxEvtOpcodeDeadline)' "$MAIN"
grep -Fq 'B2lMaxDispatchCcPdLocked(Context, &Frame)' "$IRQ"
grep -Fq 'B2lMaxRequestMuicLocked' "$IRQ"
if rg -n 'WDF_TIMER_CONFIG_INIT_PERIODIC|B2lMaxEvtPollTimer|PollingEnabled|B2L_MAX_.*POLL_MS' "$MAIN" "$IRQ" "$HDR"; then
  echo 'HATA: MAX olaylari gercek IRQ yerine timer ile ilerletiliyor' >&2
  exit 1
fi
grep -Fq 'InterruptConfig.PassiveHandling = TRUE' "$MAIN"
grep -Fq 'InterruptConfig.WaitLock = Context->MfdLock' "$MAIN"
grep -Fq 'WorkConfig.AutomaticSerialization = FALSE' "$MAIN"
grep -Fq 'WdfWorkItemFlush(Context->PolicyWork)' "$MAIN"
grep -Fq 'WdfWaitLockAcquire(Context->TransportLock' "$HW"
grep -Fq 'WdfWaitLockRelease(Context->MfdLock)' "$IRQ"
grep -Fq 'KeWaitForSingleObject(&Request->Completion' "$IRQ"
grep -Fq 'MAX_EINT_CON  0x704UL' "$IRQ"
grep -Fq 'MAX_EINT_MASK 0x904UL' "$IRQ"
grep -Fq 'MAX_EINT_PEND 0xA04UL' "$IRQ"
grep -Fq 'B2lMaxGpioWrite(Context, MAX_EINT_PEND, MAX_PIN_BIT)' "$IRQ"
grep -Fq '0x00000033' "$DSDT"
grep -Fq '0x15850000, 0x00001000' "$DSDT"
if rg -n 'GICD_|GICR_|B2lMaxReadBlock.*0x02U' "$MAIN"; then
  echo 'HATA: MAX politika isi kesme dagiticisini veya read-to-clear registerini sahipleniyor' >&2
  exit 1
fi
# Adapter MMIO/clock setup must precede IRQ enable; client transfers start
# afterwards and shutdown finishes before KMDF disconnects the interrupt.
grep -q 'EvtDeviceD0EntryPostInterruptsEnabled = B2lMaxEvtD0EntryPostInterruptsEnabled' "$MAIN"
grep -q 'EvtDeviceD0ExitPreInterruptsDisabled = B2lMaxEvtD0ExitPreInterruptsDisabled' "$MAIN"
D0_SECTION="$(sed -n '/B2lMaxEvtD0Entry(WDFDEVICE Device/,/B2lMaxEvtD0EntryPostInterruptsEnabled(WDFDEVICE Device/p' "$MAIN")"
printf '%s\n' "$D0_SECTION" | grep -q 'Status = B2lMaxHsI2cInitialize(Context)'
if printf '%s\n' "$D0_SECTION" | grep -q 'WdfWorkItemEnqueue'; then
  echo 'HATA: MAX istemci transferi IRQ acilmadan baslatiliyor' >&2
  exit 1
fi
RELEASE_SECTION="$(sed -n '/B2lMaxEvtReleaseHardware(/,/B2lMaxEvtD0Entry(WDFDEVICE Device/p' "$MAIN")"
if printf '%s\n' "$RELEASE_SECTION" | grep -Eq 'B2lMaxDisableOtg|B2lMaxSetMuicPath'; then
  echo 'HATA: IRQ kapandiktan sonra ReleaseHardware I2C baslatiyor' >&2
  exit 1
fi

# UEFI prepares the exact stock bus and boot-on USB PHY rails; Windows remains
# MAX77705 charger/CC/PD policy owner.
grep -q '\[45\] = {' "$HSI2C"
grep -q 'SclBankNum = 12' "$HSI2C"
grep -q 'SdaBankNum = 13' "$HSI2C"
grep -q 'B2L_MAX77705_BUS             45' "$DXE"
for Ldo in 10 11 12; do
  grep -q "B2L_USB_PHY_LDO${Ldo}[[:space:]]*${Ldo}" "$DXE"
done
grep -q 'Pmic->SetLdo (' "$DXE"
grep -q 'ID_S2MPS19' "$DXE"
grep -q 'S2MPS_MODE_NORMAL' "$DXE"
grep -q 'B2L_USB_PHY_LDO_SETTLE_US    1000' "$DXE"
test "$(rg -c 'HsI2c->Read \(' "$DXE")" -eq 6
grep -qx '  beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf' "$DSC"
grep -qx '  # INF beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf' "$DXE_INC"
grep -q 'MODULE_TYPE                    = DXE_DRIVER' "$DXE_INF"
grep -q '^  SiliciumPkg/SiliciumPkg.dec$' "$DXE_INF"
grep -q '^  MemoryAllocationHelperLib$' "$DXE_INF"
grep -q '^  gEfiPmicRegulatorProtocolGuid$' "$DXE_INF"
grep -q 'gEfiHsI2cProtocolGuid AND gEfiPmicRegulatorProtocolGuid' "$DXE_INF"
grep -q '^  (VOID)ImageHandle;$' "$DXE"
grep -q '^  (VOID)SystemTable;$' "$DXE"
if rg -n 'UNREFERENCED_PARAMETER|PAGED_CODE|NT_ASSERT|__pragma|__declspec' "$DXE"; then
  echo 'HATA: UEFI MAX77705 DXE kaynaginda Windows-only C makrosu bulundu' >&2
  exit 1
fi
if rg -n 'HsI2c->Write|CHG_INT_MASK|CC_STATUS0|SetBuck' "$DXE"; then
  echo 'HATA: UEFI hazirlik surucusu MAX77705 politikasina yazmamalı' >&2
  exit 1
fi

grep -q 'b2lmax77705.sys' "$WIN/build-b2lusb.ps1"
grep -q 'b2lmax77705.inf' "$ROOT/tools_b2lusb.sh"
grep -q 'b2lmax77705.inf' "$ROOT/windows/stage-winpe-driverstore.ps1"
grep -Fq 'B2LUSB-MAX77705.TXT' "$ROOT/windows/startnet.cmd"
# One unfiltered key query retains all published values, including fields
# omitted from the compact photo/console summary. Verify the dump destination
# and that the driver still publishes the previously required values.
grep -Fq 'reg.exe query "HKLM\SYSTEM\CurrentControlSet\Services\b2lmax77705" >> "%SystemDrive%\B2LUSB-MAX77705.TXT" 2>&1' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxChgDetails2' "$MAIN"
grep -Fq 'B2LMaxAutoVbusDisabled' "$MAIN"
grep -Fq 'B2LMaxOpcodeResponse' "$MAIN"
grep -Fq 'B2LMaxAutoIbusConfigured' "$MAIN"
grep -Fq 'B2LMaxAutoIbusMode' "$MAIN"
grep -Fq 'B2LMaxHsI2cControl' "$MAIN"
grep -Fq 'B2LMaxHsI2cErrorStatus' "$MAIN"
grep -Fq 'B2LMaxHsI2cTransferStatus' "$MAIN"
grep -Fq 'B2LMaxHsI2cConfiguration' "$MAIN"
grep -Fq 'B2LMaxHsI2cAutoConfiguration' "$MAIN"
grep -Fq 'B2LMaxHsI2cAddress' "$MAIN"
grep -Fq 'B2LMaxHsI2cRetryCount' "$MAIN"
grep -Fq 'B2LMaxHsI2cUsiControl' "$MAIN"
grep -Fq 'B2LMaxHsI2cAttemptedAddress' "$MAIN"
grep -Fq 'B2LMaxHsI2cFailurePhase' "$MAIN"
grep -Fq 'B2LMaxChgConfig06' "$MAIN"
grep -Fq 'B2LMaxChargerUnlocked' "$MAIN"
grep -Fq 'B2LMaxChargerUnlockAttempts' "$MAIN"
grep -Fq 'B2LMaxUicInterrupt' "$MAIN"
grep -Fq 'B2LMaxCcInterrupt' "$MAIN"
grep -Fq 'B2LMaxPdInterrupt' "$MAIN"
grep -Fq 'B2LMaxVdmInterrupt' "$MAIN"
grep -Fq 'B2LMaxUicInterruptMask' "$MAIN"
grep -Fq 'B2LMaxCcInterruptMask' "$MAIN"
grep -Fq 'B2LMaxPdInterruptMask' "$MAIN"
grep -Fq 'B2LMaxVdmInterruptMask' "$MAIN"
grep -Fq 'B2LMaxUsbcMasksConfigured' "$MAIN"
grep -Fq 'B2LMaxUsbcRuntimeMasksConfigured' "$MAIN"
grep -Fq 'B2LMaxCcControlConfigured' "$MAIN"
grep -Fq 'B2LMaxCcControlResponse' "$MAIN"
grep -Fq 'B2LMaxCcControlReadBack' "$MAIN"
grep -Fq 'B2LMaxOtgHardwareActive' "$MAIN"
grep -Fq 'B2LMaxMuicPathRequested' "$MAIN"
grep -Fq 'B2LMaxMuicControl1' "$MAIN"
grep -Fq 'B2LMaxMuicPathVerified' "$MAIN"
grep -Fq 'B2LMaxMuicUsbPath' "$MAIN"
grep -Fq 'B2LMaxPlatformPrepared' "$MAIN"
grep -Fq 'B2LMaxCmgpMuxUsi3' "$MAIN"
grep -Fq 'B2LMaxCmgpDivUsi3' "$MAIN"
grep -Fq 'B2LMaxCmgpQchUsi3' "$MAIN"
grep -Fq 'B2LMaxGpm12Con' "$MAIN"
grep -Fq 'B2LMaxGpm13Con' "$MAIN"

echo 'Windows MAX77705 safety checks: PASS'
