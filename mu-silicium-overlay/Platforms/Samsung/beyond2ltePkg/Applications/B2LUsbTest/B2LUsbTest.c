/** @file
  Staged USB diagnostic for Samsung beyond2lte.

  R11 keeps the R10 passive MMIO diagnostics.  Its monitor refreshes
  every two seconds so cable-attached and cable-removed states can be captured
  in the same boot.  A final, separately confirmed helper temporarily applies
  the stock Exynos9820 UTMI PHY/PMU sequence, owns a reserved event DMA page and
  briefly sets DCTL.RUN_STOP.  It never writes the combo PHY, DWC3 role/core
  reset or endpoint registers.  Every new MMIO window is opened only after an
  explicit key press, so a fault can be localized from the last page.

  Address contract (merged stock beyond2lte DT):
    DWC3/xHCI       0x10C00000 + 0x10000, SPI 241 (GSIV 273)
    USB DRD PHY     0x10B00000 + 0x00200
    combo PHY PCS   0x10AE0000 + 0x02800
    combo PHY PMA   0x10AF0000 + 0x00800
    PMU syscon      0x15860000; USB PHY offsets 0x72C and 0x704
    FSYS0A PD       0x15862200 + 0x20
    HSI2C45         0x15D60000 + 0x1000, retained for the completed R8 probe
**/

#include <Uefi.h>

#include <Library/IoLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

#include "B2LDwc3Probe.h"

#define B2L_DWC3_BASE               0x10C00000ULL
#define B2L_USB_PHY_BASE            0x10B00000ULL
#define B2L_USB_PCS_BASE            0x10AE0000ULL
#define B2L_USB_PMA_BASE            0x10AF0000ULL
#define B2L_PMU_BASE                0x15860000ULL
#define B2L_PD_FSYS0A_BASE          0x15862200ULL

#define B2L_PMU_USBDP_CTRL          0x0704
#define B2L_PMU_USB_PHY_CTRL        0x072C
#define B2L_USB_PCS_ABORT_OFFSET_0  0x0000
#define B2L_USB_PCS_ABORT_OFFSET_C  0x000C
#define B2L_USB_PMA_ABORT_OFFSET_0  0x0000

#define DWC3_GSBUSCFG0              0xC100
#define DWC3_GSBUSCFG1              0xC104
#define DWC3_GCTL                   0xC110
#define DWC3_GSTS                   0xC118
#define DWC3_GUCTL1                 0xC11C
#define DWC3_GSNPSID                0xC120
#define DWC3_GUID                   0xC128
#define DWC3_GUCTL                  0xC12C
#define DWC3_GHWPARAMS0             0xC140
#define DWC3_GHWPARAMS1             0xC144
#define DWC3_GHWPARAMS2             0xC148
#define DWC3_GHWPARAMS3             0xC14C
#define DWC3_VER_NUMBER             0xC1A0
#define DWC3_VER_TYPE               0xC1A4
#define DWC3_GUSB2PHYCFG0           0xC200
#define DWC3_GUSB3PIPECTL0          0xC2C0
#define DWC3_DCFG                   0xC700
#define DWC3_DCTL                   0xC704
#define DWC3_DSTS                   0xC70C
#define DWC3_OCFG                   0xCC00
#define DWC3_OCTL                   0xCC04
#define DWC3_OEVT                   0xCC08
#define DWC3_OEVTEN                 0xCC0C
#define DWC3_OSTS                   0xCC10

#define DWC3_GCTL_PRTCAP_SHIFT      12
#define DWC3_GCTL_PRTCAP_MASK       3U
#define DWC3_GCTL_CORESOFTRESET     (1U << 11)
#define DWC3_GSTS_CURMOD_MASK       3U
#define DWC3_GUSB2PHYCFG_SOFTRST    (1U << 31)
#define DWC3_GUSB2PHYCFG_SUSPHY     (1U << 6)
#define DWC3_GUSB3PIPECTL_SOFTRST   (1U << 31)
#define DWC3_GUSB3PIPECTL_SUSPHY    (1U << 17)
#define DWC3_GSNPSID_MASK           0xFFFF0000U
#define DWC3_IP_DWC3                0x55330000U
#define DWC3_IP_DWC31               0x33310000U
#define DWC3_IP_DWC32               0x33320000U

#define XHCI_HCSPARAMS1             0x04
#define XHCI_HCSPARAMS2             0x08
#define XHCI_HCSPARAMS3             0x0C
#define XHCI_HCCPARAMS1             0x10
#define XHCI_DBOFF                  0x14
#define XHCI_RTSOFF                 0x18
#define XHCI_HCCPARAMS2             0x1C
#define XHCI_USBCMD                 0x00
#define XHCI_USBSTS                 0x04
#define XHCI_PAGESIZE               0x08
#define XHCI_DNCTRL                 0x14
#define XHCI_CRCR_LO                0x18
#define XHCI_CRCR_HI                0x1C
#define XHCI_DCBAAP_LO              0x30
#define XHCI_DCBAAP_HI              0x34
#define XHCI_CONFIG                 0x38
#define XHCI_PORTSC                 0x400
#define XHCI_PORT_STRIDE            0x10

