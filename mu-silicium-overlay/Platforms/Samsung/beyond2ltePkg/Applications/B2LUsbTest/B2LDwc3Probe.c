/** @file
  Guarded Exynos9820 HS PHY and DWC_usb31 event-DMA probe for beyond2lte.

  This is deliberately not a USB gadget implementation.  It establishes the
  physical attach boundary needed before EP0 and bulk transfer work: the stock
  device-tree's UTMI PHY power/isolation and tune sequence, a controller-owned
  event ring in allocated RAM, an explicit High-Speed device selection and a
  short DCTL.RUN_STOP interval.  No combo
  PHY, DWC3 role/core-reset, endpoint command or endpoint-enable register is
  written.

  The probe refuses to write unless the core is a halted DWC_usb31 in device
  mode with no enabled endpoints, no pending event and no inherited device
  event enable.  The event interrupt stays masked; firmware polls and
  acknowledges the ring.  On a normal stop every touched register is restored
  and the DMA page is freed.  If the controller cannot halt, the page is kept
  reserved and execution intentionally stops so later software cannot reclaim
  memory that may still be a DMA target.
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

#include "B2LDwc3Probe.h"

#define B2L_DWC3_BASE                0x10C00000ULL
#define B2L_USB_PHY_BASE             0x10B00000ULL
#define B2L_PMU_BASE                 0x15860000ULL

#define B2L_PMU_USB_PHY_CTRL         0x072C
#define B2L_PMU_USB_PHY_ENABLE       BIT0

#define EXYNOS_USBCON_CTRL_VER       0x00
#define EXYNOS_USBCON_LINK_CTRL      0x04
#define EXYNOS_USBCON_LINK_PORT      0x08
#define EXYNOS_USBCON_CLKRST         0x20
#define EXYNOS_USBCON_UTMI           0x50
#define EXYNOS_USBCON_HSP            0x54
#define EXYNOS_USBCON_HSP_TUNE       0x58
#define EXYNOS_USBCON_HSP_TEST       0x5C

#define EXYNOS_USBCON_CTRL_VER_03    0x00030000U

#define LINKCTRL_DIS_QACT_LINKGATE   BIT12
#define LINKCTRL_DIS_QACT_ID0        BIT11
#define LINKCTRL_DIS_QACT_VBUS_VALID BIT10
#define LINKCTRL_DIS_QACT_BVALID     BIT9
#define LINKCTRL_FORCE_QACT          BIT8
#define LINKCTRL_BUS_FILTER_BYPASS   (0xFU << 4)
#define LINKCTRL_QACT_DISABLE_MASK   (LINKCTRL_DIS_QACT_LINKGATE | \
                                      LINKCTRL_DIS_QACT_ID0 | \
                                      LINKCTRL_DIS_QACT_VBUS_VALID | \
                                      LINKCTRL_DIS_QACT_BVALID)

#define LINKPORT_HUB_PORT_SEL_OCD_U3 BIT3
#define LINKPORT_HUB_PORT_SEL_OCD_U2 BIT2

#define CLKRST_PHY20_SW_RST          BIT13
#define CLKRST_PHY20_RST_SEL         BIT12
#define CLKRST_PHY30_SW_RST          BIT3
#define CLKRST_PHY30_RST_SEL         BIT2
#define CLKRST_PORT_RST              BIT1

#define UTMI_FORCE_VBUSVALID         BIT5
#define UTMI_FORCE_BVALID            BIT4
#define UTMI_DP_PULLDOWN             BIT3
#define UTMI_DM_PULLDOWN             BIT2
#define UTMI_FORCE_SUSPEND            BIT1
#define UTMI_FORCE_SLEEP              BIT0

#define HSP_VBUSVLDEXTSEL            BIT13
#define HSP_VBUSVLDEXT               BIT12
#define HSP_EN_UTMISUSPEND           BIT9
#define HSP_COMMONONN                BIT8

#define HSP_TUNE_TXVREF_MASK         (0xFU << 28)
#define HSP_TUNE_TXRES_MASK          (0x3U << 21)
#define HSP_TUNE_TXPREEMPA_MASK      (0x3U << 18)
#define HSP_TUNE_SQRX_MASK           (0x7U << 8)
#define HSP_TUNE_COMPDIS_MASK        (0x7U << 0)
#define HSP_TUNE_DEVICE_MASK         (HSP_TUNE_TXVREF_MASK | \
                                      HSP_TUNE_TXRES_MASK | \
                                      HSP_TUNE_TXPREEMPA_MASK | \
                                      HSP_TUNE_SQRX_MASK | \
                                      HSP_TUNE_COMPDIS_MASK)
#define HSP_TUNE_DEVICE_VALUE        ((0xCU << 28) | (0x3U << 21) | \
                                      (0x3U << 18) | (0x3U << 8) | 0x7U)

#define HSP_TEST_SIDDQ               BIT24

#define DWC3_GCTL                    0xC110
#define DWC3_GSTS                    0xC118
#define DWC3_GSNPSID                 0xC120
#define DWC3_GHWPARAMS0              0xC140
#define DWC3_GUSB2PHYCFG0            0xC200
#define DWC3_GEVNTADRLO0             0xC400
#define DWC3_GEVNTADRHI0             0xC404
#define DWC3_GEVNTSIZ0               0xC408
#define DWC3_GEVNTCOUNT0             0xC40C
#define DWC3_DCFG                    0xC700
#define DWC3_DCTL                    0xC704
#define DWC3_DEVTEN                  0xC708
#define DWC3_DSTS                    0xC70C
#define DWC3_DALEPENA                0xC720
#define DWC3_DEPCMD0                 0xC80C
#define DWC3_DEPCMD1                 0xC81C

#define DWC3_GSNPSID_MASK            0xFFFF0000U
#define DWC3_IP_DWC31                0x33310000U
#define DWC3_GCTL_PRTCAP_SHIFT       12
#define DWC3_GCTL_PRTCAP_MASK        3U
#define DWC3_GCTL_PRTCAP_DEVICE      2U
#define DWC3_GCTL_PRTCAP_OTG         3U
#define DWC3_GCTL_CORESOFTRESET      BIT11
#define DWC3_GSTS_CURMOD_MASK        3U
#define DWC3_GSTS_CURMOD_DEVICE      0U
#define DWC3_GHWPARAMS0_AWIDTH_SHIFT 24
#define DWC3_GUSB2PHYCFG_SOFTRST     BIT31
#define DWC3_GUSB2PHYCFG_SUSPHY      BIT6

#define DWC3_GEVNTSIZ_INTMASK        BIT31
#define DWC3_GEVNTSIZ_SIZE_MASK      0xFFFFU
#define DWC3_GEVNTCOUNT_MASK         0xFFFCU
#define DWC3_GEVNTCOUNT_EHB          BIT31

#define DWC3_DCTL_RUN_STOP           BIT31
#define DWC3_DCTL_CSFTRST            BIT30
#define DWC3_DCFG_DEVSPD_MASK        7U
#define DWC3_DCFG_DEVSPD_HS          0U
#define DWC3_DSTS_COREIDLE           BIT23
#define DWC3_DSTS_DEVCTRLHLT         BIT22
#define DWC3_DSTS_RXFIFOEMPTY        BIT17
#define DWC3_DSTS_SOFFN_MASK         (0x3FFFU << 3)
#define DWC3_DSTS_USBLNKST_SHIFT     18
#define DWC3_DSTS_USBLNKST_MASK      (0xFU << DWC3_DSTS_USBLNKST_SHIFT)
#define DWC3_DSTS_CONNECTSPD_MASK    7U
#define DWC3_DEPCMD_CMDACT           BIT10

#define DWC3_DEVTEN_EVNTOVERFLOWEN   BIT11
#define DWC3_DEVTEN_CMDCMPLTEN       BIT10
#define DWC3_DEVTEN_ERRTICERREN      BIT9
#define DWC3_DEVTEN_WKUPEVTEN        BIT4
#define DWC3_DEVTEN_CONNECTDONEEN    BIT2
#define DWC3_DEVTEN_USBRSTEN         BIT1
#define DWC3_DEVTEN_DISCONNEVTEN     BIT0
#define DWC3_PROBE_DEVICE_EVENTS     (DWC3_DEVTEN_EVNTOVERFLOWEN | \
                                      DWC3_DEVTEN_CMDCMPLTEN | \
                                      DWC3_DEVTEN_ERRTICERREN | \
                                      DWC3_DEVTEN_WKUPEVTEN | \
                                      DWC3_DEVTEN_CONNECTDONEEN | \
                                      DWC3_DEVTEN_USBRSTEN | \
                                      DWC3_DEVTEN_DISCONNEVTEN)

#define DWC3_EVENT_BUFFER_SIZE       EFI_PAGE_SIZE
#define DWC3_EVENT_WORD_COUNT        (DWC3_EVENT_BUFFER_SIZE / sizeof (UINT32))
#define DWC3_PROBE_SECONDS           30
#define DWC3_PROBE_LAST_EVENTS       8
#define DWC3_STOP_TIMEOUT_MS         2000

typedef struct {
  UINT32  EventAddressLow;
  UINT32  EventAddressHigh;
  UINT32  EventSize;
  UINT32  DeviceEventEnable;
  UINT32  DeviceConfig;
  UINT32  DeviceControl;
} B2L_DWC3_PROBE_SAVED;

typedef struct {
  UINT32  PmuUsbPhy;
  UINT32  LinkControl;
  UINT32  LinkPort;
  UINT32  ClockReset;
  UINT32  Utmi;
  UINT32  HsPhy;
  UINT32  HsTune;
  UINT32  HsTest;
} B2L_HS_PHY_SAVED;

typedef struct {
  UINTN   ReadPosition;
  UINTN   Total;
  UINTN   EndpointTotal;
  UINTN   OtherTotal;
  UINTN   DeviceType[16];
  UINT32  Last[DWC3_PROBE_LAST_EVENTS];
  UINTN   LastCount;
  UINTN   LastWrite;
} B2L_DWC3_EVENT_STATS;

STATIC
UINTN
Dwc3Register (
  IN UINTN  Offset
  )
{
  return (UINTN)(B2L_DWC3_BASE + Offset);
}

STATIC
UINT32
Dwc3Read32 (
  IN UINTN  Offset
  )
{
  return MmioRead32 (Dwc3Register (Offset));
}

STATIC
UINTN
PhyRegister (
  IN UINTN  Offset
  )
{
  return (UINTN)(B2L_USB_PHY_BASE + Offset);
}

STATIC
UINT32
PhyRead32 (
  IN UINTN  Offset
  )
{
  return MmioRead32 (PhyRegister (Offset));
}

STATIC
UINT32
PmuRead32 (
  IN UINTN  Offset
  )
{
  return MmioRead32 ((UINTN)(B2L_PMU_BASE + Offset));
}

/**
  Runtime write allow-list.  Keep this switch narrower than the mapped DWC3
  page: event ring ownership, device-event enable and soft connect only.
**/
STATIC
BOOLEAN
Dwc3ProbeWrite32 (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  switch (Offset) {
    case DWC3_GEVNTADRLO0:
    case DWC3_GEVNTADRHI0:
    case DWC3_GEVNTSIZ0:
    case DWC3_GEVNTCOUNT0:
    case DWC3_DEVTEN:
    case DWC3_DCFG:
    case DWC3_DCTL:
      MmioWrite32 (Dwc3Register (Offset), Value);
      return TRUE;
    default:
      Print (L"R12 IC HATA: izin verilmeyen DWC3 write offset=%x\n", Offset);
      return FALSE;
  }
}

