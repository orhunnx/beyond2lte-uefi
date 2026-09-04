#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN="$ROOT/windows/b2lusb"
MAIN="$WIN/b2lmax77705.c"
HW="$WIN/b2lmax77705_hw.c"
HDR="$WIN/b2lmax77705.h"
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
test "$(rg -c 'MmMapIoSpaceEx' "$MAIN")" -eq 4
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
grep -q 'Response\[0\] != Opcode' "$MAIN"
grep -q 'Context->AutoVbusDisabled = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_AUTO_VBUS' "$MAIN"
grep -q 'Context->AutoIbusConfigured = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_AUTO_IBUS' "$MAIN"
grep -q 'B2lMaxConfigureAutoIbus' "$MAIN"
grep -q 'Context->UsbcMasksConfigured = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_USBC_MASKS' "$MAIN"
grep -q 'B2lMaxConfigureUsbcMasks' "$MAIN"
grep -q 'Context->UsbcRuntimeMasksConfigured = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_RUNTIME_MASKS' "$MAIN"
grep -q 'B2lMaxConfigureRuntimeUsbcMasks' "$MAIN"
grep -q 'Context->CcControlConfigured = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_CC_CONTROL' "$MAIN"
grep -q 'B2lMaxConfigureCcControl' "$MAIN"
grep -q 'Context->CcControlReadBack != MAX77705_CCCTRL1_BEYOND2LTE' "$MAIN"
grep -q 'B2lMaxNormalizeCcOpen' "$MAIN"
grep -q 'Context->CcOpenNormalized = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_CC_OPEN' "$MAIN"
grep -q 'Context->Control3After & MAX77705_CONTROL3_LOCK_ERROR_EN' "$MAIN"
grep -q 'B2lMaxSetMuicPath' "$MAIN"
grep -q 'Context->MuicControl1 != Path' "$MAIN"
grep -q 'Context->MuicPathVerified = TRUE' "$MAIN"
grep -q 'B2L_MAX_STAGE_MUIC_USB' "$MAIN"
grep -q 'B2L_MAX_STAGE_MUIC_OPEN' "$MAIN"
grep -q 'B2lMaxReadAndClearUsbcInterrupts' "$MAIN"
grep -q 'B2lMaxReadCcStatus' "$MAIN"
grep -q 'MAX77705_CC_INT_STATE            (1U << 0)' "$MAIN"
grep -q 'MAX77705_CC_INT_CURRENT          (1U << 2)' "$MAIN"
grep -q 'MAX77705_CC_INT_PIN_STATE        (1U << 3)' "$MAIN"
grep -q 'Context->TopInterruptSourceSeen |= Context->TopInterruptSource' "$MAIN"
grep -q 'Context->CcInterruptSeen |= Context->CcInterrupt' "$MAIN"
grep -q 'Context->CcStateChanged = TRUE' "$MAIN"
grep -q 'Context->CcLastTransitionTime = SampleTime' "$MAIN"
grep -q 'SampleTime = KeQueryInterruptTime()' "$MAIN"
test "$(rg -c 'MAX77705_TOP_INTSRC_MASK' "$MAIN")" -eq 2
READ_STATUS_SECTION="$(sed -n '/B2lMaxReadStatus(PB2L_MAX_CONTEXT Context)/,/B2lMaxEvaluateConnection(PB2L_MAX_CONTEXT Context)/p' "$MAIN")"
IRQ_DRAIN_LINE="$(printf '%s\n' "$READ_STATUS_SECTION" | grep -n 'B2lMaxReadAndClearUsbcInterrupts(Context)' | head -n1 | cut -d: -f1)"
CC_STATUS_LINE="$(printf '%s\n' "$READ_STATUS_SECTION" | grep -n 'B2lMaxReadCcStatus(Context)' | head -n1 | cut -d: -f1)"
CHARGER_LINE="$(printf '%s\n' "$READ_STATUS_SECTION" | grep -n 'B2L_MAX_CHARGER_ADDRESS' | head -n1 | cut -d: -f1)"
test "$IRQ_DRAIN_LINE" -lt "$CC_STATUS_LINE"
test "$CC_STATUS_LINE" -lt "$CHARGER_LINE"
grep -q 'DriverVer=09/04/2026,0.1.0.27' "$INF"
SERVICE_SECTION="$(sed -n '/B2lMaxServiceConnection(PB2L_MAX_CONTEXT Context)/,/B2lMaxUnmapResources(PB2L_MAX_CONTEXT Context)/p' "$MAIN")"
AUTOIBUS_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxConfigureAutoIbus(Context)' | head -n1 | cut -d: -f1)"
MASKS_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxConfigureUsbcMasks(Context)' | head -n1 | cut -d: -f1)"
AUTOVBUS_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxDisableAutoVbus(Context)' | head -n1 | cut -d: -f1)"
CCCTRL_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxConfigureCcControl(Context)' | head -n1 | cut -d: -f1)"
RUNTIME_MASKS_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxConfigureRuntimeUsbcMasks(Context)' | head -n1 | cut -d: -f1)"
CC_OPEN_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxNormalizeCcOpen(Context)' | head -n1 | cut -d: -f1)"
EVALUATE_LINE="$(printf '%s\n' "$SERVICE_SECTION" | grep -n 'B2lMaxEvaluateConnection(Context)' | head -n1 | cut -d: -f1)"
test "$AUTOIBUS_LINE" -lt "$MASKS_LINE"
test "$MASKS_LINE" -lt "$AUTOVBUS_LINE"
test "$AUTOVBUS_LINE" -lt "$CCCTRL_LINE"
test "$CCCTRL_LINE" -lt "$RUNTIME_MASKS_LINE"
test "$RUNTIME_MASKS_LINE" -lt "$CC_OPEN_LINE"
test "$CC_OPEN_LINE" -lt "$EVALUATE_LINE"
EVALUATE_SECTION="$(sed -n '/B2lMaxEvaluateConnection(PB2L_MAX_CONTEXT Context)/,/B2lMaxServiceConnection(PB2L_MAX_CONTEXT Context)/p' "$MAIN")"
MUIC_USB_LINE="$(printf '%s\n' "$EVALUATE_SECTION" | grep -n 'MAX77705_MUIC_COM_USB_AP' | tail -n1 | cut -d: -f1)"
OTG_ON_LINE="$(printf '%s\n' "$EVALUATE_SECTION" | grep -n 'B2lMaxEnableOtg(Context)' | head -n1 | cut -d: -f1)"
OTG_OFF_LINE="$(printf '%s\n' "$EVALUATE_SECTION" | grep -n 'B2lMaxDisableOtg(Context)' | head -n1 | cut -d: -f1)"
MUIC_OPEN_LINE="$(printf '%s\n' "$EVALUATE_SECTION" | grep -n 'MAX77705_MUIC_COM_OPEN' | tail -n1 | cut -d: -f1)"
test "$MUIC_USB_LINE" -lt "$OTG_ON_LINE"
test "$OTG_OFF_LINE" -lt "$MUIC_OPEN_LINE"
test "$(rg -c 'Status = B2lMaxUnlockCharger\(Context\);' "$MAIN")" -ge 3
if rg -ni 'firmware.*(write|update|flash)|SW_RESET|USBC_RESET' "$MAIN" "$HDR"; then
  echo 'HATA: MAX77705 Windows yolu firmware/reset politikasina genislemis' >&2
  exit 1