#define XHCI_USBSTS_HCH             (1U << 0)
#define XHCI_USBSTS_HSE             (1U << 2)
#define XHCI_USBSTS_PCD             (1U << 4)
#define XHCI_USBSTS_CNR             (1U << 11)
#define XHCI_USBSTS_HCE             (1U << 12)
#define XHCI_PORTSC_CCS             (1U << 0)
#define XHCI_PORTSC_PED             (1U << 1)
#define XHCI_PORTSC_OCA             (1U << 3)
#define XHCI_PORTSC_PP              (1U << 9)
#define XHCI_PORTSC_CSC             (1U << 17)
#define XHCI_PORTSC_PEC             (1U << 18)
#define XHCI_PORTSC_WRC             (1U << 19)
#define XHCI_PORTSC_OCC             (1U << 20)
#define XHCI_PORTSC_PRC             (1U << 21)
#define XHCI_PORTSC_PLC             (1U << 22)
#define XHCI_PORTSC_CEC             (1U << 23)
#define XHCI_PORTSC_CAS             (1U << 24)
#define XHCI_PORTSC_CHANGE_MASK     (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | \
                                     XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | \
                                     XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | \
                                     XHCI_PORTSC_CEC)

#define DWC3_DCTL_RUN_STOP          (1U << 31)
#define DWC3_DSTS_CONNECTSPD_MASK   7U
#define DWC3_DSTS_RXFIFOEMPTY       (1U << 17)
#define DWC3_DSTS_SOFFN_MASK        (0x3FFFU << 3)
#define DWC3_DSTS_USBLNKST_SHIFT    18
#define DWC3_DSTS_USBLNKST_MASK     (0xFU << DWC3_DSTS_USBLNKST_SHIFT)
#define DWC3_DSTS_DEVCTRLHLT        (1U << 22)
#define DWC3_DSTS_COREIDLE          (1U << 23)

#define DWC3_OSTS_CONIDSTS          (1U << 0)
#define DWC3_OSTS_VBUSVLD           (1U << 1)
#define DWC3_OSTS_BSESVLD           (1U << 2)
#define DWC3_OSTS_XHCIPRTPOWER      (1U << 3)
#define DWC3_OSTS_PERIPHERALSTATE   (1U << 4)

#define B2L_USB_PHY_SAMPLE_COUNT    16

typedef struct {
  UINT32  PmuUsbDp;
  UINT32  PmuUsbPhy;
  UINT32  Gctl;
  UINT32  Gsts;
  UINT32  Usb2PhyCfg;
  UINT32  Usb3PipeCtl;
  UINT32  Dcfg;
  UINT32  Dctl;
  UINT32  Dsts;
  UINT32  Ocfg;
  UINT32  Octl;
  UINT32  Oevt;
  UINT32  Oevten;
  UINT32  Osts;
  UINT32  XhciUsbSts;
  UINT32  PortSc[2];
  UINT32  Phy[B2L_USB_PHY_SAMPLE_COUNT];
  BOOLEAN XhciValid;
} B2L_USB_ATTACH_SNAPSHOT;

STATIC
UINT32
Read32 (
  IN UINTN  Base,
  IN UINTN  Offset
  )
{
  return MmioRead32 (Base + Offset);
}

STATIC
CONST CHAR16 *
PortCapabilityName (
  IN UINT32  Value
  )
{
  switch (Value) {
    case 1:
      return L"HOST";
    case 2:
      return L"DEVICE";
    case 3:
      return L"OTG";
    default:
      return L"RESERVED";
  }
}

STATIC
CONST CHAR16 *
CurrentModeName (
  IN UINT32  Value
  )
{
  switch (Value) {
    case 0:
      return L"DEVICE";
    case 1:
      return L"HOST";
    default:
      return L"UNKNOWN";
  }
}

STATIC
CONST CHAR16 *
CoreIdName (
  IN UINT32  Id
  )
{
  switch (Id & DWC3_GSNPSID_MASK) {
    case DWC3_IP_DWC3:
      return L"DWC_USB3";
    case DWC3_IP_DWC31:
      return L"DWC_USB31";
    case DWC3_IP_DWC32:
      return L"DWC_USB32";
    default:
      return L"BEKLENMEDIK";
  }
}