/** Main PHY write allow-list copied from the Exynos v3.1 UTMI sequence. **/
STATIC
BOOLEAN
PhyProbeWrite32 (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  switch (Offset) {
    case EXYNOS_USBCON_LINK_CTRL:
    case EXYNOS_USBCON_LINK_PORT:
    case EXYNOS_USBCON_CLKRST:
    case EXYNOS_USBCON_UTMI:
    case EXYNOS_USBCON_HSP:
    case EXYNOS_USBCON_HSP_TUNE:
    case EXYNOS_USBCON_HSP_TEST:
      MmioWrite32 (PhyRegister (Offset), Value);
      return TRUE;
    default:
      Print (L"R12 IC HATA: izin verilmeyen PHY write offset=%x\n", Offset);
      return FALSE;
  }
}

/** PMU write allow-list: only the DT-declared USB2 PHY isolation control. **/
STATIC
BOOLEAN
PmuProbeWrite32 (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  if (Offset != B2L_PMU_USB_PHY_CTRL) {
    Print (L"R12 IC HATA: izin verilmeyen PMU write offset=%x\n", Offset);
    return FALSE;
  }

  MmioWrite32 ((UINTN)(B2L_PMU_BASE + Offset), Value);
  return TRUE;
}

STATIC
VOID
SaveHsPhyState (
  OUT B2L_HS_PHY_SAVED  *Saved
  )
{
  Saved->PmuUsbPhy  = PmuRead32 (B2L_PMU_USB_PHY_CTRL);
  Saved->LinkControl = PhyRead32 (EXYNOS_USBCON_LINK_CTRL);
  Saved->LinkPort    = PhyRead32 (EXYNOS_USBCON_LINK_PORT);
  Saved->ClockReset  = PhyRead32 (EXYNOS_USBCON_CLKRST);
  Saved->Utmi        = PhyRead32 (EXYNOS_USBCON_UTMI);
  Saved->HsPhy       = PhyRead32 (EXYNOS_USBCON_HSP);
  Saved->HsTune      = PhyRead32 (EXYNOS_USBCON_HSP_TUNE);
  Saved->HsTest      = PhyRead32 (EXYNOS_USBCON_HSP_TEST);
}