fi

# HSI2C transfer path is bounded, serialized and uses repeated START for
# one-byte register reads, matching Samsung's register transport.
grep -q 'B2L_HSI2C_TIMEOUT_US    100000UL' "$HW"
grep -q 'B2L_HSI2C_POLL_DELAY_US 1000UL' "$HW"
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
test "$(grep -n 'HSI2C_TX_DATA, Buffer\[Index++\]' "$HW" | head -n1 | cut -d: -f1)" \
  -lt "$(grep -n 'HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN' "$HW" | head -n1 | cut -d: -f1)"
test "$(grep -n 'HSI2C_ADDRESS, TargetAddress' "$HW" | head -n1 | cut -d: -f1)" \
  -lt "$(grep -n 'HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN' "$HW" | head -n1 | cut -d: -f1)"
grep -q 'Context->HsI2cAttemptedAddress = TargetAddress' "$HW"
grep -q 'WdfWaitLockAcquire(Context->BusLock' "$MAIN"
grep -q 'B2lMaxTransferMessage' "$HW"
grep -q 'FALSE,' "$HW"
grep -q '(BOOLEAN)(ReadLength == 0UL)' "$HW"
grep -q 'B2L_MAX_INITIAL_POLL_MS      1UL' "$HDR"
grep -q 'B2L_MAX_ATTACH_POLL_MS       20UL' "$HDR"
grep -q 'B2L_MAX_ATTACH_FAST_POLLS    50UL' "$HDR"
grep -q 'B2L_MAX_POLL_MS              200UL' "$HDR"
grep -q 'B2L_MAX_RETRY_POLL_MS        2000UL' "$HDR"
grep -q 'B2L_MAX_IDLE_DIAGNOSTIC_POLLS 10UL' "$HDR"
grep -q 'WDF_TIMER_CONFIG_INIT(&TimerConfig, B2lMaxEvtPollTimer)' "$MAIN"
if grep -q 'WDF_TIMER_CONFIG_INIT_PERIODIC' "$MAIN"; then
  echo 'HATA: pasif MAX77705 timer periyodik WDF timer olarak olusturulamaz' >&2
  exit 1