STATIC
CONST CHAR16 *
XhciLinkStateName (
  IN UINT32  PortSc
  )
{
  switch ((PortSc >> 5) & 0xF) {
    case 0:
      return L"U0";
    case 1:
      return L"U1";
    case 2:
      return L"U2";
    case 3:
      return L"U3";
    case 4:
      return L"DISABLED";
    case 5:
      return L"RXDETECT";
    case 6:
      return L"INACTIVE";
    case 7:
      return L"POLLING";
    case 8:
      return L"RECOVERY";
    case 9:
      return L"HOTRESET";
    case 10:
      return L"COMPLIANCE";
    case 11:
      return L"TEST";
    case 15:
      return L"RESUME";
    default:
      return L"UNKNOWN";
  }
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
VOID
CaptureUsbAttachSnapshot (
  OUT B2L_USB_ATTACH_SNAPSHOT  *Snapshot
  )
{
  UINT32  Cap;
  UINT32  CapLength;
  UINT32  Hcs1;
  UINT32  MaxPorts;
  UINT32  OpBase;
  UINTN   Index;

  Snapshot->PmuUsbDp    = Read32 (B2L_PMU_BASE, B2L_PMU_USBDP_CTRL);
  Snapshot->PmuUsbPhy   = Read32 (B2L_PMU_BASE, B2L_PMU_USB_PHY_CTRL);
  Snapshot->Gctl        = Read32 (B2L_DWC3_BASE, DWC3_GCTL);
  Snapshot->Gsts        = Read32 (B2L_DWC3_BASE, DWC3_GSTS);
  Snapshot->Usb2PhyCfg  = Read32 (B2L_DWC3_BASE, DWC3_GUSB2PHYCFG0);
  Snapshot->Usb3PipeCtl = Read32 (B2L_DWC3_BASE, DWC3_GUSB3PIPECTL0);
  Snapshot->Dcfg        = Read32 (B2L_DWC3_BASE, DWC3_DCFG);
  Snapshot->Dctl        = Read32 (B2L_DWC3_BASE, DWC3_DCTL);
  Snapshot->Dsts        = Read32 (B2L_DWC3_BASE, DWC3_DSTS);
  Snapshot->Ocfg        = Read32 (B2L_DWC3_BASE, DWC3_OCFG);
  Snapshot->Octl        = Read32 (B2L_DWC3_BASE, DWC3_OCTL);
  Snapshot->Oevt        = Read32 (B2L_DWC3_BASE, DWC3_OEVT);
  Snapshot->Oevten      = Read32 (B2L_DWC3_BASE, DWC3_OEVTEN);
  Snapshot->Osts        = Read32 (B2L_DWC3_BASE, DWC3_OSTS);

  Snapshot->XhciValid  = FALSE;
  Snapshot->XhciUsbSts = 0xFFFFFFFFU;
  Snapshot->PortSc[0]  = 0xFFFFFFFFU;
  Snapshot->PortSc[1]  = 0xFFFFFFFFU;
  Cap                   = Read32 (B2L_DWC3_BASE, 0x00);
  Hcs1                  = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS1);
  CapLength             = Cap & 0xFF;
  MaxPorts              = (Hcs1 >> 24) & 0xFF;
  if ((CapLength >= 0x20) && (CapLength <= 0x100) &&
      (MaxPorts >= 2) && (MaxPorts <= 16)) {
    OpBase                = CapLength;
    Snapshot->XhciUsbSts  = Read32 (
                              B2L_DWC3_BASE,
                              OpBase + XHCI_USBSTS
                              );
    Snapshot->PortSc[0]   = Read32 (
                              B2L_DWC3_BASE,
                              OpBase + XHCI_PORTSC
                              );
    Snapshot->PortSc[1]   = Read32 (
                              B2L_DWC3_BASE,
                              OpBase + XHCI_PORTSC + XHCI_PORT_STRIDE
                              );
    Snapshot->XhciValid   = TRUE;
  }

  for (Index = 0; Index < B2L_USB_PHY_SAMPLE_COUNT; Index++) {
    Snapshot->Phy[Index] = Read32 (
                             B2L_USB_PHY_BASE,
                             Index * sizeof (UINT32)
                             );
  }
}

#define B2L_USB_DIFF_POWER  BIT0
#define B2L_USB_DIFF_GLOBAL BIT1
#define B2L_USB_DIFF_DEVICE BIT2
#define B2L_USB_DIFF_OTG    BIT3
#define B2L_USB_DIFF_XHCI   BIT4
#define B2L_USB_DIFF_PHY    BIT5