STATIC
BOOLEAN
EnableHsPhy (
  IN CONST B2L_HS_PHY_SAVED  *Saved
  )
{
  UINT32  Value;

  if (!PmuProbeWrite32 (
         B2L_PMU_USB_PHY_CTRL,
         Saved->PmuUsbPhy | B2L_PMU_USB_PHY_ENABLE
         )) {
    return FALSE;
  }

  Value  = PhyRead32 (EXYNOS_USBCON_LINK_CTRL);
  Value |= LINKCTRL_QACT_DISABLE_MASK;
  Value &= ~LINKCTRL_FORCE_QACT;
  gBS->Stall (500);
  if (!PhyProbeWrite32 (EXYNOS_USBCON_LINK_CTRL, Value)) {
    return FALSE;
  }
  gBS->Stall (500);
  Value = PhyRead32 (EXYNOS_USBCON_LINK_CTRL) | LINKCTRL_FORCE_QACT;
  gBS->Stall (500);
  if (!PhyProbeWrite32 (EXYNOS_USBCON_LINK_CTRL, Value)) {
    return FALSE;
  }

  Value = PhyRead32 (EXYNOS_USBCON_CLKRST) |
          CLKRST_PHY20_SW_RST | CLKRST_PHY20_RST_SEL |
          CLKRST_PHY30_SW_RST | CLKRST_PHY30_RST_SEL;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_CLKRST, Value)) {
    return FALSE;
  }

  Value  = PhyRead32 (EXYNOS_USBCON_UTMI);
  Value &= ~(UTMI_FORCE_SUSPEND | UTMI_FORCE_SLEEP |
             UTMI_DP_PULLDOWN | UTMI_DM_PULLDOWN);
  if (!PhyProbeWrite32 (EXYNOS_USBCON_UTMI, Value)) {
    return FALSE;
  }

  Value = PhyRead32 (EXYNOS_USBCON_HSP) |
          HSP_EN_UTMISUSPEND | HSP_COMMONONN;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_HSP, Value)) {
    return FALSE;
  }
  gBS->Stall (100);

  Value = PhyRead32 (EXYNOS_USBCON_LINK_CTRL) |
          LINKCTRL_BUS_FILTER_BYPASS;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_LINK_CTRL, Value)) {
    return FALSE;
  }

  Value = PhyRead32 (EXYNOS_USBCON_UTMI) |
          UTMI_FORCE_BVALID | UTMI_FORCE_VBUSVALID;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_UTMI, Value)) {
    return FALSE;
  }
  Value = PhyRead32 (EXYNOS_USBCON_HSP) |
          HSP_VBUSVLDEXTSEL | HSP_VBUSVLDEXT;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_HSP, Value)) {
    return FALSE;
  }

  // Device-mode values are the first cells in beyond2lte hs_tune entries.
  Value  = PhyRead32 (EXYNOS_USBCON_HSP_TUNE);
  Value &= ~HSP_TUNE_DEVICE_MASK;
  Value |= HSP_TUNE_DEVICE_VALUE;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_HSP_TUNE, Value)) {
    return FALSE;
  }

  Value = PhyRead32 (EXYNOS_USBCON_HSP_TEST) & ~HSP_TEST_SIDDQ;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_HSP_TEST, Value)) {
    return FALSE;
  }
  gBS->Stall (10);

  Value  = PhyRead32 (EXYNOS_USBCON_CLKRST);
  Value |= CLKRST_PHY20_RST_SEL;
  Value &= ~(CLKRST_PHY20_SW_RST | CLKRST_PHY30_SW_RST |
             CLKRST_PORT_RST);
  if (!PhyProbeWrite32 (EXYNOS_USBCON_CLKRST, Value)) {
    return FALSE;
  }
  gBS->Stall (75);

  Value = PhyRead32 (EXYNOS_USBCON_LINK_PORT) |
          LINKPORT_HUB_PORT_SEL_OCD_U3 | LINKPORT_HUB_PORT_SEL_OCD_U2;
  if (!PhyProbeWrite32 (EXYNOS_USBCON_LINK_PORT, Value)) {
    return FALSE;
  }

  return ((PmuRead32 (B2L_PMU_USB_PHY_CTRL) &
           B2L_PMU_USB_PHY_ENABLE) != 0) &&
         ((PhyRead32 (EXYNOS_USBCON_HSP_TEST) & HSP_TEST_SIDDQ) == 0);
}