fi
grep -q 'TimerConfig.AutomaticSerialization = FALSE' "$MAIN"
grep -q 'Context->PollingEnabled = TRUE' "$MAIN"
grep -q 'Context->PollingEnabled = FALSE' "$MAIN"
test "$(rg -c 'WdfTimerStart\(' "$MAIN")" -eq 2
test "$(rg -c 'B2lMaxServiceConnection\(Context\)' "$MAIN")" -eq 1
grep -q 'Context->CcSampleCount <= B2L_MAX_ATTACH_FAST_POLLS' "$MAIN"
grep -q 'B2LMaxCcOpenNormalized' "$MAIN"
grep -q 'B2LMaxControl3Before' "$MAIN"
grep -q 'B2LMaxControl3After' "$MAIN"
grep -q 'CCOPEN_OK B2LMaxCcOpenNormalized' "$ROOT/windows/startnet.cmd"
if sed -n '/B2lMaxEvtD0Entry(WDFDEVICE Device/,/B2lMaxEvtD0Exit(WDFDEVICE Device/p' "$MAIN" | \
   grep -q 'B2lMaxServiceConnection'; then
  echo 'HATA: tek cekirdekte MAX77705 transferi PnP D0Entry yolunu bloklayamaz' >&2
  exit 1
fi
sed -n '/B2lMaxEvtPollTimer(WDFTIMER Timer)/,$p' "$MAIN" | \
  grep -q 'B2L_MAX_RETRY_POLL_MS'
sed -n '/B2lMaxEvtPollTimer(WDFTIMER Timer)/,$p' "$MAIN" | \
  grep -q 'B2L_MAX_ATTACH_POLL_MS'
if sed -n '/B2lMaxEvtD0Entry/,/B2lMaxEvtD0Exit/p' "$MAIN" | grep -q 'return Status'; then
  echo 'HATA: gecici MAX77705 I2C hatasi ACPI parent D0 girisini dusurmemeli' >&2
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
grep -Fq 'B2LMaxChgDetails2' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxAutoVbusDisabled' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxOpcodeResponse' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxAutoIbusConfigured' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxAutoIbusMode' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cControl' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cErrorStatus' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cTransferStatus' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cConfiguration' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cAutoConfiguration' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cAddress' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cRetryCount' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cUsiControl' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cAttemptedAddress' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxHsI2cFailurePhase' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxChgConfig06' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxChargerUnlocked' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxChargerUnlockAttempts' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxUicInterrupt' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCcInterrupt' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxPdInterrupt' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxVdmInterrupt' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxUicInterruptMask' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCcInterruptMask' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxPdInterruptMask' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxVdmInterruptMask' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxUsbcMasksConfigured' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxUsbcRuntimeMasksConfigured' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCcControlConfigured' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCcControlResponse' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCcControlReadBack' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxOtgHardwareActive' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxMuicPathRequested' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxMuicControl1' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxMuicPathVerified' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxMuicUsbPath' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxPlatformPrepared' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCmgpMuxUsi3' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCmgpDivUsi3' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxCmgpQchUsi3' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxGpm12Con' "$ROOT/windows/startnet.cmd"
grep -Fq 'B2LMaxGpm13Con' "$ROOT/windows/startnet.cmd"

echo 'Windows MAX77705 safety checks: PASS'