STATIC
UINT32
UsbAttachDiffMask (
  IN CONST B2L_USB_ATTACH_SNAPSHOT  *Previous,
  IN CONST B2L_USB_ATTACH_SNAPSHOT  *Current
  )
{
  UINT32  Mask;
  UINTN   Index;

  Mask = 0;
  if ((Previous->PmuUsbDp != Current->PmuUsbDp) ||
      (Previous->PmuUsbPhy != Current->PmuUsbPhy)) {
    Mask |= B2L_USB_DIFF_POWER;
  }
  if ((Previous->Gctl != Current->Gctl) ||
      (Previous->Gsts != Current->Gsts) ||
      (Previous->Usb2PhyCfg != Current->Usb2PhyCfg) ||
      (Previous->Usb3PipeCtl != Current->Usb3PipeCtl)) {
    Mask |= B2L_USB_DIFF_GLOBAL;
  }
  if ((Previous->Dcfg != Current->Dcfg) ||
      (Previous->Dctl != Current->Dctl) ||
      ((Previous->Dsts & ~DWC3_DSTS_SOFFN_MASK) !=
       (Current->Dsts & ~DWC3_DSTS_SOFFN_MASK))) {
    Mask |= B2L_USB_DIFF_DEVICE;
  }
  if ((Previous->Ocfg != Current->Ocfg) ||
      (Previous->Octl != Current->Octl) ||
      (Previous->Oevt != Current->Oevt) ||
      (Previous->Oevten != Current->Oevten) ||
      (Previous->Osts != Current->Osts)) {
    Mask |= B2L_USB_DIFF_OTG;
  }
  if ((Previous->XhciValid != Current->XhciValid) ||
      (Previous->XhciUsbSts != Current->XhciUsbSts) ||
      (Previous->PortSc[0] != Current->PortSc[0]) ||
      (Previous->PortSc[1] != Current->PortSc[1])) {
    Mask |= B2L_USB_DIFF_XHCI;
  }
  for (Index = 0; Index < B2L_USB_PHY_SAMPLE_COUNT; Index++) {
    if (Previous->Phy[Index] != Current->Phy[Index]) {
      Mask |= B2L_USB_DIFF_PHY;
      break;
    }
  }
  return Mask;
}

STATIC
VOID
PrintMonitorPort (
  IN UINTN   Port,
  IN UINT32  PortSc
  )
{
  Print (L"P%u=%08x CCS=%u PED=%u PP=%u PLS=%s SPD=%x CHG=%02x CAS=%u\n",
         Port, PortSc,
         (PortSc & XHCI_PORTSC_CCS) != 0,
         (PortSc & XHCI_PORTSC_PED) != 0,
         (PortSc & XHCI_PORTSC_PP) != 0,
         XhciLinkStateName (PortSc),
         (PortSc >> 10) & 0xF,
         (PortSc & XHCI_PORTSC_CHANGE_MASK) >> 17,
         (PortSc & XHCI_PORTSC_CAS) != 0);
}