STATIC
VOID
RestoreHsPhy (
  IN CONST B2L_HS_PHY_SAVED  *Saved
  )
{
  UINT32  Value;

  // First put the live UTMI block into the vendor driver's disabled state.
  Value = PhyRead32 (EXYNOS_USBCON_UTMI) |
          UTMI_FORCE_SUSPEND | UTMI_FORCE_SLEEP;
  PhyProbeWrite32 (EXYNOS_USBCON_UTMI, Value);
  Value = PhyRead32 (EXYNOS_USBCON_HSP_TEST) | HSP_TEST_SIDDQ;
  PhyProbeWrite32 (EXYNOS_USBCON_HSP_TEST, Value);
  Value = PhyRead32 (EXYNOS_USBCON_LINK_CTRL) & ~LINKCTRL_FORCE_QACT;
  PhyProbeWrite32 (EXYNOS_USBCON_LINK_CTRL, Value);

  // Isolate before restoring the exact inherited main-PHY register image.
  PmuProbeWrite32 (B2L_PMU_USB_PHY_CTRL, Saved->PmuUsbPhy);
  gBS->Stall (10);
  PhyProbeWrite32 (EXYNOS_USBCON_HSP_TEST, Saved->HsTest);
  PhyProbeWrite32 (EXYNOS_USBCON_HSP_TUNE, Saved->HsTune);
  PhyProbeWrite32 (EXYNOS_USBCON_HSP, Saved->HsPhy);
  PhyProbeWrite32 (EXYNOS_USBCON_UTMI, Saved->Utmi);
  PhyProbeWrite32 (EXYNOS_USBCON_CLKRST, Saved->ClockReset);
  PhyProbeWrite32 (EXYNOS_USBCON_LINK_PORT, Saved->LinkPort);
  PhyProbeWrite32 (EXYNOS_USBCON_LINK_CTRL, Saved->LinkControl);
}

STATIC
CONST CHAR16 *
Dwc3LinkStateName (
  IN UINT32  Dsts
  )
{
  switch ((Dsts & DWC3_DSTS_USBLNKST_MASK) >>
          DWC3_DSTS_USBLNKST_SHIFT) {
    case 0:
      return L"U0";
    case 1:
      return L"U1";
    case 2:
      return L"U2";
    case 3:
      return L"U3";
    case 4:
      return L"SS_DIS";
    case 5:
      return L"RX_DET";
    case 6:
      return L"SS_INACT";
    case 7:
      return L"POLL";
    case 8:
      return L"RECOV";
    case 9:
      return L"HOT_RST";
    case 10:
      return L"COMP";
    case 11:
      return L"LOOPBACK";
    case 14:
      return L"RESET";
    case 15:
      return L"RESUME";
    default:
      return L"UNKNOWN";
  }
}

STATIC
CONST CHAR16 *
Dwc3SpeedName (
  IN UINT32  Dsts
  )
{
  switch (Dsts & DWC3_DSTS_CONNECTSPD_MASK) {
    case 0:
      return L"HS";
    case 1:
      return L"FS";
    case 2:
      return L"LS";
    case 3:
      return L"FS48";
    case 4:
      return L"SS";
    case 5:
      return L"SSP";
    default:
      return L"UNKNOWN";
  }
}

STATIC
CONST CHAR16 *
DeviceEventName (
  IN UINTN  Type
  )
{
  switch (Type) {
    case 0:
      return L"DISCONNECT";
    case 1:
      return L"USB_RESET";
    case 2:
      return L"CONNECT_DONE";
    case 3:
      return L"LINK_CHANGE";
    case 4:
      return L"WAKEUP";
    case 6:
      return L"SUSPEND";
    case 7:
      return L"SOF";
    case 9:
      return L"ERRATIC";
    case 10:
      return L"CMD_COMPLETE";
    case 11:
      return L"OVERFLOW";
    default:
      return L"OTHER";
  }
}