STATIC
BOOLEAN
WaitForMonitorTick (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Slice;

  for (Slice = 0; Slice < 20; Slice++) {
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
VOID
ShowUsbAttachMonitor (
  VOID
  )
{
  B2L_USB_ATTACH_SNAPSHOT  Current;
  B2L_USB_ATTACH_SNAPSHOT  Previous;
  BOOLEAN                  AttachSeen;
  BOOLEAN                  HavePrevious;
  UINT32                   Diff;
  UINT32                   StickyDiff;
  UINTN                    Index;
  UINTN                    Sample;

  HavePrevious = FALSE;
  StickyDiff   = 0;
  Sample       = 1;
  for (;;) {
    CaptureUsbAttachSnapshot (&Current);
    Diff = HavePrevious ? UsbAttachDiffMask (&Previous, &Current) : 0;
    StickyDiff |= Diff;
    AttachSeen = (BOOLEAN)(((Current.Osts &
                             (DWC3_OSTS_VBUSVLD | DWC3_OSTS_BSESVLD)) != 0) ||
                           (Current.XhciValid &&
                            (((Current.PortSc[0] | Current.PortSc[1]) &
                              XHCI_PORTSC_CCS) != 0)));

    gST->ConOut->ClearScreen (gST->ConOut);
    Print (L"=== B2L USB TANI R%u - CANLI ATTACH MONITOR ===\n",
           B2L_USB_TEST_REVISION);
    Print (L"ORNEK=%u ARALIK=2sn ATTACH_GORULEN=%s  Q/ESC=Monitorden cik\n",
           Sample, AttachSeen ? L"EVET" : L"HAYIR");
    Print (L"Once kablo TAKILI fotograf; sonra CIKAR, ornek artinca tekrar.\n");
    Print (L"Yalniz salt-okuma; DWC3/xHCI/PHY/rol/reset/transfer yazmasi yok.\n\n");

    Print (L"DELTA ANLIK PWR=%u DWC=%u DEV=%u OTG=%u XHC=%u PHY=%u\n",
           (Diff & B2L_USB_DIFF_POWER) != 0,
           (Diff & B2L_USB_DIFF_GLOBAL) != 0,
           (Diff & B2L_USB_DIFF_DEVICE) != 0,
           (Diff & B2L_USB_DIFF_OTG) != 0,
           (Diff & B2L_USB_DIFF_XHCI) != 0,
           (Diff & B2L_USB_DIFF_PHY) != 0);
    Print (L"DELTA BIRIKIM PWR=%u DWC=%u DEV=%u OTG=%u XHC=%u PHY=%u\n",
           (StickyDiff & B2L_USB_DIFF_POWER) != 0,
           (StickyDiff & B2L_USB_DIFF_GLOBAL) != 0,
           (StickyDiff & B2L_USB_DIFF_DEVICE) != 0,
           (StickyDiff & B2L_USB_DIFF_OTG) != 0,
           (StickyDiff & B2L_USB_DIFF_XHCI) != 0,
           (StickyDiff & B2L_USB_DIFF_PHY) != 0);
    Print (L"PMU DP=%08x USBPHY=%08x\n",
           Current.PmuUsbDp, Current.PmuUsbPhy);
    Print (L"DWC GCTL=%08x GSTS=%08x USB2=%08x USB3=%08x\n",
           Current.Gctl, Current.Gsts,
           Current.Usb2PhyCfg, Current.Usb3PipeCtl);
    Print (L"DEV DCFG=%08x DCTL=%08x RUN=%u DSTS=%08x SOFN=%04x\n",
           Current.Dcfg, Current.Dctl,
           (Current.Dctl & DWC3_DCTL_RUN_STOP) != 0,
           Current.Dsts,
           (Current.Dsts & DWC3_DSTS_SOFFN_MASK) >> 3);
    Print (L"DEV SPD=%s LINK=%s HLT=%u IDLE=%u RXEMPTY=%u\n",
           Dwc3SpeedName (Current.Dsts), Dwc3LinkStateName (Current.Dsts),
           (Current.Dsts & DWC3_DSTS_DEVCTRLHLT) != 0,
           (Current.Dsts & DWC3_DSTS_COREIDLE) != 0,
           (Current.Dsts & DWC3_DSTS_RXFIFOEMPTY) != 0);
    Print (L"OTG OCFG=%08x OCTL=%08x OEVT=%08x EN=%08x\n",
           Current.Ocfg, Current.Octl, Current.Oevt, Current.Oevten);
    Print (L"OTG OSTS=%08x ID=%u VBUS=%u BSES=%u XHPP=%u PERI=%u\n",
           Current.Osts,
           (Current.Osts & DWC3_OSTS_CONIDSTS) != 0,
           (Current.Osts & DWC3_OSTS_VBUSVLD) != 0,
           (Current.Osts & DWC3_OSTS_BSESVLD) != 0,
           (Current.Osts & DWC3_OSTS_XHCIPRTPOWER) != 0,
           (Current.Osts & DWC3_OSTS_PERIPHERALSTATE) != 0);
    if (Current.XhciValid) {
      Print (L"XHCI STS=%08x HCH=%u PCD=%u\n", Current.XhciUsbSts,
             (Current.XhciUsbSts & XHCI_USBSTS_HCH) != 0,
             (Current.XhciUsbSts & XHCI_USBSTS_PCD) != 0);
      PrintMonitorPort (1, Current.PortSc[0]);
      PrintMonitorPort (2, Current.PortSc[1]);
    } else {
      Print (L"XHCI capability gecersiz; operational/port okunmadi.\n");
    }
    Print (L"DRD PHY 10B00000, daha once dogrulanan 000..03C:\n");
    for (Index = 0; Index < B2L_USB_PHY_SAMPLE_COUNT; Index += 4) {
      Print (L"%03x=%08x %03x=%08x %03x=%08x %03x=%08x\n",
             Index * 4, Current.Phy[Index],
             (Index + 1) * 4, Current.Phy[Index + 1],
             (Index + 2) * 4, Current.Phy[Index + 2],
             (Index + 3) * 4, Current.Phy[Index + 3]);
    }

    Previous     = Current;
    HavePrevious = TRUE;
    Sample++;
    if (!WaitForMonitorTick ()) {
      break;
    }
  }
}

STATIC
BOOLEAN
PromptForNextStage (
  IN CONST CHAR16  *Stage
  )
{
  EFI_EVENT      Event;
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Index;

  Print (L"\nSONRAKI: %s\n", Stage);
  Print (L"Fotografla. Devam: herhangi bir tus. Cikis: ESC veya Q.\n");

  if ((gST->ConIn == NULL) || (gST->ConIn->WaitForKey == NULL)) {
    Print (L"Klavye olayi yok; guvenli cikis.\n");
    return FALSE;
  }

  Event = gST->ConIn->WaitForKey;
  for (;;) {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (!EFI_ERROR (Status)) {
      break;
    }

    Status = gBS->WaitForEvent (1, &Event, &Index);
    if (EFI_ERROR (Status)) {
      Print (L"Tus beklenemedi: %r\n", Status);
      return FALSE;
    }
  }

  if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == L'q') ||
      (Key.UnicodeChar == L'Q')) {
    return FALSE;
  }

  return TRUE;
}

STATIC
VOID
ShowPowerDomain (
  VOID
  )
{
  UINT32  Config;
  UINT32  Status;
  UINT32  Option;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 1 FSYS0A ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"HICBIR MMIO YAZMASI, RESET, I2C VEYA USB TRANSFERI YOK.\n\n");

  Config = Read32 (B2L_PD_FSYS0A_BASE, 0x00);
  Print (L"CONFIG [15862200]=%08x\n", Config);
  Status = Read32 (B2L_PD_FSYS0A_BASE, 0x04);
  Print (L"STATUS [15862204]=%08x\n", Status);
  Option = Read32 (B2L_PD_FSYS0A_BASE, 0x08);
  Print (L"OPTION [15862208]=%08x\n", Option);

  Print (L"\nDT PD FSYS0A BASE=15862200 LEN=20\n");
  Print (L"CFG_STAT_ESIT=%s  STAT_ALT_NIBBLE=%x\n",
         (Config == Status) ? L"EVET" : L"HAYIR", Status & 0xF);
  Print (L"15862210 ve 15862214 okunmadi: R1'de 15862214 external abort verdi.\n");
  Print (L"Bu sayfa FSYS0A disinda register okumadi.\n");
}

STATIC
VOID
ShowPmuUsbControls (
  VOID
  )
{
  UINT32  UsbPhy;
  UINT32  UsbDp;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 2 PMU USB ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"Her deger okunduktan hemen sonra yazdirilir.\n\n");

  UsbDp = Read32 (B2L_PMU_BASE, B2L_PMU_USBDP_CTRL);
  Print (L"PMU USBDP  [15860704]=%08x\n", UsbDp);
  UsbPhy = Read32 (B2L_PMU_BASE, B2L_PMU_USB_PHY_CTRL);
  Print (L"PMU USBPHY [1586072C]=%08x\n", UsbPhy);
  Print (L"\nBu sayfa PMU USB kontrolleri disinda register okumadi.\n");
}