STATIC
CONST CHAR16 *
EndpointEventName (
  IN UINTN  Type
  )
{
  switch (Type) {
    case 1:
      return L"XFER_COMPLETE";
    case 2:
      return L"XFER_PROGRESS";
    case 3:
      return L"XFER_NOT_READY";
    case 4:
      return L"FIFO_EVENT";
    case 6:
      return L"STREAM_EVENT";
    case 7:
      return L"EPCMD_COMPLETE";
    default:
      return L"UNKNOWN";
  }
}

STATIC
VOID
RememberEvent (
  IN OUT B2L_DWC3_EVENT_STATS  *Stats,
  IN     UINT32                 Event
  )
{
  UINTN  Type;

  Stats->Last[Stats->LastWrite] = Event;
  Stats->LastWrite = (Stats->LastWrite + 1) % DWC3_PROBE_LAST_EVENTS;
  if (Stats->LastCount < DWC3_PROBE_LAST_EVENTS) {
    Stats->LastCount++;
  }

  Stats->Total++;
  if ((Event & BIT0) != 0) {
    Stats->EndpointTotal++;
    return;
  }

  if (((Event >> 1) & 0x7F) != 0) {
    Stats->OtherTotal++;
    return;
  }

  Type = (Event >> 8) & 0xF;
  Stats->DeviceType[Type]++;
}

STATIC
UINT32
DrainEventBuffer (
  IN     EFI_PHYSICAL_ADDRESS   BufferAddress,
  IN OUT B2L_DWC3_EVENT_STATS  *Stats
  )
{
  volatile UINT32  *Events;
  UINT32            CountRegister;
  UINT32            EventBytes;
  UINTN             Index;
  UINTN             WordCount;

  CountRegister = Dwc3Read32 (DWC3_GEVNTCOUNT0);
  EventBytes    = CountRegister & DWC3_GEVNTCOUNT_MASK;
  if (EventBytes == 0) {
    return CountRegister;
  }

  if ((EventBytes > DWC3_EVENT_BUFFER_SIZE) ||
      ((EventBytes & (sizeof (UINT32) - 1)) != 0)) {
    Print (L"R12 EVENT COUNT gecersiz: %08x\n", CountRegister);
    return CountRegister;
  }

  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)BufferAddress,
    DWC3_EVENT_BUFFER_SIZE
    );
  Events    = (volatile UINT32 *)(UINTN)BufferAddress;
  WordCount = EventBytes / sizeof (UINT32);
  for (Index = 0; Index < WordCount; Index++) {
    RememberEvent (Stats, Events[Stats->ReadPosition]);
    Stats->ReadPosition = (Stats->ReadPosition + 1) % DWC3_EVENT_WORD_COUNT;
  }

  Dwc3ProbeWrite32 (DWC3_GEVNTCOUNT0, EventBytes);
  return CountRegister;
}

STATIC
VOID
PrintLastEvents (
  IN CONST B2L_DWC3_EVENT_STATS  *Stats
  )
{
  UINTN  Index;
  UINTN  Position;
  UINTN  Type;

  Print (L"SON OLAYLAR:");
  for (Index = 0; Index < Stats->LastCount; Index++) {
    Position = (Stats->LastWrite + DWC3_PROBE_LAST_EVENTS -
                Stats->LastCount + Index) % DWC3_PROBE_LAST_EVENTS;
    Print (L" %08x", Stats->Last[Position]);
  }
  Print (L"\n");

  if (Stats->LastCount == 0) {
    Print (L"SON TIP: henuz olay yok\n");
    return;
  }

  Position = (Stats->LastWrite + DWC3_PROBE_LAST_EVENTS - 1) %
             DWC3_PROBE_LAST_EVENTS;
  if ((Stats->Last[Position] & BIT0) != 0) {
    Type = (Stats->Last[Position] >> 6) & 0xF;
    Print (L"SON TIP: ENDPOINT ep=%u %s(%u)\n",
           (Stats->Last[Position] >> 1) & 0x1F,
           EndpointEventName (Type), Type);
  } else if (((Stats->Last[Position] >> 1) & 0x7F) == 0) {
    Type = (Stats->Last[Position] >> 8) & 0xF;
    Print (L"SON TIP: DEVICE %s(%u) bilgi=%x\n",
           DeviceEventName (Type), Type,
           (Stats->Last[Position] >> 16) & 0x1FF);
  } else {
    Print (L"SON TIP: DIGER core event\n");
  }
}

STATIC
BOOLEAN
WaitOneSecondOrQuit (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Slice;

  for (Slice = 0; Slice < 10; Slice++) {
    if ((gST->ConIn != NULL) && (gST->ConIn->ReadKeyStroke != NULL)) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (!EFI_ERROR (Status) &&
          ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == L'q') ||
           (Key.UnicodeChar == L'Q'))) {
        return FALSE;
      }
    }
    gBS->Stall (100000);
  }
  return TRUE;
}