STATIC
BOOLEAN
ShowDwc3AndXhci (
  VOID
  )
{
  UINT32  Cap;
  UINT32  Hcs1;
  UINT32  Hcs2;
  UINT32  Hcs3;
  UINT32  Hcc1;
  UINT32  Hcc2;
  UINT32  Gctl;
  UINT32  Gsts;
  UINT32  Id;
  UINT32  Ip;
  UINT32  Usb2;
  UINT32  Usb3;
  UINT32  Version;
  UINT32  VersionType;
  UINT32  MaxPorts;
  UINT32  MaxSlots;
  UINT32  CapLength;
  UINT32  OpBase;
  UINT32  UsbCmd;
  UINT32  UsbSts;
  UINT32  Port;
  UINT32  PortSc;
  BOOLEAN CoreSane;
  BOOLEAN CapSane;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 3 DWC3 XHCI ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"BASE=10C00000 LEN=10000 IRQ SPI=241 GSIV=273\n\n");

  Id    = Read32 (B2L_DWC3_BASE, DWC3_GSNPSID);
  Gctl  = Read32 (B2L_DWC3_BASE, DWC3_GCTL);
  Gsts  = Read32 (B2L_DWC3_BASE, DWC3_GSTS);
  Usb2  = Read32 (B2L_DWC3_BASE, DWC3_GUSB2PHYCFG0);
  Usb3  = Read32 (B2L_DWC3_BASE, DWC3_GUSB3PIPECTL0);

  Ip          = Id & DWC3_GSNPSID_MASK;
  CoreSane    = (Ip == DWC3_IP_DWC3) || (Ip == DWC3_IP_DWC31) ||
                (Ip == DWC3_IP_DWC32);
  Version     = 0;
  VersionType = 0;
  if ((Ip == DWC3_IP_DWC31) || (Ip == DWC3_IP_DWC32)) {
    Version     = Read32 (B2L_DWC3_BASE, DWC3_VER_NUMBER);
    VersionType = Read32 (B2L_DWC3_BASE, DWC3_VER_TYPE);
  }

  Print (L"GSNPSID=%08x  CORE_ID=%s\n", Id, CoreIdName (Id));
  if ((Ip == DWC3_IP_DWC31) || (Ip == DWC3_IP_DWC32)) {
    Print (L"VER_NUMBER=%08x VER_TYPE=%08x\n", Version, VersionType);
  }
  Print (L"GCTL=%08x PRTCAP=%s CORE_SRST=%u\n", Gctl,
         PortCapabilityName ((Gctl >> DWC3_GCTL_PRTCAP_SHIFT) &
                             DWC3_GCTL_PRTCAP_MASK),
         (Gctl & DWC3_GCTL_CORESOFTRESET) != 0);
  Print (L"GSTS=%08x CURMOD=%s\n", Gsts,
         CurrentModeName (Gsts & DWC3_GSTS_CURMOD_MASK));
  Print (L"USB2PHYCFG=%08x SRST=%u SUSPHY=%u\n", Usb2,
         (Usb2 & DWC3_GUSB2PHYCFG_SOFTRST) != 0,
         (Usb2 & DWC3_GUSB2PHYCFG_SUSPHY) != 0);
  Print (L"USB3PIPECTL=%08x SRST=%u SUSPHY=%u\n", Usb3,
         (Usb3 & DWC3_GUSB3PIPECTL_SOFTRST) != 0,
         (Usb3 & DWC3_GUSB3PIPECTL_SUSPHY) != 0);
  Print (L"BUS0=%08x BUS1=%08x GUCTL1=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_GSBUSCFG0),
         Read32 (B2L_DWC3_BASE, DWC3_GSBUSCFG1),
         Read32 (B2L_DWC3_BASE, DWC3_GUCTL1));
  Print (L"GUID=%08x GUCTL=%08x HWP0=%08x HWP1=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_GUID),
         Read32 (B2L_DWC3_BASE, DWC3_GUCTL),
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS0),
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS1));
  Print (L"HWP2=%08x HWP3=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS2),
         Read32 (B2L_DWC3_BASE, DWC3_GHWPARAMS3));

  Cap       = Read32 (B2L_DWC3_BASE, 0x00);
  Hcs1      = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS1);
  Hcs2      = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS2);
  Hcs3      = Read32 (B2L_DWC3_BASE, XHCI_HCSPARAMS3);
  Hcc1      = Read32 (B2L_DWC3_BASE, XHCI_HCCPARAMS1);
  Hcc2      = Read32 (B2L_DWC3_BASE, XHCI_HCCPARAMS2);
  CapLength = Cap & 0xFF;
  MaxSlots  = Hcs1 & 0xFF;
  MaxPorts  = (Hcs1 >> 24) & 0xFF;
  CapSane   = (CapLength >= 0x20) && (CapLength <= 0x100) &&
              (MaxSlots != 0) && (MaxPorts != 0) && (MaxPorts <= 16);

  Print (L"\nXHCI CAP=%08x CAPLEN=%x HCIVER=%04x SANE=%s\n",
         Cap, CapLength, Cap >> 16, CapSane ? L"EVET" : L"HAYIR");
  Print (L"HCS1=%08x SLOT=%u PORT=%u INTR=%u\n", Hcs1, MaxSlots,
         MaxPorts, (Hcs1 >> 8) & 0x7FF);
  Print (L"HCS2=%08x HCS3=%08x HCC1=%08x HCC2=%08x\n",
         Hcs2, Hcs3, Hcc1, Hcc2);
  Print (L"DBOFF=%08x RTSOFF=%08x\n",
         Read32 (B2L_DWC3_BASE, XHCI_DBOFF),
         Read32 (B2L_DWC3_BASE, XHCI_RTSOFF));

  if (!CapSane) {
    Print (L"XHCI turetilmis adresleri okunmadi: capability gecersiz.\n");
    Print (L"Device rolde bu ayri bir olcumdur; DWC3 kimligi gecerliyse devam.\n");
    return CoreSane;
  }

  OpBase = CapLength;
  UsbCmd = Read32 (B2L_DWC3_BASE, OpBase + XHCI_USBCMD);
  UsbSts = Read32 (B2L_DWC3_BASE, OpBase + XHCI_USBSTS);
  Print (L"CMD=%08x STS=%08x RUN=%u HCH=%u CNR=%u HSE=%u HCE=%u\n",
         UsbCmd, UsbSts, (UsbCmd & 1U) != 0,
         (UsbSts & XHCI_USBSTS_HCH) != 0,
         (UsbSts & XHCI_USBSTS_CNR) != 0,
         (UsbSts & XHCI_USBSTS_HSE) != 0,
         (UsbSts & XHCI_USBSTS_HCE) != 0);
  Print (L"PAGE=%08x DNCTRL=%08x CONFIG=%08x\n",
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_PAGESIZE),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_DNCTRL),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_CONFIG));
  Print (L"CRCR=%08x:%08x DCBAAP=%08x:%08x\n",
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_CRCR_HI),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_CRCR_LO),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_DCBAAP_HI),
         Read32 (B2L_DWC3_BASE, OpBase + XHCI_DCBAAP_LO));

  for (Port = 0; Port < MaxPorts; Port++) {
    PortSc = Read32 (B2L_DWC3_BASE,
                     OpBase + XHCI_PORTSC + (Port * XHCI_PORT_STRIDE));
    Print (L"PORT%u=%08x CCS=%u PED=%u PP=%u OCA=%u PLS=%x SPEED=%x\n",
           Port + 1, PortSc,
           (PortSc & XHCI_PORTSC_CCS) != 0,
           (PortSc & XHCI_PORTSC_PED) != 0,
           (PortSc & XHCI_PORTSC_PP) != 0,
           (PortSc & XHCI_PORTSC_OCA) != 0,
           (PortSc >> 5) & 0xF, (PortSc >> 10) & 0xF);
  }

  return CoreSane;
}

STATIC
VOID
ShowDwc3RoleRegisters (
  VOID
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 4 ROL ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"DWC3 device ve OTG registerleri; yalniz okuma.\n\n");
  Print (L"DCFG=%08x DCTL=%08x DSTS=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_DCFG),
         Read32 (B2L_DWC3_BASE, DWC3_DCTL),
         Read32 (B2L_DWC3_BASE, DWC3_DSTS));
  Print (L"OCFG=%08x OCTL=%08x OSTS=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_OCFG),
         Read32 (B2L_DWC3_BASE, DWC3_OCTL),
         Read32 (B2L_DWC3_BASE, DWC3_OSTS));
  Print (L"OEVT=%08x OEVTEN=%08x\n",
         Read32 (B2L_DWC3_BASE, DWC3_OEVT),
         Read32 (B2L_DWC3_BASE, DWC3_OEVTEN));
  Print (L"\nMAX77705 sonraki ayri onayli asamalarda incelenir.\n");
}

STATIC
VOID
ShowDrdPhyWindow (
  VOID
  )
{
  UINTN   Offset;
  UINT32  Value;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 5 USB DRD PHY ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"BASE=%08x DT_LEN=200 OKUNAN=0..3C\n\n", B2L_USB_PHY_BASE);
  for (Offset = 0; Offset < 0x40; Offset += sizeof (UINT32)) {
    Print (L"%03x: ", Offset);
    Value = Read32 (B2L_USB_PHY_BASE, Offset);
    Print (L"%08x\n", Value);
  }
  Print (L"\nGenis tarama yok; read-clear olabilecek bilinmeyen alanlar okunmadi.\n");
}

STATIC
VOID
ShowSkippedComboPcs (
  VOID
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 6 COMBO PHY PCS ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"BASE=%08x DT_LEN=2800 OKUMA YOK\n\n", B2L_USB_PCS_BASE);
  Print (L"%03x: R5 cihaz testinde synchronous external abort\n",
         B2L_USB_PCS_ABORT_OFFSET_0);
  Print (L"%03x: R4 cihaz testinde synchronous external abort\n",
         B2L_USB_PCS_ABORT_OFFSET_C);
  Print (L"\nPCS penceresi mevcut power clock izolasyon durumunda atlandi.\n");
  Print (L"Sebep kanitlanmadan bu pencerede baska MMIO okunmayacak.\n");
}

STATIC
VOID
ShowSkippedComboPma (
  VOID
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB SALT OKUMA TANI R%u - ASAMA 7 COMBO PHY PMA ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"BASE=%08x DT_LEN=800 OKUMA YOK\n\n", B2L_USB_PMA_BASE);
  Print (L"%03x: R6 kablosuz cihaz testinde synchronous external abort\n",
         B2L_USB_PMA_ABORT_OFFSET_0);
  Print (L"\nPMA penceresi mevcut power clock izolasyon durumunda atlandi.\n");
  Print (L"Kablo durumu once guvenli PMU DWC3 ve DRD ekranlarinda karsilastirilacak.\n");
}

EFI_STATUS
EFIAPI
B2LUsbTestEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  BOOLEAN  Dwc3Sane;
  EFI_STATUS ProbeStatus;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  ShowPowerDomain ();
  if (!PromptForNextStage (L"PMU USB kontrol registerleri 15860704/1586072C")) {
    return EFI_SUCCESS;
  }
  ShowPmuUsbControls ();

  if (!PromptForNextStage (L"DWC3 ve xHCI penceresi 10C00000")) {
    return EFI_SUCCESS;
  }

  Dwc3Sane = ShowDwc3AndXhci ();
  if (!Dwc3Sane) {
    Print (L"\nDWC3 ailesi GSNPSID ile dogrulanmadi; PHY asamalari kapatildi.\n");
    PromptForNextStage (L"guvenli cikis");
    return EFI_DEVICE_ERROR;
  }

  if (!PromptForNextStage (L"DWC3 device OTG rol registerleri")) {
    return EFI_SUCCESS;
  }
  ShowDwc3RoleRegisters ();

  if (!PromptForNextStage (L"USB DRD PHY ana pencere 10B00000")) {
    return EFI_SUCCESS;
  }
  ShowDrdPhyWindow ();

  if (!PromptForNextStage (L"combo PHY PCS sonucu; MMIO okuma yok")) {
    return EFI_SUCCESS;
  }
  ShowSkippedComboPcs ();

  if (!PromptForNextStage (L"combo PHY PMA sonucu; MMIO okuma yok")) {
    return EFI_SUCCESS;
  }
  ShowSkippedComboPma ();

  if (!PromptForNextStage (
         L"canli USB monitor; veri kablosu PC'ye TAKILI olsun"
         )) {
    return EFI_SUCCESS;
  }
  ShowUsbAttachMonitor ();

  if (!PromptForNextStage (
         L"R11 AKTIF HS PHY + DWC3 event; veri kablosu PC'ye TAKILI"
         )) {
    return EFI_SUCCESS;
  }
  ProbeStatus = B2LDwc3RunEventProbe ();
  if (EFI_ERROR (ProbeStatus)) {
    Print (L"R11 aktif HS PHY/DWC3 probu tamamlanamadi: %r\n", ProbeStatus);
  }
  return ProbeStatus;
}