STATIC
BOOLEAN
PreflightIsSafe (
  IN EFI_PHYSICAL_ADDRESS  BufferAddress
  )
{
  UINT32  AddressWidth;
  UINT32  CoreId;
  UINT32  Dcfg;
  UINT32  Dctl;
  UINT32  Dsts;
  UINT32  EventCount;
  UINT32  Gctl;
  UINT32  Gsts;
  UINT32  HwParams0;
  UINT32  PortCapability;
  UINT32  PmuUsbPhy;
  UINT32  PhyVersion;
  UINT32  Usb2PhyCfg;

  CoreId         = Dwc3Read32 (DWC3_GSNPSID);
  Gctl           = Dwc3Read32 (DWC3_GCTL);
  Gsts           = Dwc3Read32 (DWC3_GSTS);
  HwParams0      = Dwc3Read32 (DWC3_GHWPARAMS0);
  Dcfg           = Dwc3Read32 (DWC3_DCFG);
  Dctl           = Dwc3Read32 (DWC3_DCTL);
  Dsts           = Dwc3Read32 (DWC3_DSTS);
  EventCount     = Dwc3Read32 (DWC3_GEVNTCOUNT0);
  AddressWidth   = HwParams0 >> DWC3_GHWPARAMS0_AWIDTH_SHIFT;
  PortCapability = (Gctl >> DWC3_GCTL_PRTCAP_SHIFT) &
                   DWC3_GCTL_PRTCAP_MASK;
  PmuUsbPhy      = PmuRead32 (B2L_PMU_USB_PHY_CTRL);
  PhyVersion     = PhyRead32 (EXYNOS_USBCON_CTRL_VER);
  Usb2PhyCfg     = Dwc3Read32 (DWC3_GUSB2PHYCFG0);

  Print (L"R12 PREFLIGHT ID=%08x GCTL=%08x GSTS=%08x HWP0=%08x AW=%u\n",
         CoreId, Gctl, Gsts, HwParams0, AddressWidth);
  Print (L"DCFG=%08x DEVSPD=%u DCTL=%08x DSTS=%08x\n",
         Dcfg, Dcfg & DWC3_DCFG_DEVSPD_MASK, Dctl, Dsts);
  Print (L"DEVTEN=%08x DALEPENA=%08x\n",
         Dwc3Read32 (DWC3_DEVTEN),
         Dwc3Read32 (DWC3_DALEPENA));
  Print (L"EVT=%08x:%08x SIZ=%08x CNT=%08x EP0CMD=%08x EP1CMD=%08x\n",
         Dwc3Read32 (DWC3_GEVNTADRHI0),
         Dwc3Read32 (DWC3_GEVNTADRLO0),
         Dwc3Read32 (DWC3_GEVNTSIZ0), EventCount,
         Dwc3Read32 (DWC3_DEPCMD0), Dwc3Read32 (DWC3_DEPCMD1));
  Print (L"AYRILAN EVENT DMA=%lx boyut=%u\n",
         BufferAddress, DWC3_EVENT_BUFFER_SIZE);
  Print (L"PHYVER=%08x PMU_USBPHY=%08x GUSB2PHYCFG=%08x\n",
         PhyVersion, PmuUsbPhy, Usb2PhyCfg);

  if ((CoreId & DWC3_GSNPSID_MASK) != DWC3_IP_DWC31) {
    Print (L"RED: cekirdek DWC_usb31 degil.\n");
    return FALSE;
  }
  if ((Gctl & DWC3_GCTL_CORESOFTRESET) != 0) {
    Print (L"RED: global core soft-reset aktif.\n");
    return FALSE;
  }
  if ((PortCapability != DWC3_GCTL_PRTCAP_DEVICE) &&
      (PortCapability != DWC3_GCTL_PRTCAP_OTG)) {
    Print (L"RED: port capability device/OTG degil.\n");
    return FALSE;
  }
  if ((Gsts & DWC3_GSTS_CURMOD_MASK) != DWC3_GSTS_CURMOD_DEVICE) {
    Print (L"RED: cekirdek device modunda degil.\n");
    return FALSE;
  }
  if (((Dctl & (DWC3_DCTL_RUN_STOP | DWC3_DCTL_CSFTRST)) != 0) ||
      ((Dsts & DWC3_DSTS_DEVCTRLHLT) == 0)) {
    Print (L"RED: device zaten aktif/resetli veya halted degil.\n");
    return FALSE;
  }
  if (Dwc3Read32 (DWC3_DALEPENA) != 0) {
    Print (L"RED: endpoint etkin; stale TRB DMA riski.\n");
    return FALSE;
  }
  if ((Dwc3Read32 (DWC3_DEPCMD0) & DWC3_DEPCMD_CMDACT) != 0 ||
      (Dwc3Read32 (DWC3_DEPCMD1) & DWC3_DEPCMD_CMDACT) != 0) {
    Print (L"RED: EP0/EP1 komutu halen aktif.\n");
    return FALSE;
  }
  if (Dwc3Read32 (DWC3_DEVTEN) != 0) {
    Print (L"RED: kalitilmis device-event enable sifir degil.\n");
    return FALSE;
  }
  if ((EventCount & (DWC3_GEVNTCOUNT_MASK | DWC3_GEVNTCOUNT_EHB)) != 0) {
    Print (L"RED: event buffer count/busy sifir degil.\n");
    return FALSE;
  }
  if (((BufferAddress >> 32) != 0) && (AddressWidth < 64)) {
    Print (L"RED: event DMA 4GiB ustunde fakat core AWIDTH=%u.\n",
           AddressWidth);
    return FALSE;
  }
  if (PhyVersion != EXYNOS_USBCON_CTRL_VER_03) {
    Print (L"RED: ana PHY Exynos USB v3.1 (0x00030000) degil.\n");
    return FALSE;
  }
  if ((PmuUsbPhy & B2L_PMU_USB_PHY_ENABLE) != 0) {
    Print (L"RED: USB2 PHY zaten de-isole; kalitilmis sahibi bilinmiyor.\n");
    return FALSE;
  }
  if ((Usb2PhyCfg &
       (DWC3_GUSB2PHYCFG_SOFTRST | DWC3_GUSB2PHYCFG_SUSPHY)) != 0) {
    Print (L"RED: DWC3 USB2 PHY soft-reset/suspend aktif.\n");
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
ShowLiveProbe (
  IN UINTN                         Second,
  IN UINT32                        CountRegister,
  IN CONST B2L_DWC3_EVENT_STATS   *Stats
  )
{
  UINT32  Dctl;
  UINT32  Dcfg;
  UINT32  Dsts;
  UINT32  HsTest;
  UINT32  PmuUsbPhy;

  Dctl = Dwc3Read32 (DWC3_DCTL);
  Dcfg = Dwc3Read32 (DWC3_DCFG);
  Dsts = Dwc3Read32 (DWC3_DSTS);
  HsTest = PhyRead32 (EXYNOS_USBCON_HSP_TEST);
  PmuUsbPhy = PmuRead32 (B2L_PMU_USB_PHY_CTRL);
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - AKTIF DWC3 EVENT PROBU ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"SANIYE=%u/%u  Q/ESC=guvenli durdur  KABLO PC'YE TAKILI\n",
         Second, DWC3_PROBE_SECONDS);
  Print (L"YAZMA: stok HS PHY + PMU[72C].0 + DCFG.DEVSPD=HS + event DMA.\n");
  Print (L"DWC3 rol/core-reset/endpoint yazmasi ve combo PHY erisimi YOK.\n\n");
  Print (L"PMU_USBPHY=%08x ON=%u HSP_TEST=%08x SIDDQ=%u\n",
         PmuUsbPhy, (PmuUsbPhy & B2L_PMU_USB_PHY_ENABLE) != 0,
         HsTest, (HsTest & HSP_TEST_SIDDQ) != 0);
  Print (L"DCFG=%08x DEVSPD=%u DCTL=%08x RUN=%u DSTS=%08x SOFN=%04x\n",
         Dcfg, Dcfg & DWC3_DCFG_DEVSPD_MASK,
         Dctl, (Dctl & DWC3_DCTL_RUN_STOP) != 0, Dsts,
         (Dsts & DWC3_DSTS_SOFFN_MASK) >> 3);
  Print (L"SPD=%s LINK=%s HLT=%u IDLE=%u RXEMPTY=%u\n",
         Dwc3SpeedName (Dsts), Dwc3LinkStateName (Dsts),
         (Dsts & DWC3_DSTS_DEVCTRLHLT) != 0,
         (Dsts & DWC3_DSTS_COREIDLE) != 0,
         (Dsts & DWC3_DSTS_RXFIFOEMPTY) != 0);
  Print (L"DEVTEN=%08x DALEPENA=%08x EVTCNT=%08x READPOS=%u\n",
         Dwc3Read32 (DWC3_DEVTEN), Dwc3Read32 (DWC3_DALEPENA),
         CountRegister, Stats->ReadPosition);
  Print (L"TOPLAM=%u DEVICE=%u EP=%u DIGER=%u\n",
         Stats->Total, Stats->Total - Stats->EndpointTotal - Stats->OtherTotal,
         Stats->EndpointTotal, Stats->OtherTotal);
  Print (L"DIS=%u RST=%u CONN=%u LINK=%u WAKE=%u SUSP=%u\n",
         Stats->DeviceType[0], Stats->DeviceType[1], Stats->DeviceType[2],
         Stats->DeviceType[3], Stats->DeviceType[4], Stats->DeviceType[6]);
  Print (L"ERR=%u CMD=%u OVF=%u\n",
         Stats->DeviceType[9], Stats->DeviceType[10], Stats->DeviceType[11]);
  PrintLastEvents (Stats);
  Print (L"\nBeklenen ilk kanit: USB_RESET ve/veya CONNECT_DONE sayaci artar.\n");
  Print (L"FIFO_EVENT EP0 hazir olmadigini gosterir; attach kaniti degildir.\n");
  Print (L"Bu tur descriptor/EP0 sunmaz; hedef yalniz fiziksel USB_RESET.\n");
}

STATIC
VOID
FailStoppedWithReservedDma (
  IN EFI_PHYSICAL_ADDRESS        BufferAddress,
  IN OUT B2L_DWC3_EVENT_STATS  *Stats
  )
{
  Print (L"\nKRITIK: DWC3 2 saniyede halt olmadi.\n");
  Print (L"DMA sayfasi %lx RESERVED birakildi; Windows'a devam ETME.\n",
         BufferAddress);
  Print (L"Telefonu donanim tusuyla yeniden baslat. Bu ekran bilerek cikmaz.\n");
  for (;;) {
    DrainEventBuffer (BufferAddress, Stats);
    gBS->Stall (100000);
  }
}

EFI_STATUS
B2LDwc3RunEventProbe (
  VOID
  )
{
  B2L_DWC3_EVENT_STATS  Stats;
  B2L_DWC3_PROBE_SAVED Saved;
  B2L_HS_PHY_SAVED     PhySaved;
  EFI_PHYSICAL_ADDRESS BufferAddress;
  EFI_STATUS           Status;
  UINT32               CountRegister;
  UINT32               Dctl;
  UINT32               Dsts;
  UINTN                Millisecond;
  UINTN                Second;
  BOOLEAN              PhyStateSaved;

  // Keep the DMA target below 4 GiB even though this core reports AWIDTH=64.
  // That avoids depending on an undocumented Exynos interconnect high-address
  // path during the first active probe.
  BufferAddress = MAX_UINT32;
  Status = gBS->AllocatePages (
                  AllocateMaxAddress,
                  EfiReservedMemoryType,
                  EFI_SIZE_TO_PAGES (DWC3_EVENT_BUFFER_SIZE),
                  &BufferAddress
                  );
  if (EFI_ERROR (Status)) {
    Print (L"R12 event DMA sayfasi ayrilamadi: %r\n", Status);
    return Status;
  }

  ZeroMem ((VOID *)(UINTN)BufferAddress, DWC3_EVENT_BUFFER_SIZE);
  ZeroMem (&Stats, sizeof (Stats));
  ZeroMem (&PhySaved, sizeof (PhySaved));
  PhyStateSaved = FALSE;
  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)BufferAddress,
    DWC3_EVENT_BUFFER_SIZE
    );

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - AKTIF DWC3 PREFLIGHT ===\n\n",
         B2L_USB_TEST_REVISION);
  if (!PreflightIsSafe (BufferAddress)) {
    gBS->FreePages (
           BufferAddress,
           EFI_SIZE_TO_PAGES (DWC3_EVENT_BUFFER_SIZE)
           );
    Print (L"R12 yazma yapmadan iptal edildi.\n");
    return EFI_NOT_READY;
  }

  Saved.EventAddressLow  = Dwc3Read32 (DWC3_GEVNTADRLO0);
  Saved.EventAddressHigh = Dwc3Read32 (DWC3_GEVNTADRHI0);
  Saved.EventSize        = Dwc3Read32 (DWC3_GEVNTSIZ0);
  Saved.DeviceEventEnable = Dwc3Read32 (DWC3_DEVTEN);
  Saved.DeviceConfig      = Dwc3Read32 (DWC3_DCFG);
  Saved.DeviceControl     = Dwc3Read32 (DWC3_DCTL);
  SaveHsPhyState (&PhySaved);
  PhyStateSaved = TRUE;

  if (!EnableHsPhy (&PhySaved)) {
    Print (L"R12 HS PHY enable dogrulamasi basarisiz.\n");
    Status = EFI_DEVICE_ERROR;
    goto RestoreHaltedController;
  }

  if (!Dwc3ProbeWrite32 (
         DWC3_DCFG,
         (Saved.DeviceConfig & ~DWC3_DCFG_DEVSPD_MASK) |
         DWC3_DCFG_DEVSPD_HS
         ) ||
      !Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, (UINT32)BufferAddress) ||
      !Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, (UINT32)(BufferAddress >> 32)) ||
      !Dwc3ProbeWrite32 (
         DWC3_GEVNTSIZ0,
         DWC3_GEVNTSIZ_INTMASK |
         (DWC3_EVENT_BUFFER_SIZE & DWC3_GEVNTSIZ_SIZE_MASK)
         ) ||
      !Dwc3ProbeWrite32 (DWC3_DEVTEN, DWC3_PROBE_DEVICE_EVENTS) ||
      !Dwc3ProbeWrite32 (
         DWC3_DCTL,
         Saved.DeviceControl | DWC3_DCTL_RUN_STOP
         )) {
    Print (L"R12 internal write allow-list hatasi.\n");
    Status = EFI_ACCESS_DENIED;
    goto RestoreHaltedController;
  }

  Status = EFI_SUCCESS;
  for (Second = 1; Second <= DWC3_PROBE_SECONDS; Second++) {
    CountRegister = DrainEventBuffer (BufferAddress, &Stats);
    ShowLiveProbe (Second, CountRegister, &Stats);
    if (!WaitOneSecondOrQuit ()) {
      break;
    }
  }

  Dctl = Dwc3Read32 (DWC3_DCTL);
  Dwc3ProbeWrite32 (DWC3_DCTL, Dctl & ~DWC3_DCTL_RUN_STOP);
  for (Millisecond = 0; Millisecond < DWC3_STOP_TIMEOUT_MS; Millisecond++) {
    DrainEventBuffer (BufferAddress, &Stats);
    Dsts = Dwc3Read32 (DWC3_DSTS);
    if ((Dsts & DWC3_DSTS_DEVCTRLHLT) != 0) {
      break;
    }
    gBS->Stall (1000);
  }
  if ((Dsts & DWC3_DSTS_DEVCTRLHLT) == 0) {
    FailStoppedWithReservedDma (BufferAddress, &Stats);
  }

RestoreHaltedController:
  Dwc3ProbeWrite32 (DWC3_DEVTEN, 0);
  DrainEventBuffer (BufferAddress, &Stats);
  Dwc3ProbeWrite32 (DWC3_GEVNTSIZ0, DWC3_GEVNTSIZ_INTMASK);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, 0);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, 0);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, Saved.EventAddressLow);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, Saved.EventAddressHigh);
  Dwc3ProbeWrite32 (DWC3_GEVNTSIZ0, Saved.EventSize);
  Dwc3ProbeWrite32 (DWC3_DEVTEN, Saved.DeviceEventEnable);
  Dwc3ProbeWrite32 (DWC3_DCFG, Saved.DeviceConfig);
  Dwc3ProbeWrite32 (DWC3_DCTL, Saved.DeviceControl);
  if (PhyStateSaved) {
    RestoreHsPhy (&PhySaved);
  }
  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)BufferAddress,
    DWC3_EVENT_BUFFER_SIZE
    );
  gBS->FreePages (
         BufferAddress,
         EFI_SIZE_TO_PAGES (DWC3_EVENT_BUFFER_SIZE)
         );

  Print (L"\nR12 PROB BITTI: HLT=%u olay=%u; DCFG/DMA/PHY/PMU geri alindi.\n",
         (Dwc3Read32 (DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) != 0,
         Stats.Total);
  Print (L"Sonraki adim icin bu ekranin ve canli ekranin fotografini cek.\n");
  return Status;
}
