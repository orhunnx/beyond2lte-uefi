/** @file
  Guarded Exynos9820 HS PHY, DWC_usb31 event probe and CACHE MSC gadget.

  R14 retains the deliberately narrow R13 USB Mass Storage gadget and adds a
  touchscreen front end through the existing EFI Absolute Pointer protocol.
  Storage remains EP0 plus one bulk OUT/IN pair backed only by the exact GPT
  CACHE partition.  No combo PHY, DWC3 role/core-reset or GPT write is
  performed.

  The probe refuses to write unless the core is a halted DWC_usb31 in device
  mode with no enabled endpoints, no pending event and no inherited device
  event enable.  The event interrupt stays masked; firmware polls and
  acknowledges the ring.  On a normal stop every touched register is restored
  and DMA is freed.  After a mounted session execution still stops for a
  hardware reboot, avoiding reuse of firmware FAT cache.  If the controller
  cannot halt, DMA stays reserved and execution intentionally stops so later
  software cannot reclaim a possible DMA target.
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleTextIn.h>

#include "B2LDwc3Probe.h"
#include "B2LTouchUi.h"

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
#define DWC3_DEP_BASE(Ep)            (0xC800U + ((Ep) * 0x10U))
#define DWC3_DEPCMDPAR2(Ep)          (DWC3_DEP_BASE (Ep) + 0x00U)
#define DWC3_DEPCMDPAR1(Ep)          (DWC3_DEP_BASE (Ep) + 0x04U)
#define DWC3_DEPCMDPAR0(Ep)          (DWC3_DEP_BASE (Ep) + 0x08U)
#define DWC3_DEPCMD(Ep)              (DWC3_DEP_BASE (Ep) + 0x0CU)
#define DWC3_DEPCMD0                 DWC3_DEPCMD (0)
#define DWC3_DEPCMD1                 DWC3_DEPCMD (1)

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
#define DWC3_DCTL_ULSTCHNGREQ_SHIFT  5
#define DWC3_DCTL_ULSTCHNGREQ_MASK   (0xFU << DWC3_DCTL_ULSTCHNGREQ_SHIFT)
#define DWC3_DCTL_ULSTCHNGREQ(Value) \
  (((Value) << DWC3_DCTL_ULSTCHNGREQ_SHIFT) & \
   DWC3_DCTL_ULSTCHNGREQ_MASK)
#define DWC3_DCTL_ULSTCHNG_RX_DETECT DWC3_DCTL_ULSTCHNGREQ (0x5U)
#define DWC3_DCFG_DEVSPD_MASK        7U
#define DWC3_DCFG_DEVSPD_HS          0U
#define DWC3_DCFG_DEVADDR_SHIFT      3
#define DWC3_DCFG_DEVADDR_MASK       (0x7FU << DWC3_DCFG_DEVADDR_SHIFT)
#define DWC3_DCFG_DEVADDR(Address)   \
  (((Address) << DWC3_DCFG_DEVADDR_SHIFT) & DWC3_DCFG_DEVADDR_MASK)
#define DWC3_DSTS_COREIDLE           BIT23
#define DWC3_DSTS_DEVCTRLHLT         BIT22
#define DWC3_DSTS_RXFIFOEMPTY        BIT17
#define DWC3_DSTS_SOFFN_MASK         (0x3FFFU << 3)
#define DWC3_DSTS_USBLNKST_SHIFT     18
#define DWC3_DSTS_USBLNKST_MASK      (0xFU << DWC3_DSTS_USBLNKST_SHIFT)
#define DWC3_DSTS_CONNECTSPD_MASK    7U
#define DWC3_DEPCMD_CMDACT           BIT10
#define DWC3_DEPCMD_HIPRI_FORCERM    BIT11
#define DWC3_DEPCMD_STATUS_MASK      (0xFU << 12)
#define DWC3_DEPCMD_PARAM_SHIFT      16
#define DWC3_DEPCMD_PARAM(Value)     ((Value) << DWC3_DEPCMD_PARAM_SHIFT)
#define DWC3_DEPCMD_GET_RSC_IDX(Cmd) (((Cmd) >> 16) & 0x7FU)
#define DWC3_DEPCMD_SETEPCONFIG      0x01U
#define DWC3_DEPCMD_SETTRANSFRESOURCE 0x02U
#define DWC3_DEPCMD_SETSTALL         0x04U
#define DWC3_DEPCMD_CLEARSTALL       0x05U
#define DWC3_DEPCMD_STARTTRANSFER    0x06U
#define DWC3_DEPCMD_ENDTRANSFER      0x08U
#define DWC3_DEPCMD_DEPSTARTCFG      0x09U

#define DWC3_DEPCFG_XFER_COMPLETE_EN BIT8
#define DWC3_DEPCFG_XFER_IN_PROGRESS_EN BIT9
#define DWC3_DEPCFG_XFER_NOT_READY_EN BIT10
#define DWC3_DEPCFG_EP_NUMBER(Ep)    (((Ep) & 0x1FU) << 25)
#define DWC3_DEPCFG_EP_TYPE(Type)    (((Type) & 0x3U) << 1)
#define DWC3_DEPCFG_MAX_PACKET_SIZE(Size) (((Size) & 0x7FFU) << 3)
#define DWC3_DEPCFG_FIFO_NUMBER(Number) (((Number) & 0x1FU) << 17)
#define DWC3_DEPCFG_ACTION_INIT      (0U << 30)
#define DWC3_DEPCFG_ACTION_MODIFY    (2U << 30)
#define DWC3_DEPXFERCFG_NUM_XFER_RES(Number) ((Number) & 0xFFFFU)

#define DWC3_TRB_SIZE_MASK           0x00FFFFFFU
#define DWC3_TRB_CTRL_HWO            BIT0
#define DWC3_TRB_CTRL_LST            BIT1
#define DWC3_TRB_CTRL_TRBCTL(Type)   (((Type) & 0x3FU) << 4)
#define DWC3_TRB_CTRL_ISP_IMI        BIT10
#define DWC3_TRB_CTRL_IOC            BIT11
#define DWC3_TRBCTL_NORMAL           DWC3_TRB_CTRL_TRBCTL (1)
#define DWC3_TRBCTL_CONTROL_SETUP    DWC3_TRB_CTRL_TRBCTL (2)
#define DWC3_TRBCTL_CONTROL_STATUS2  DWC3_TRB_CTRL_TRBCTL (3)
#define DWC3_TRBCTL_CONTROL_STATUS3  DWC3_TRB_CTRL_TRBCTL (4)
#define DWC3_TRBCTL_CONTROL_DATA     DWC3_TRB_CTRL_TRBCTL (5)

#define DWC3_DEPEVT_XFERCOMPLETE     0x01U
#define DWC3_DEPEVT_XFERNOTREADY     0x03U
#define DWC3_DEPEVT_STATUS_CONTROL_STATUS 2U

#define USB_REQ_GET_STATUS           0x00U
#define USB_REQ_CLEAR_FEATURE        0x01U
#define USB_REQ_SET_FEATURE          0x03U
#define USB_REQ_SET_ADDRESS          0x05U
#define USB_REQ_GET_DESCRIPTOR       0x06U
#define USB_REQ_GET_CONFIGURATION    0x08U
#define USB_REQ_SET_CONFIGURATION    0x09U
#define USB_REQ_GET_INTERFACE        0x0AU
#define USB_REQ_SET_INTERFACE        0x0BU
#define USB_REQ_TYPE_DIRECTION_IN    BIT7
#define USB_REQ_TYPE_TYPE_MASK       0x60U
#define USB_REQ_TYPE_STANDARD        0x00U
#define USB_REQ_TYPE_CLASS           0x20U
#define USB_REQ_TYPE_RECIPIENT_MASK  0x1FU
#define USB_REQ_RECIPIENT_ENDPOINT   0x02U
#define USB_FEATURE_ENDPOINT_HALT    0x00U
#define USB_DESC_TYPE_DEVICE         0x01U
#define USB_DESC_TYPE_CONFIGURATION  0x02U
#define USB_DESC_TYPE_STRING         0x03U
#define USB_DESC_TYPE_DEVICE_QUALIFIER 0x06U
#define USB_DESC_TYPE_OTHER_SPEED    0x07U

#define USB_MSC_GET_MAX_LUN          0xFEU
#define USB_MSC_BULK_RESET           0xFFU
#define USB_MSC_CBW_SIGNATURE        0x43425355U
#define USB_MSC_CSW_SIGNATURE        0x53425355U
#define USB_MSC_CSW_PASSED           0U
#define USB_MSC_CSW_FAILED           1U
#define USB_MSC_CSW_PHASE_ERROR      2U

#define SCSI_TEST_UNIT_READY         0x00U
#define SCSI_REQUEST_SENSE           0x03U
#define SCSI_INQUIRY                 0x12U
#define SCSI_MODE_SENSE_6            0x1AU
#define SCSI_START_STOP_UNIT         0x1BU
#define SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1EU
#define SCSI_READ_FORMAT_CAPACITIES  0x23U
#define SCSI_READ_CAPACITY_10        0x25U
#define SCSI_READ_10                 0x28U
#define SCSI_WRITE_10                0x2AU
#define SCSI_VERIFY_10               0x2FU
#define SCSI_SYNCHRONIZE_CACHE_10    0x35U
#define SCSI_MODE_SENSE_10           0x5AU
#define SCSI_REPORT_LUNS             0xA0U

#define B2L_CACHE_PARTITION_BYTES    629145600ULL
#define B2L_CACHE_BLOCK_SIZE         4096U
#define B2L_CACHE_LAST_BLOCK         153599ULL
#define B2L_MSC_EVENT_OFFSET         0U
#define B2L_MSC_EP0_TRB_OFFSET       EFI_PAGE_SIZE
#define B2L_MSC_EP2_TRB_OFFSET       (EFI_PAGE_SIZE + 0x40U)
#define B2L_MSC_EP3_TRB_OFFSET       (EFI_PAGE_SIZE + 0x80U)
#define B2L_MSC_CONTROL_OFFSET       (EFI_PAGE_SIZE * 2U)
#define B2L_MSC_BULK_OFFSET          (EFI_PAGE_SIZE * 3U)
#define B2L_MSC_BULK_BYTES           (EFI_PAGE_SIZE * 16U)
#define B2L_MSC_DMA_BYTES            (B2L_MSC_BULK_OFFSET + B2L_MSC_BULK_BYTES)
#define B2L_MSC_EP0_MAX_PACKET       64U
#define B2L_MSC_BULK_MAX_PACKET      512U
#define B2L_MSC_COMMAND_TIMEOUT_US   100000U
#define B2L_MSC_STOP_TIMEOUT_MS      2000U

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

#pragma pack(1)
typedef struct {
  UINT32  BufferPointerLow;
  UINT32  BufferPointerHigh;
  UINT32  Size;
  UINT32  Control;
} B2L_DWC3_TRB;

typedef struct {
  UINT8   RequestType;
  UINT8   Request;
  UINT16  Value;
  UINT16  Index;
  UINT16  Length;
} B2L_USB_SETUP_PACKET;

typedef struct {
  UINT32  Signature;
  UINT32  Tag;
  UINT32  DataTransferLength;
  UINT8   Flags;
  UINT8   Lun;
  UINT8   CommandLength;
  UINT8   CommandBlock[16];
} B2L_USB_MSC_CBW;

typedef struct {
  UINT32  Signature;
  UINT32  Tag;
  UINT32  DataResidue;
  UINT8   Status;
} B2L_USB_MSC_CSW;
#pragma pack()

typedef enum {
  B2L_EP0_SETUP_ARMED,
  B2L_EP0_DATA_IN,
  B2L_EP0_WAIT_STATUS,
  B2L_EP0_STATUS
} B2L_EP0_STATE;

typedef struct {
  EFI_PHYSICAL_ADDRESS   DmaAddress;
  VOID                  *Dma;
  volatile UINT32       *EventWords;
  UINTN                  EventReadPosition;
  B2L_DWC3_TRB          *Ep0Trb;
  B2L_DWC3_TRB          *Ep2Trb;
  B2L_DWC3_TRB          *Ep3Trb;
  UINT8                 *ControlBuffer;
  UINT8                 *BulkBuffer;
  EFI_BLOCK_IO_PROTOCOL *CacheBlockIo;
  EFI_HANDLE             CacheHandle;
  UINT32                 EpResource[4];
  B2L_EP0_STATE          Ep0State;
  BOOLEAN                Ep0ThreeStage;
  BOOLEAN                Configured;
  BOOLEAN                BulkInitialized;
  BOOLEAN                BusReset;
  BOOLEAN                StopRequested;
  BOOLEAN                Ejected;
  BOOLEAN                ReadOnly;
  BOOLEAN                WroteMedia;
  BOOLEAN                FlushSucceeded;
  UINT8                  ConfigurationValue;
  UINT8                  SenseKey;
  UINT8                  SenseAsc;
  UINT8                  SenseAscq;
  UINTN                  ResetCount;
  UINTN                  ConnectDoneCount;
  UINTN                  CommandCount;
  UINT64                 BytesRead;
  UINT64                 BytesWritten;
} B2L_USB_MSC_CONTEXT;

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
    case DWC3_DALEPENA:
    case DWC3_DEPCMDPAR2 (0):
    case DWC3_DEPCMDPAR1 (0):
    case DWC3_DEPCMDPAR0 (0):
    case DWC3_DEPCMD (0):
    case DWC3_DEPCMDPAR2 (1):
    case DWC3_DEPCMDPAR1 (1):
    case DWC3_DEPCMDPAR0 (1):
    case DWC3_DEPCMD (1):
    case DWC3_DEPCMDPAR2 (2):
    case DWC3_DEPCMDPAR1 (2):
    case DWC3_DEPCMDPAR0 (2):
    case DWC3_DEPCMD (2):
    case DWC3_DEPCMDPAR2 (3):
    case DWC3_DEPCMDPAR1 (3):
    case DWC3_DEPCMDPAR0 (3):
    case DWC3_DEPCMD (3):
      MmioWrite32 (Dwc3Register (Offset), Value);
      return TRUE;
    default:
      Print (L"R13 IC HATA: izin verilmeyen DWC3 write offset=%x\n", Offset);
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
  // Bit 0 set selects the device/global format; zero is an endpoint event.
  // R12 had this polarity reversed, which hid the observed USB_RESET (0x101)
  // and CONNECT_DONE (0x201) events in the endpoint counter.
  if ((Event & BIT0) == 0) {
    Stats->EndpointTotal++;
    return;
  }

  if (((Event >> 1) & 0x7F) == 0) {
    Type = (Event >> 8) & 0xF;
    Stats->DeviceType[Type]++;
    return;
  }

  Stats->OtherTotal++;
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
  if ((Stats->Last[Position] & BIT0) == 0) {
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
         (Saved.DeviceControl & ~DWC3_DCTL_ULSTCHNGREQ_MASK) |
         DWC3_DCTL_ULSTCHNG_RX_DETECT
         ) ||
      !Dwc3ProbeWrite32 (
         DWC3_DCTL,
         (Saved.DeviceControl & ~DWC3_DCTL_ULSTCHNGREQ_MASK) |
         DWC3_DCTL_ULSTCHNG_RX_DETECT | DWC3_DCTL_RUN_STOP
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

  // The Exynos vendor sequence requests RX_DETECT before both RUN and STOP.
  // Keep the two MMIO writes separate so the link request reaches the core
  // before RUN_STOP is cleared.
  Dctl  = Dwc3Read32 (DWC3_DCTL);
  Dctl &= ~DWC3_DCTL_ULSTCHNGREQ_MASK;
  Dctl |= DWC3_DCTL_ULSTCHNG_RX_DETECT;
  Dwc3ProbeWrite32 (DWC3_DCTL, Dctl);
  Dwc3ProbeWrite32 (DWC3_DCTL, Dctl & ~DWC3_DCTL_RUN_STOP);
  gBS->Stall (50000);
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

STATIC
UINT32
ReadBe32 (
  IN CONST UINT8  *Bytes
  )
{
  return ((UINT32)Bytes[0] << 24) | ((UINT32)Bytes[1] << 16) |
         ((UINT32)Bytes[2] << 8) | Bytes[3];
}

STATIC
UINT16
ReadLe16 (
  IN CONST UINT8  *Bytes
  )
{
  return (UINT16)(Bytes[0] | ((UINT16)Bytes[1] << 8));
}

STATIC
UINT32
ReadLe32 (
  IN CONST UINT8  *Bytes
  )
{
  return (UINT32)(Bytes[0] | ((UINT32)Bytes[1] << 8) |
                  ((UINT32)Bytes[2] << 16) | ((UINT32)Bytes[3] << 24));
}

STATIC
UINT16
ReadBe16 (
  IN CONST UINT8  *Bytes
  )
{
  return (UINT16)(((UINT16)Bytes[0] << 8) | Bytes[1]);
}

STATIC
VOID
WriteBe32 (
  OUT UINT8   *Bytes,
  IN  UINT32   Value
  )
{
  Bytes[0] = (UINT8)(Value >> 24);
  Bytes[1] = (UINT8)(Value >> 16);
  Bytes[2] = (UINT8)(Value >> 8);
  Bytes[3] = (UINT8)Value;
}

STATIC
BOOLEAN
IsCachePartitionName (
  IN CONST CHAR16  *Name
  )
{
  STATIC CONST CHAR16  CacheName[] = L"CACHE";

  return (CompareMem (Name, CacheName, sizeof (CacheName)) == 0);
}

STATIC
EFI_STATUS
FindCachePartition (
  IN  EFI_HANDLE             ImageHandle,
  OUT B2L_USB_MSC_CONTEXT   *Context
  )
{
  EFI_BLOCK_IO_PROTOCOL       *BlockIo;
  EFI_HANDLE                  *Handles;
  EFI_HANDLE                   ImageDevice;
  EFI_LOADED_IMAGE_PROTOCOL   *LoadedImage;
  EFI_PARTITION_INFO_PROTOCOL *PartitionInfo;
  EFI_STATUS                   Status;
  UINTN                        HandleCount;
  UINTN                        Index;

  Handles      = NULL;
  ImageDevice  = NULL;
  LoadedImage  = NULL;
  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (!EFI_ERROR (Status) && (LoadedImage != NULL)) {
    ImageDevice = LoadedImage->DeviceHandle;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPartitionInfoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Print (L"CACHE GPT handle listesi bulunamadi: %r\n", Status);
    return Status;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < HandleCount; Index++) {
    PartitionInfo = NULL;
    BlockIo       = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (
                          Handles[Index],
                          &gEfiPartitionInfoProtocolGuid,
                          (VOID **)&PartitionInfo
                          )) ||
        (PartitionInfo == NULL) ||
        (PartitionInfo->Type != PARTITION_TYPE_GPT) ||
        !IsCachePartitionName (PartitionInfo->Info.Gpt.PartitionName)) {
      continue;
    }

    if (Handles[Index] == ImageDevice) {
      Print (L"RED: B2LUSB.EFI CACHE bolumunden calisiyor; kendini sunamaz.\n");
      Status = EFI_ACCESS_DENIED;
      break;
    }

    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&BlockIo
                    );
    if (EFI_ERROR (Status) || (BlockIo == NULL) ||
        (BlockIo->Media == NULL)) {
      Print (L"CACHE GPT bulundu fakat BlockIo yok: %r\n", Status);
      Status = EFI_NOT_FOUND;
      break;
    }

    if (!BlockIo->Media->MediaPresent ||
        !BlockIo->Media->LogicalPartition ||
        (BlockIo->Media->BlockSize != B2L_CACHE_BLOCK_SIZE) ||
        (BlockIo->Media->LastBlock != B2L_CACHE_LAST_BLOCK) ||
        (((BlockIo->Media->LastBlock + 1ULL) *
          BlockIo->Media->BlockSize) != B2L_CACHE_PARTITION_BYTES)) {
      Print (L"RED: CACHE geometrisi beklenmiyor: block=%u last=%Lu logical=%u.\n",
             BlockIo->Media->BlockSize, BlockIo->Media->LastBlock,
             BlockIo->Media->LogicalPartition);
      Status = EFI_COMPROMISED_DATA;
      break;
    }

    Context->CacheHandle  = Handles[Index];
    Context->CacheBlockIo = BlockIo;
    Status = EFI_SUCCESS;
    break;
  }

  if (Handles != NULL) {
    gBS->FreePool (Handles);
  }

  return Status;
}

STATIC
EFI_STATUS
PromptCacheAccessMode (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINTN       Selection;
  STATIC CONST B2L_TOUCH_UI_OPTION  AccessOptions[] = {
    { L"SALT-OKUMA", L"CACHE degismez", FALSE },
    { L"OKUMA / YAZMA", L"Dosya aktarimi - dikkat", TRUE },
    { L"GERI", L"USB disk modunu iptal et", FALSE }
  };
  STATIC CONST B2L_TOUCH_UI_OPTION  ConfirmOptions[] = {
    { L"YAZMAYI AC", L"CACHE uzerinde degisiklik yapilabilir", TRUE },
    { L"GERI DON", L"Salt-okuma secimine don", FALSE }
  };

  for (;;) {
    Status = B2LTouchUiChoose (
               L"CACHE USB DISK",
               L"600 MiB FAT32 - erisim modunu sec",
               AccessOptions,
               sizeof (AccessOptions) / sizeof (AccessOptions[0]),
               0,
               &Selection
               );
    if (EFI_ERROR (Status) || (Selection == 2)) {
      return EFI_ABORTED;
    }
    if (Selection == 0) {
      Context->ReadOnly = TRUE;
      return EFI_SUCCESS;
    }
    if (Context->CacheBlockIo->Media->ReadOnly) {
      gST->ConOut->ClearScreen (gST->ConOut);
      Print (L"Firmware BlockIo CACHE'i salt-okuma bildirdi.\n");
      Print (L"Yazma modu acilamadi; secim ekrani geri geliyor.\n");
      gBS->Stall (2000000);
      continue;
    }
    Status = B2LTouchUiChoose (
               L"YAZMA MODU ONAYI",
               L"Yanlis kablo cekimi FAT32 verisini bozabilir",
               ConfirmOptions,
               sizeof (ConfirmOptions) / sizeof (ConfirmOptions[0]),
               1,
               &Selection
               );
    if (EFI_ERROR (Status) || (Selection != 0)) {
      continue;
    }
    Context->ReadOnly = FALSE;
    return EFI_SUCCESS;
  }
}

STATIC
EFI_STATUS
ValidateCacheFat32 (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  EFI_BLOCK_IO_MEDIA  *Media;
  EFI_STATUS           Status;

  Media = Context->CacheBlockIo->Media;
  ZeroMem (Context->BulkBuffer, Media->BlockSize);
  WriteBackInvalidateDataCacheRange (Context->BulkBuffer, Media->BlockSize);
  Status = Context->CacheBlockIo->ReadBlocks (
                                   Context->CacheBlockIo,
                                   Media->MediaId,
                                   0,
                                   Media->BlockSize,
                                   Context->BulkBuffer
                                   );
  if (EFI_ERROR (Status)) {
    Print (L"CACHE FAT32 boot sektoru okunamadi: %r\n", Status);
    return Status;
  }
  WriteBackInvalidateDataCacheRange (Context->BulkBuffer, Media->BlockSize);

  if ((Context->BulkBuffer[510] != 0x55) ||
      (Context->BulkBuffer[511] != 0xAA) ||
      (ReadLe16 (&Context->BulkBuffer[11]) != B2L_CACHE_BLOCK_SIZE) ||
      (Context->BulkBuffer[13] == 0) ||
      (ReadLe16 (&Context->BulkBuffer[14]) == 0) ||
      (Context->BulkBuffer[16] == 0) ||
      (ReadLe32 (&Context->BulkBuffer[32]) !=
       (B2L_CACHE_LAST_BLOCK + 1ULL)) ||
      (CompareMem (&Context->BulkBuffer[82], "FAT32   ", 8) != 0)) {
    Print (L"RED: CACHE boot sektoru beklenen 4096-byte FAT32 degil.\n");
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Dwc3SendEpCommand (
  IN  UINTN   Endpoint,
  IN  UINT32  Command,
  IN  UINT32  Param0,
  IN  UINT32  Param1,
  IN  UINT32  Param2,
  OUT UINT32 *CompletedCommand OPTIONAL
  )
{
  UINT32  Value;
  UINTN   Microsecond;

  if (Endpoint >= 4) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Dwc3ProbeWrite32 (DWC3_DEPCMDPAR0 (Endpoint), Param0) ||
      !Dwc3ProbeWrite32 (DWC3_DEPCMDPAR1 (Endpoint), Param1) ||
      !Dwc3ProbeWrite32 (DWC3_DEPCMDPAR2 (Endpoint), Param2) ||
      !Dwc3ProbeWrite32 (
         DWC3_DEPCMD (Endpoint),
         Command | DWC3_DEPCMD_CMDACT
         )) {
    return EFI_ACCESS_DENIED;
  }

  Value = 0;
  for (Microsecond = 0; Microsecond < B2L_MSC_COMMAND_TIMEOUT_US;
       Microsecond++) {
    Value = Dwc3Read32 (DWC3_DEPCMD (Endpoint));
    if ((Value & DWC3_DEPCMD_CMDACT) == 0) {
      break;
    }
    gBS->Stall (1);
  }

  if (CompletedCommand != NULL) {
    *CompletedCommand = Value;
  }
  if ((Value & DWC3_DEPCMD_CMDACT) != 0) {
    Print (L"EP%u command timeout: cmd=%x reg=%08x\n",
           Endpoint, Command, Value);
    return EFI_TIMEOUT;
  }
  if ((Value & DWC3_DEPCMD_STATUS_MASK) != 0) {
    Print (L"EP%u command failed: cmd=%x reg=%08x\n",
           Endpoint, Command, Value);
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
PrepareTrb (
  IN OUT B2L_DWC3_TRB       *Trb,
  IN     EFI_PHYSICAL_ADDRESS Buffer,
  IN     UINT32               Length,
  IN     UINT32               Type
  )
{
  ZeroMem (Trb, sizeof (*Trb));
  Trb->BufferPointerLow  = (UINT32)Buffer;
  Trb->BufferPointerHigh = (UINT32)(Buffer >> 32);
  Trb->Size               = Length & DWC3_TRB_SIZE_MASK;
  Trb->Control            = Type | DWC3_TRB_CTRL_HWO |
                            DWC3_TRB_CTRL_ISP_IMI | DWC3_TRB_CTRL_IOC |
                            DWC3_TRB_CTRL_LST;
  WriteBackInvalidateDataCacheRange (Trb, sizeof (*Trb));
}

STATIC
EFI_STATUS
StartEndpointTransfer (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINTN                 Endpoint,
  IN     B2L_DWC3_TRB         *Trb,
  IN     EFI_PHYSICAL_ADDRESS  TrbAddress,
  IN     EFI_PHYSICAL_ADDRESS  BufferAddress,
  IN     UINT32                Length,
  IN     UINT32                Type
  )
{
  EFI_STATUS  Status;
  UINT32      Command;

  if ((Endpoint >= 4) || (Context->EpResource[Endpoint] != 0)) {
    return EFI_ALREADY_STARTED;
  }

  PrepareTrb (Trb, BufferAddress, Length, Type);
  Status = Dwc3SendEpCommand (
             Endpoint,
             DWC3_DEPCMD_STARTTRANSFER,
             (UINT32)(TrbAddress >> 32),
             (UINT32)TrbAddress,
             0,
             &Command
             );
  if (!EFI_ERROR (Status)) {
    Context->EpResource[Endpoint] = DWC3_DEPCMD_GET_RSC_IDX (Command);
    if (Context->EpResource[Endpoint] == 0) {
      Print (L"EP%u STARTTRANSFER resource index sifir.\n", Endpoint);
      return EFI_DEVICE_ERROR;
    }
  }
  return Status;
}

STATIC
VOID
EndEndpointTransfer (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINTN                 Endpoint
  )
{
  if ((Endpoint >= 4) || (Context->EpResource[Endpoint] == 0)) {
    return;
  }

  Dwc3SendEpCommand (
    Endpoint,
    DWC3_DEPCMD_ENDTRANSFER | DWC3_DEPCMD_HIPRI_FORCERM |
    DWC3_DEPCMD_PARAM (Context->EpResource[Endpoint]),
    0,
    0,
    0,
    NULL
    );
  Context->EpResource[Endpoint] = 0;
}

STATIC
BOOLEAN
PollQuitKey (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;

  if (B2LTouchUiPollStopButton ()) {
    return TRUE;
  }
  Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  return !EFI_ERROR (Status) &&
         ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == L'q') ||
          (Key.UnicodeChar == L'Q'));
}

STATIC
BOOLEAN
NextDwc3Event (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  OUT    UINT32               *Event
  )
{
  UINT32  Count;
  UINT32  EventBytes;

  Count      = Dwc3Read32 (DWC3_GEVNTCOUNT0);
  EventBytes = Count & DWC3_GEVNTCOUNT_MASK;
  if (EventBytes == 0) {
    return FALSE;
  }
  if ((EventBytes > DWC3_EVENT_BUFFER_SIZE) || ((EventBytes & 3U) != 0)) {
    Print (L"MSC event count gecersiz: %08x\n", Count);
    Context->StopRequested = TRUE;
    return FALSE;
  }

  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)(Context->DmaAddress + B2L_MSC_EVENT_OFFSET),
    DWC3_EVENT_BUFFER_SIZE
    );
  *Event = Context->EventWords[Context->EventReadPosition];
  Context->EventReadPosition =
    (Context->EventReadPosition + 1) % DWC3_EVENT_WORD_COUNT;
  Dwc3ProbeWrite32 (DWC3_GEVNTCOUNT0, sizeof (UINT32));
  return TRUE;
}

STATIC CONST UINT8  mDeviceDescriptor[] = {
  18, USB_DESC_TYPE_DEVICE, 0x00, 0x02,
  0x00, 0x00, 0x00, B2L_MSC_EP0_MAX_PACKET,
  0xE8, 0x04, 0xC1, 0xB2, 0x00, 0x01,
  1, 2, 3, 1
};

STATIC CONST UINT8  mConfigurationDescriptor[] = {
  9, USB_DESC_TYPE_CONFIGURATION, 32, 0, 1, 1, 0, 0xC0, 1,
  9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
  7, 5, 0x01, 0x02, 0x00, 0x02, 0,
  7, 5, 0x81, 0x02, 0x00, 0x02, 0
};

STATIC CONST UINT8  mDeviceQualifierDescriptor[] = {
  10, USB_DESC_TYPE_DEVICE_QUALIFIER, 0x00, 0x02,
  0x00, 0x00, 0x00, B2L_MSC_EP0_MAX_PACKET, 1, 0
};

STATIC
UINTN
BuildStringDescriptor (
  IN  UINT8  Index,
  OUT UINT8 *Buffer,
  IN  UINTN  Capacity
  )
{
  CONST CHAR16  *Text;
  UINTN          CharacterCount;
  UINTN          Position;

  if (Capacity < 4) {
    return 0;
  }
  if (Index == 0) {
    Buffer[0] = 4;
    Buffer[1] = USB_DESC_TYPE_STRING;
    Buffer[2] = 0x09;
    Buffer[3] = 0x04;
    return 4;
  }

  switch (Index) {
    case 1:
      Text = L"B2L UEFI";
      break;
    case 2:
      Text = L"CACHE FAT32";
      break;
    case 3:
      Text = L"B2L-CACHE-R13";
      break;
    default:
      return 0;
  }

  CharacterCount = StrLen (Text);
  if ((CharacterCount > 126) || ((2 + (CharacterCount * 2)) > Capacity)) {
    return 0;
  }
  Buffer[0] = (UINT8)(2 + (CharacterCount * 2));
  Buffer[1] = USB_DESC_TYPE_STRING;
  for (Position = 0; Position < CharacterCount; Position++) {
    Buffer[2 + (Position * 2)] = (UINT8)Text[Position];
    Buffer[3 + (Position * 2)] = (UINT8)(Text[Position] >> 8);
  }
  return Buffer[0];
}

STATIC
EFI_STATUS
ConfigureEndpoint (
  IN UINTN    Endpoint,
  IN UINT32   EndpointType,
  IN UINT32   MaxPacket,
  IN BOOLEAN  Modify
  )
{
  UINT32  Param0;
  UINT32  Param1;

  Param0 = DWC3_DEPCFG_EP_TYPE (EndpointType) |
           DWC3_DEPCFG_MAX_PACKET_SIZE (MaxPacket) |
           (Modify ? DWC3_DEPCFG_ACTION_MODIFY :
                     DWC3_DEPCFG_ACTION_INIT);
  if ((Endpoint & 1U) != 0) {
    Param0 |= DWC3_DEPCFG_FIFO_NUMBER (Endpoint >> 1);
  }

  Param1 = DWC3_DEPCFG_EP_NUMBER (Endpoint) |
           DWC3_DEPCFG_XFER_COMPLETE_EN;
  if (Endpoint <= 1) {
    Param1 |= DWC3_DEPCFG_XFER_NOT_READY_EN;
  } else {
    Param1 |= DWC3_DEPCFG_XFER_IN_PROGRESS_EN;
  }

  return Dwc3SendEpCommand (
           Endpoint,
           DWC3_DEPCMD_SETEPCONFIG,
           Param0,
           Param1,
           0,
           NULL
           );
}

STATIC
EFI_STATUS
ArmEp0Setup (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;

  if (Context->EpResource[0] != 0) {
    return EFI_ALREADY_STARTED;
  }
  ZeroMem (Context->ControlBuffer, sizeof (B2L_USB_SETUP_PACKET));
  WriteBackInvalidateDataCacheRange (
    Context->ControlBuffer,
    B2L_MSC_EP0_MAX_PACKET
    );
  Status = StartEndpointTransfer (
             Context,
             0,
             Context->Ep0Trb,
             Context->DmaAddress + B2L_MSC_EP0_TRB_OFFSET,
             Context->DmaAddress + B2L_MSC_CONTROL_OFFSET,
             sizeof (B2L_USB_SETUP_PACKET),
             DWC3_TRBCTL_CONTROL_SETUP
             );
  if (!EFI_ERROR (Status)) {
    Context->Ep0State = B2L_EP0_SETUP_ARMED;
  }
  return Status;
}

STATIC
EFI_STATUS
InitializeControlEndpoints (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINTN       Endpoint;

  Status = Dwc3SendEpCommand (
             0,
             DWC3_DEPCMD_DEPSTARTCFG,
             0,
             0,
             0,
             NULL
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Endpoint = 0; Endpoint < 4; Endpoint++) {
    Status = Dwc3SendEpCommand (
               Endpoint,
               DWC3_DEPCMD_SETTRANSFRESOURCE,
               DWC3_DEPXFERCFG_NUM_XFER_RES (1),
               0,
               0,
               NULL
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = ConfigureEndpoint (0, 0, B2L_MSC_EP0_MAX_PACKET, FALSE);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = ConfigureEndpoint (1, 0, B2L_MSC_EP0_MAX_PACKET, FALSE);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!Dwc3ProbeWrite32 (DWC3_DALEPENA, BIT0 | BIT1)) {
    return EFI_ACCESS_DENIED;
  }
  return ArmEp0Setup (Context);
}

STATIC
EFI_STATUS
InitializeBulkEndpoints (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;

  if (!Context->BulkInitialized) {
    Status = ConfigureEndpoint (2, 2, B2L_MSC_BULK_MAX_PACKET, FALSE);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Status = ConfigureEndpoint (3, 2, B2L_MSC_BULK_MAX_PACKET, FALSE);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Context->BulkInitialized = TRUE;
  }

  if (!Dwc3ProbeWrite32 (DWC3_DALEPENA, BIT0 | BIT1 | BIT2 | BIT3)) {
    return EFI_ACCESS_DENIED;
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StartControlIn (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     CONST VOID           *Data,
  IN     UINTN                 DataLength,
  IN     UINT16                RequestedLength
  )
{
  EFI_STATUS  Status;
  UINTN       Length;

  Length = MIN (DataLength, RequestedLength);
  if (Length > EFI_PAGE_SIZE) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if ((Length != 0) && (Data != Context->ControlBuffer)) {
    CopyMem (Context->ControlBuffer, Data, Length);
  }
  WriteBackInvalidateDataCacheRange (Context->ControlBuffer, Length);
  Status = StartEndpointTransfer (
             Context,
             1,
             Context->Ep0Trb,
             Context->DmaAddress + B2L_MSC_EP0_TRB_OFFSET,
             Context->DmaAddress + B2L_MSC_CONTROL_OFFSET,
             (UINT32)Length,
             DWC3_TRBCTL_CONTROL_DATA
             );
  if (!EFI_ERROR (Status)) {
    Context->Ep0ThreeStage = TRUE;
    Context->Ep0State      = B2L_EP0_DATA_IN;
  }
  return Status;
}

STATIC
VOID
StallEp0AndRestart (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  EndEndpointTransfer (Context, 0);
  EndEndpointTransfer (Context, 1);
  Dwc3SendEpCommand (0, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
  Context->Ep0State = B2L_EP0_SETUP_ARMED;
  ArmEp0Setup (Context);
}

STATIC
EFI_STATUS
StartControlStatus (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINTN                 Endpoint
  )
{
  EFI_STATUS  Status;
  UINT32      Type;

  if (Endpoint > 1) {
    return EFI_INVALID_PARAMETER;
  }
  Type = Context->Ep0ThreeStage ? DWC3_TRBCTL_CONTROL_STATUS3 :
                                  DWC3_TRBCTL_CONTROL_STATUS2;
  Status = StartEndpointTransfer (
             Context,
             Endpoint,
             Context->Ep0Trb,
             Context->DmaAddress + B2L_MSC_EP0_TRB_OFFSET,
             Context->DmaAddress + B2L_MSC_CONTROL_OFFSET,
             0,
             Type
             );
  if (!EFI_ERROR (Status)) {
    Context->Ep0State = B2L_EP0_STATUS;
  }
  return Status;
}

STATIC
EFI_STATUS
HandleSetupPacket (
  IN OUT B2L_USB_MSC_CONTEXT  *Context
  )
{
  B2L_USB_SETUP_PACKET  Setup;
  CONST VOID           *Descriptor;
  EFI_STATUS            Status;
  UINT32                Dcfg;
  UINTN                 DescriptorLength;
  UINT8                 DescriptorType;
  UINT8                 DescriptorIndex;
  UINT8                 Reply[2];

  WriteBackInvalidateDataCacheRange (
    Context->ControlBuffer,
    B2L_MSC_EP0_MAX_PACKET
    );
  CopyMem (&Setup, Context->ControlBuffer, sizeof (Setup));
  Context->Ep0ThreeStage = (Setup.Length != 0);

  if ((Setup.RequestType & USB_REQ_TYPE_TYPE_MASK) == USB_REQ_TYPE_STANDARD) {
    switch (Setup.Request) {
      case USB_REQ_GET_DESCRIPTOR:
        if ((Setup.RequestType & USB_REQ_TYPE_DIRECTION_IN) == 0) {
          break;
        }
        DescriptorType  = (UINT8)(Setup.Value >> 8);
        DescriptorIndex = (UINT8)Setup.Value;
        Descriptor       = NULL;
        DescriptorLength = 0;
        if (DescriptorType == USB_DESC_TYPE_DEVICE) {
          Descriptor       = mDeviceDescriptor;
          DescriptorLength = sizeof (mDeviceDescriptor);
        } else if ((DescriptorType == USB_DESC_TYPE_CONFIGURATION) ||
                   (DescriptorType == USB_DESC_TYPE_OTHER_SPEED)) {
          CopyMem (
            Context->ControlBuffer,
            mConfigurationDescriptor,
            sizeof (mConfigurationDescriptor)
            );
          if (DescriptorType == USB_DESC_TYPE_OTHER_SPEED) {
            Context->ControlBuffer[1] = USB_DESC_TYPE_OTHER_SPEED;
            // The other speed is Full-Speed; bulk max packet is 64 there.
            Context->ControlBuffer[22] = 64;
            Context->ControlBuffer[23] = 0;
            Context->ControlBuffer[29] = 64;
            Context->ControlBuffer[30] = 0;
          }
          Descriptor       = Context->ControlBuffer;
          DescriptorLength = sizeof (mConfigurationDescriptor);
        } else if (DescriptorType == USB_DESC_TYPE_STRING) {
          DescriptorLength = BuildStringDescriptor (
                               DescriptorIndex,
                               Context->ControlBuffer,
                               EFI_PAGE_SIZE
                               );
          Descriptor = Context->ControlBuffer;
        } else if (DescriptorType == USB_DESC_TYPE_DEVICE_QUALIFIER) {
          Descriptor       = mDeviceQualifierDescriptor;
          DescriptorLength = sizeof (mDeviceQualifierDescriptor);
        }
        if ((Descriptor == NULL) || (DescriptorLength == 0)) {
          break;
        }
        return StartControlIn (
                 Context,
                 Descriptor,
                 DescriptorLength,
                 Setup.Length
                 );

      case USB_REQ_SET_ADDRESS:
        if ((Setup.RequestType != 0) || (Setup.Length != 0) ||
            (Setup.Value > 127)) {
          break;
        }
        Dcfg  = Dwc3Read32 (DWC3_DCFG);
        Dcfg &= ~DWC3_DCFG_DEVADDR_MASK;
        Dcfg |= DWC3_DCFG_DEVADDR (Setup.Value);
        Dwc3ProbeWrite32 (DWC3_DCFG, Dcfg);
        Context->Ep0ThreeStage = FALSE;
        Context->Ep0State      = B2L_EP0_WAIT_STATUS;
        return EFI_SUCCESS;

      case USB_REQ_GET_CONFIGURATION:
        if ((Setup.RequestType & USB_REQ_TYPE_DIRECTION_IN) == 0) {
          break;
        }
        Reply[0] = Context->ConfigurationValue;
        return StartControlIn (Context, Reply, 1, Setup.Length);

      case USB_REQ_SET_CONFIGURATION:
        if ((Setup.RequestType != 0) || (Setup.Length != 0) ||
            (Setup.Value > 1)) {
          break;
        }
        if (Setup.Value == 1) {
          Status = InitializeBulkEndpoints (Context);
          if (EFI_ERROR (Status)) {
            return Status;
          }
          Context->Configured        = TRUE;
          Context->BusReset          = FALSE;
          Context->ConfigurationValue = 1;
        } else {
          EndEndpointTransfer (Context, 2);
          EndEndpointTransfer (Context, 3);
          Dwc3ProbeWrite32 (DWC3_DALEPENA, BIT0 | BIT1);
          Context->Configured         = FALSE;
          Context->ConfigurationValue = 0;
        }
        Context->Ep0ThreeStage = FALSE;
        Context->Ep0State      = B2L_EP0_WAIT_STATUS;
        return EFI_SUCCESS;

      case USB_REQ_GET_STATUS:
        if ((Setup.RequestType & USB_REQ_TYPE_DIRECTION_IN) == 0) {
          break;
        }
        Reply[0] = 0;
        Reply[1] = 0;
        return StartControlIn (Context, Reply, sizeof (Reply), Setup.Length);

      case USB_REQ_CLEAR_FEATURE: {
        UINTN  Endpoint;

        if (((Setup.RequestType & USB_REQ_TYPE_RECIPIENT_MASK) ==
             USB_REQ_RECIPIENT_ENDPOINT) &&
            (Setup.Value == USB_FEATURE_ENDPOINT_HALT) &&
            (Setup.Length == 0)) {
          Endpoint = (Setup.Index & 0x0FU) << 1;
          if ((Setup.Index & 0x80U) != 0) {
            Endpoint++;
          }
          if (Endpoint < 4) {
            Dwc3SendEpCommand (
              Endpoint,
              DWC3_DEPCMD_CLEARSTALL,
              0,
              0,
              0,
              NULL
              );
            Context->Ep0ThreeStage = FALSE;
            Context->Ep0State      = B2L_EP0_WAIT_STATUS;
            return EFI_SUCCESS;
          }
        }
        break;
      }

      case USB_REQ_SET_FEATURE:
        if ((Setup.Length == 0) &&
            ((Setup.RequestType & USB_REQ_TYPE_RECIPIENT_MASK) !=
             USB_REQ_RECIPIENT_ENDPOINT)) {
          Context->Ep0ThreeStage = FALSE;
          Context->Ep0State      = B2L_EP0_WAIT_STATUS;
          return EFI_SUCCESS;
        }
        break;

      case USB_REQ_GET_INTERFACE:
        if ((Setup.RequestType & USB_REQ_TYPE_DIRECTION_IN) != 0) {
          Reply[0] = 0;
          return StartControlIn (Context, Reply, 1, Setup.Length);
        }
        break;

      case USB_REQ_SET_INTERFACE:
        if ((Setup.Value == 0) && (Setup.Index == 0) &&
            (Setup.Length == 0)) {
          Context->Ep0ThreeStage = FALSE;
          Context->Ep0State      = B2L_EP0_WAIT_STATUS;
          return EFI_SUCCESS;
        }
        break;

      default:
        break;
    }
  } else if ((Setup.RequestType & USB_REQ_TYPE_TYPE_MASK) ==
             USB_REQ_TYPE_CLASS) {
    if ((Setup.Request == USB_MSC_GET_MAX_LUN) &&
        ((Setup.RequestType & USB_REQ_TYPE_DIRECTION_IN) != 0) &&
        (Setup.Value == 0) && (Setup.Index == 0) && (Setup.Length == 1)) {
      Reply[0] = 0;
      return StartControlIn (Context, Reply, 1, 1);
    }
    if ((Setup.Request == USB_MSC_BULK_RESET) &&
        ((Setup.RequestType & USB_REQ_TYPE_DIRECTION_IN) == 0) &&
        (Setup.Value == 0) && (Setup.Index == 0) && (Setup.Length == 0)) {
      EndEndpointTransfer (Context, 2);
      EndEndpointTransfer (Context, 3);
      Dwc3SendEpCommand (2, DWC3_DEPCMD_CLEARSTALL, 0, 0, 0, NULL);
      Dwc3SendEpCommand (3, DWC3_DEPCMD_CLEARSTALL, 0, 0, 0, NULL);
      Context->Ep0ThreeStage = FALSE;
      Context->Ep0State      = B2L_EP0_WAIT_STATUS;
      return EFI_SUCCESS;
    }
  }

  StallEp0AndRestart (Context);
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
HandleControlEndpointEvent (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINT32                Event
  )
{
  UINTN       Endpoint;
  UINTN       EventType;
  UINTN       EventStatus;
  EFI_STATUS  Status;

  Endpoint   = (Event >> 1) & 0x1FU;
  EventType  = (Event >> 6) & 0xFU;
  EventStatus = (Event >> 12) & 0xFU;
  if (Endpoint > 1) {
    return EFI_NOT_FOUND;
  }

  if (EventType == DWC3_DEPEVT_XFERCOMPLETE) {
    Context->EpResource[Endpoint] = 0;
    WriteBackInvalidateDataCacheRange (Context->Ep0Trb, sizeof (*Context->Ep0Trb));
    if ((Endpoint == 0) && (Context->Ep0State == B2L_EP0_SETUP_ARMED)) {
      return HandleSetupPacket (Context);
    }
    if ((Endpoint == 1) && (Context->Ep0State == B2L_EP0_DATA_IN)) {
      Context->Ep0State = B2L_EP0_WAIT_STATUS;
      return EFI_SUCCESS;
    }
    if (Context->Ep0State == B2L_EP0_STATUS) {
      Context->Ep0ThreeStage = FALSE;
      return ArmEp0Setup (Context);
    }
    return EFI_SUCCESS;
  }

  if ((EventType == DWC3_DEPEVT_XFERNOTREADY) &&
      (EventStatus == DWC3_DEPEVT_STATUS_CONTROL_STATUS) &&
      (Context->Ep0State == B2L_EP0_WAIT_STATUS)) {
    Status = StartControlStatus (Context, Endpoint);
    if (EFI_ERROR (Status)) {
      StallEp0AndRestart (Context);
    }
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
HandleDeviceEvent (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINT32                Event
  )
{
  UINTN   Type;
  UINT32  Dcfg;

  if (((Event >> 1) & 0x7FU) != 0) {
    return;
  }
  Type = (Event >> 8) & 0xFU;
  if (Type == 1) {
    Context->ResetCount++;
    Context->BusReset           = TRUE;
    Context->Configured         = FALSE;
    Context->ConfigurationValue = 0;
    EndEndpointTransfer (Context, 2);
    EndEndpointTransfer (Context, 3);
    Dcfg  = Dwc3Read32 (DWC3_DCFG);
    Dcfg &= ~DWC3_DCFG_DEVADDR_MASK;
    Dwc3ProbeWrite32 (DWC3_DCFG, Dcfg);
  } else if (Type == 2) {
    Context->ConnectDoneCount++;
    ConfigureEndpoint (0, 0, B2L_MSC_EP0_MAX_PACKET, TRUE);
    ConfigureEndpoint (1, 0, B2L_MSC_EP0_MAX_PACKET, TRUE);
  } else if (Type == 0) {
    if (Context->Configured || Context->WroteMedia) {
      Context->StopRequested = TRUE;
    }
  }
}

STATIC
EFI_STATUS
DispatchDwc3Event (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINT32                Event
  )
{
  if ((Event & BIT0) == 0) {
    if (((Event >> 1) & 0x1FU) <= 1) {
      return HandleControlEndpointEvent (Context, Event);
    }
    return EFI_SUCCESS;
  }

  HandleDeviceEvent (Context, Event);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
BulkTransfer (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     BOOLEAN               DirectionIn,
  IN     UINT32                Length,
  OUT    UINT32               *ActualLength
  )
{
  B2L_DWC3_TRB          *Trb;
  EFI_PHYSICAL_ADDRESS   TrbAddress;
  EFI_STATUS             Status;
  UINT32                 Event;
  UINT32                 EventType;
  UINT32                 Endpoint;
  UINT32                 Remaining;

  Endpoint   = DirectionIn ? 3U : 2U;
  Trb        = DirectionIn ? Context->Ep3Trb : Context->Ep2Trb;
  TrbAddress = Context->DmaAddress +
               (DirectionIn ? B2L_MSC_EP3_TRB_OFFSET :
                              B2L_MSC_EP2_TRB_OFFSET);
  *ActualLength = 0;

  if (Length > B2L_MSC_BULK_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if (!DirectionIn) {
    ZeroMem (Context->BulkBuffer, Length);
  }
  WriteBackInvalidateDataCacheRange (Context->BulkBuffer, Length);
  Status = StartEndpointTransfer (
             Context,
             Endpoint,
             Trb,
             TrbAddress,
             Context->DmaAddress + B2L_MSC_BULK_OFFSET,
             Length,
             DWC3_TRBCTL_NORMAL
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (;;) {
    if (PollQuitKey ()) {
      Context->StopRequested = TRUE;
      EndEndpointTransfer (Context, Endpoint);
      return EFI_ABORTED;
    }
    if (!NextDwc3Event (Context, &Event)) {
      if (Context->StopRequested) {
        EndEndpointTransfer (Context, Endpoint);
        return EFI_ABORTED;
      }
      gBS->Stall (100);
      continue;
    }

    if ((Event & BIT0) == 0) {
      Endpoint = (Event >> 1) & 0x1FU;
      EventType = (Event >> 6) & 0xFU;
      if ((Endpoint == (DirectionIn ? 3U : 2U)) &&
          (EventType == DWC3_DEPEVT_XFERCOMPLETE)) {
        Context->EpResource[Endpoint] = 0;
        WriteBackInvalidateDataCacheRange (Trb, sizeof (*Trb));
        Remaining = Trb->Size & DWC3_TRB_SIZE_MASK;
        if (Remaining <= Length) {
          *ActualLength = Length - Remaining;
        }
        if (!DirectionIn) {
          WriteBackInvalidateDataCacheRange (
            Context->BulkBuffer,
            *ActualLength
            );
        }
        return EFI_SUCCESS;
      }
    }

    Status = DispatchDwc3Event (Context, Event);
    if (EFI_ERROR (Status) && (Status != EFI_UNSUPPORTED)) {
      EndEndpointTransfer (Context, DirectionIn ? 3U : 2U);
      return Status;
    }
    if (Context->BusReset || Context->StopRequested) {
      EndEndpointTransfer (Context, DirectionIn ? 3U : 2U);
      return EFI_ABORTED;
    }
  }
}

STATIC
VOID
SetSense (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     UINT8                 Key,
  IN     UINT8                 Asc,
  IN     UINT8                 Ascq
  )
{
  Context->SenseKey  = Key;
  Context->SenseAsc  = Asc;
  Context->SenseAscq = Ascq;
}

STATIC
EFI_STATUS
SendScsiData (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     CONST B2L_USB_MSC_CBW *Cbw,
  IN OUT B2L_USB_MSC_CSW      *Csw,
  IN     CONST VOID           *Data,
  IN     UINT32                DataLength
  )
{
  EFI_STATUS  Status;
  UINT32      Actual;
  UINT32      Length;

  if (((Cbw->Flags & BIT7) == 0) && (Cbw->DataTransferLength != 0)) {
    Csw->Status = USB_MSC_CSW_PHASE_ERROR;
    return EFI_PROTOCOL_ERROR;
  }

  Length = MIN (DataLength, Cbw->DataTransferLength);
  if (Length > B2L_MSC_BULK_BYTES) {
    Csw->Status = USB_MSC_CSW_FAILED;
    return EFI_BAD_BUFFER_SIZE;
  }
  if ((Length != 0) && (Data != Context->BulkBuffer)) {
    CopyMem (Context->BulkBuffer, Data, Length);
  }
  if (Length == 0) {
    return EFI_SUCCESS;
  }

  Status = BulkTransfer (Context, TRUE, Length, &Actual);
  if (EFI_ERROR (Status) || (Actual != Length)) {
    Csw->Status = USB_MSC_CSW_FAILED;
    return EFI_DEVICE_ERROR;
  }
  Csw->DataResidue -= MIN (Csw->DataResidue, Actual);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TransferScsiBlocks (
  IN OUT B2L_USB_MSC_CONTEXT   *Context,
  IN     CONST B2L_USB_MSC_CBW *Cbw,
  IN OUT B2L_USB_MSC_CSW       *Csw,
  IN     BOOLEAN                WriteToMedia
  )
{
  EFI_BLOCK_IO_MEDIA  *Media;
  EFI_STATUS           Status;
  EFI_LBA              Lba;
  UINT32               Actual;
  UINT32               BlockCount;
  UINT32               ChunkBlocks;
  UINT32               ChunkBytes;
  UINT32               ExpectedBytes;

  Media      = Context->CacheBlockIo->Media;
  Lba        = ReadBe32 (&Cbw->CommandBlock[2]);
  BlockCount = ReadBe16 (&Cbw->CommandBlock[7]);
  ExpectedBytes = BlockCount * Media->BlockSize;

  if ((Cbw->DataTransferLength != ExpectedBytes) ||
      (((Cbw->Flags & BIT7) != 0) == WriteToMedia)) {
    Csw->Status = USB_MSC_CSW_PHASE_ERROR;
    SetSense (Context, 0x05, 0x24, 0x00);
    return EFI_PROTOCOL_ERROR;
  }
  if ((BlockCount != 0) &&
      ((Lba > Media->LastBlock) ||
       (BlockCount > (Media->LastBlock + 1ULL - Lba)))) {
    Csw->Status = USB_MSC_CSW_FAILED;
    SetSense (Context, 0x05, 0x21, 0x00);
    return EFI_INVALID_PARAMETER;
  }
  if (WriteToMedia && Context->ReadOnly) {
    Csw->Status = USB_MSC_CSW_FAILED;
    SetSense (Context, 0x07, 0x27, 0x00);
    return EFI_WRITE_PROTECTED;
  }

  while (BlockCount != 0) {
    ChunkBlocks = MIN (
                    BlockCount,
                    B2L_MSC_BULK_BYTES / Media->BlockSize
                    );
    ChunkBytes = ChunkBlocks * Media->BlockSize;
    if (WriteToMedia) {
      Status = BulkTransfer (Context, FALSE, ChunkBytes, &Actual);
      if (EFI_ERROR (Status) || (Actual != ChunkBytes)) {
        Csw->Status = USB_MSC_CSW_FAILED;
        SetSense (Context, 0x03, 0x0C, 0x00);
        return EFI_DEVICE_ERROR;
      }
      Status = Context->CacheBlockIo->WriteBlocks (
                                       Context->CacheBlockIo,
                                       Media->MediaId,
                                       Lba,
                                       ChunkBytes,
                                       Context->BulkBuffer
                                       );
      if (EFI_ERROR (Status)) {
        Csw->Status = USB_MSC_CSW_FAILED;
        SetSense (Context, 0x03, 0x0C, 0x00);
        return Status;
      }
      Context->WroteMedia    = TRUE;
      Context->BytesWritten += ChunkBytes;
    } else {
      Status = Context->CacheBlockIo->ReadBlocks (
                                       Context->CacheBlockIo,
                                       Media->MediaId,
                                       Lba,
                                       ChunkBytes,
                                       Context->BulkBuffer
                                       );
      if (EFI_ERROR (Status)) {
        Csw->Status = USB_MSC_CSW_FAILED;
        SetSense (Context, 0x03, 0x11, 0x00);
        return Status;
      }
      Status = BulkTransfer (Context, TRUE, ChunkBytes, &Actual);
      if (EFI_ERROR (Status) || (Actual != ChunkBytes)) {
        Csw->Status = USB_MSC_CSW_FAILED;
        return EFI_DEVICE_ERROR;
      }
      Context->BytesRead += ChunkBytes;
    }

    Csw->DataResidue -= MIN (Csw->DataResidue, ChunkBytes);
    Lba        += ChunkBlocks;
    BlockCount -= ChunkBlocks;
  }

  if (WriteToMedia) {
    Status = Context->CacheBlockIo->FlushBlocks (Context->CacheBlockIo);
    Context->FlushSucceeded = !EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      Csw->Status = USB_MSC_CSW_FAILED;
      SetSense (Context, 0x03, 0x0C, 0x02);
      return Status;
    }
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ExecuteScsiCommand (
  IN OUT B2L_USB_MSC_CONTEXT   *Context,
  IN     CONST B2L_USB_MSC_CBW *Cbw,
  IN OUT B2L_USB_MSC_CSW       *Csw
  )
{
  EFI_STATUS  Status;
  UINT8       Data[64];
  UINT8       Opcode;

  ZeroMem (Data, sizeof (Data));
  Opcode = Cbw->CommandBlock[0];
  Status = EFI_SUCCESS;
  switch (Opcode) {
    case SCSI_TEST_UNIT_READY:
      if (Cbw->DataTransferLength != 0) {
        Csw->Status = USB_MSC_CSW_PHASE_ERROR;
        return EFI_PROTOCOL_ERROR;
      }
      break;

    case SCSI_INQUIRY:
      Data[0] = 0x00;
      Data[1] = 0x80;
      Data[2] = 0x05;
      Data[3] = 0x02;
      Data[4] = 31;
      CopyMem (&Data[8],  "B2L UEFI", 8);
      CopyMem (&Data[16], "CACHE FAT32     ", 16);
      CopyMem (&Data[32], "R13 ", 4);
      Status = SendScsiData (Context, Cbw, Csw, Data, 36);
      break;

    case SCSI_REQUEST_SENSE:
      Data[0]  = 0x70;
      Data[2]  = Context->SenseKey;
      Data[7]  = 10;
      Data[12] = Context->SenseAsc;
      Data[13] = Context->SenseAscq;
      Status = SendScsiData (Context, Cbw, Csw, Data, 18);
      SetSense (Context, 0, 0, 0);
      break;

    case SCSI_MODE_SENSE_6:
      Data[0] = 3;
      Data[2] = Context->ReadOnly ? 0x80 : 0;
      Status = SendScsiData (Context, Cbw, Csw, Data, 4);
      break;

    case SCSI_MODE_SENSE_10:
      Data[1] = 6;
      Data[3] = Context->ReadOnly ? 0x80 : 0;
      Status = SendScsiData (Context, Cbw, Csw, Data, 8);
      break;

    case SCSI_READ_CAPACITY_10:
      WriteBe32 (&Data[0], (UINT32)Context->CacheBlockIo->Media->LastBlock);
      WriteBe32 (&Data[4], Context->CacheBlockIo->Media->BlockSize);
      Status = SendScsiData (Context, Cbw, Csw, Data, 8);
      break;

    case SCSI_READ_FORMAT_CAPACITIES:
      Data[3] = 8;
      WriteBe32 (
        &Data[4],
        (UINT32)(Context->CacheBlockIo->Media->LastBlock + 1ULL)
        );
      Data[8]  = 0x02;
      Data[9]  = (UINT8)(Context->CacheBlockIo->Media->BlockSize >> 16);
      Data[10] = (UINT8)(Context->CacheBlockIo->Media->BlockSize >> 8);
      Data[11] = (UINT8)Context->CacheBlockIo->Media->BlockSize;
      Status = SendScsiData (Context, Cbw, Csw, Data, 12);
      break;

    case SCSI_REPORT_LUNS:
      WriteBe32 (&Data[0], 8);
      Status = SendScsiData (Context, Cbw, Csw, Data, 16);
      break;

    case SCSI_READ_10:
      Status = TransferScsiBlocks (Context, Cbw, Csw, FALSE);
      break;

    case SCSI_WRITE_10:
      Status = TransferScsiBlocks (Context, Cbw, Csw, TRUE);
      break;

    case SCSI_VERIFY_10:
      if (Cbw->DataTransferLength != 0) {
        Csw->Status = USB_MSC_CSW_PHASE_ERROR;
        Status = EFI_PROTOCOL_ERROR;
      }
      break;

    case SCSI_SYNCHRONIZE_CACHE_10:
      Status = Context->CacheBlockIo->FlushBlocks (Context->CacheBlockIo);
      Context->FlushSucceeded = !EFI_ERROR (Status);
      if (EFI_ERROR (Status)) {
        Csw->Status = USB_MSC_CSW_FAILED;
        SetSense (Context, 0x03, 0x0C, 0x02);
      }
      break;

    case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
      break;

    case SCSI_START_STOP_UNIT:
      if ((Cbw->CommandBlock[4] & BIT1) != 0 &&
          (Cbw->CommandBlock[4] & BIT0) == 0) {
        Status = Context->CacheBlockIo->FlushBlocks (Context->CacheBlockIo);
        Context->FlushSucceeded = !EFI_ERROR (Status);
        if (!EFI_ERROR (Status)) {
          Context->Ejected = TRUE;
        }
      }
      break;

    default:
      Csw->Status = USB_MSC_CSW_FAILED;
      SetSense (Context, 0x05, 0x20, 0x00);
      Status = EFI_UNSUPPORTED;
      break;
  }

  if (EFI_ERROR (Status) && (Csw->Status == USB_MSC_CSW_PASSED)) {
    Csw->Status = USB_MSC_CSW_FAILED;
  }
  return Status;
}

STATIC
EFI_STATUS
SendCommandStatusWrapper (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     B2L_USB_MSC_CSW      *Csw
  )
{
  EFI_STATUS  Status;
  UINT32      Actual;

  CopyMem (Context->BulkBuffer, Csw, sizeof (*Csw));
  Status = BulkTransfer (Context, TRUE, sizeof (*Csw), &Actual);
  if (EFI_ERROR (Status) || (Actual != sizeof (*Csw))) {
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
ShowMassStorageStatus (
  IN CONST B2L_USB_MSC_CONTEXT  *Context,
  IN CONST B2L_USB_MSC_CBW      *LastCbw OPTIONAL
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L CACHE USB DISK R%u ===\n", B2L_USB_TEST_REVISION);
  Print (L"MOD=%s  USB=%s  RESET=%u CONNECT=%u\n",
         Context->ReadOnly ? L"SALT-OKUMA" : L"OKUMA/YAZMA",
         Context->Configured ? L"WINDOWS BAGLI" : L"ENUM BEKLENIYOR",
         Context->ResetCount, Context->ConnectDoneCount);
  Print (L"CACHE=600MiB FAT32  SEKTOR=4096  GPT YAZMASI YOK\n");
  Print (L"KOMUT=%u OKUNAN=%Lu YAZILAN=%Lu\n",
         Context->CommandCount, Context->BytesRead, Context->BytesWritten);
  if (LastCbw != NULL) {
    Print (L"SON SCSI=%02x TAG=%08x LEN=%u YON=%s\n",
           LastCbw->CommandBlock[0], LastCbw->Tag,
           LastCbw->DataTransferLength,
           (LastCbw->Flags & BIT7) != 0 ? L"IN" : L"OUT");
  }
  Print (L"\nWindows'ta disk gorununce dosya aktar. Bitince once Guvenle Cikar.\n");
  Print (L"Bu uygulama Windows'a geri donmez; sonunda donanim reboot gerekir.\n");
  Print (L"BIXBY/POWER uzun: acil bitir.\n");
  B2LTouchUiDrawStopButton (L"GUVENLE CIKARDIM - USB'YI BITIR");
}

STATIC
VOID
CacheMassStorageFailStopped (
  IN OUT B2L_USB_MSC_CONTEXT  *Context,
  IN     CONST CHAR16         *Reason
  )
{
  UINT32  Event;

  Context->CacheBlockIo->FlushBlocks (Context->CacheBlockIo);
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L CACHE USB KRITIK DURUS ===\n\n");
  Print (L"%s\n", Reason);
  Print (L"DMA alani %lx RESERVED; Windows'a devam ETME.\n",
         Context->DmaAddress);
  Print (L"Telefonu donanim tuslariyla yeniden baslat.\n");
  for (;;) {
    while (NextDwc3Event (Context, &Event)) {
      // Acknowledge only.  Never start a new transfer on the fail-stop path.
    }
    gBS->Stall (100000);
  }
}

STATIC
VOID
CacheMassStorageRebootScreen (
  IN CONST B2L_USB_MSC_CONTEXT  *Context
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== CACHE USB OTURUMU BITTI ===\n\n");
  Print (L"OKUNAN=%Lu  YAZILAN=%Lu  FLUSH=%s  DWC3=HALTED\n",
         Context->BytesRead, Context->BytesWritten,
         Context->FlushSucceeded ? L"OK" : L"HATA");
  Print (L"GPT degismedi; yalniz CACHE BlockIo kullanildi.\n\n");
  Print (L"Firmware FAT eslemesini yeniden kullanmamak icin Windows'a devam YOK.\n");
  Print (L"Simdi telefonu donanim tuslariyla yeniden baslat.\n");
  for (;;) {
    gBS->Stall (100000);
  }
}

EFI_STATUS
B2LDwc3RunCacheMassStorage (
  IN EFI_HANDLE  ImageHandle
  )
{
  B2L_DWC3_PROBE_SAVED  Saved;
  B2L_HS_PHY_SAVED      PhySaved;
  B2L_USB_MSC_CBW       Cbw;
  B2L_USB_MSC_CONTEXT   Context;
  B2L_USB_MSC_CSW       Csw;
  EFI_PHYSICAL_ADDRESS  DmaAddress;
  EFI_STATUS            Status;
  UINT32                Actual;
  UINT32                Dctl;
  UINT32                Dsts;
  UINT32                Event;
  UINTN                 Endpoint;
  UINTN                 Millisecond;
  BOOLEAN               ControllerRunning;
  BOOLEAN               PhyStateSaved;
  BOOLEAN               SessionStarted;

  ZeroMem (&Context, sizeof (Context));
  ZeroMem (&Saved, sizeof (Saved));
  ZeroMem (&PhySaved, sizeof (PhySaved));
  ControllerRunning = FALSE;
  PhyStateSaved      = FALSE;
  SessionStarted     = FALSE;

  Status = FindCachePartition (ImageHandle, &Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Context.CacheBlockIo->Media->IoAlign > EFI_PAGE_SIZE) {
    Print (L"RED: CACHE BlockIo IoAlign=%u > DMA page alignment.\n",
           Context.CacheBlockIo->Media->IoAlign);
    return EFI_UNSUPPORTED;
  }
  Status = PromptCacheAccessMode (&Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = Context.CacheBlockIo->FlushBlocks (Context.CacheBlockIo);
  if (EFI_ERROR (Status)) {
    Print (L"CACHE baslangic FlushBlocks basarisiz: %r\n", Status);
    return Status;
  }
  Context.FlushSucceeded = TRUE;

  DmaAddress = MAX_UINT32;
  Status = gBS->AllocatePages (
                  AllocateMaxAddress,
                  EfiReservedMemoryType,
                  EFI_SIZE_TO_PAGES (B2L_MSC_DMA_BYTES),
                  &DmaAddress
                  );
  if (EFI_ERROR (Status)) {
    Print (L"MSC DMA alani ayrilamadi: %r\n", Status);
    return Status;
  }

  Context.DmaAddress    = DmaAddress;
  Context.Dma           = (VOID *)(UINTN)DmaAddress;
  Context.EventWords    = (volatile UINT32 *)(UINTN)(
                            DmaAddress + B2L_MSC_EVENT_OFFSET
                            );
  Context.Ep0Trb        = (B2L_DWC3_TRB *)(UINTN)(
                            DmaAddress + B2L_MSC_EP0_TRB_OFFSET
                            );
  Context.Ep2Trb        = (B2L_DWC3_TRB *)(UINTN)(
                            DmaAddress + B2L_MSC_EP2_TRB_OFFSET
                            );
  Context.Ep3Trb        = (B2L_DWC3_TRB *)(UINTN)(
                            DmaAddress + B2L_MSC_EP3_TRB_OFFSET
                            );
  Context.ControlBuffer = (UINT8 *)(UINTN)(
                            DmaAddress + B2L_MSC_CONTROL_OFFSET
                            );
  Context.BulkBuffer    = (UINT8 *)(UINTN)(
                            DmaAddress + B2L_MSC_BULK_OFFSET
                            );
  ZeroMem (Context.Dma, B2L_MSC_DMA_BYTES);
  WriteBackInvalidateDataCacheRange (Context.Dma, B2L_MSC_DMA_BYTES);

  Status = ValidateCacheFat32 (&Context);
  if (EFI_ERROR (Status)) {
    goto FreeDma;
  }

  if (!PreflightIsSafe (DmaAddress)) {
    Status = EFI_NOT_READY;
    goto FreeDma;
  }

  Saved.EventAddressLow   = Dwc3Read32 (DWC3_GEVNTADRLO0);
  Saved.EventAddressHigh  = Dwc3Read32 (DWC3_GEVNTADRHI0);
  Saved.EventSize         = Dwc3Read32 (DWC3_GEVNTSIZ0);
  Saved.DeviceEventEnable = Dwc3Read32 (DWC3_DEVTEN);
  Saved.DeviceConfig      = Dwc3Read32 (DWC3_DCFG);
  Saved.DeviceControl     = Dwc3Read32 (DWC3_DCTL);
  SaveHsPhyState (&PhySaved);
  PhyStateSaved = TRUE;

  if (!EnableHsPhy (&PhySaved)) {
    Print (L"R13 CACHE MSC HS PHY enable dogrulamasi basarisiz.\n");
    Status = EFI_DEVICE_ERROR;
    goto RestoreHaltedController;
  }

  if (!Dwc3ProbeWrite32 (
         DWC3_DCFG,
         (Saved.DeviceConfig &
          ~(DWC3_DCFG_DEVSPD_MASK | DWC3_DCFG_DEVADDR_MASK)) |
         DWC3_DCFG_DEVSPD_HS
         ) ||
      !Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, (UINT32)DmaAddress) ||
      !Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, (UINT32)(DmaAddress >> 32)) ||
      !Dwc3ProbeWrite32 (
         DWC3_GEVNTSIZ0,
         DWC3_GEVNTSIZ_INTMASK |
         (DWC3_EVENT_BUFFER_SIZE & DWC3_GEVNTSIZ_SIZE_MASK)
         ) ||
      !Dwc3ProbeWrite32 (DWC3_DEVTEN, DWC3_PROBE_DEVICE_EVENTS)) {
    Status = EFI_ACCESS_DENIED;
    goto RestoreHaltedController;
  }

  Status = InitializeControlEndpoints (&Context);
  if (EFI_ERROR (Status)) {
    Print (L"R13 CACHE MSC EP0 baslatilamadi: %r\n", Status);
    goto RestoreHaltedController;
  }

  Dctl  = Saved.DeviceControl & ~DWC3_DCTL_ULSTCHNGREQ_MASK;
  Dctl |= DWC3_DCTL_ULSTCHNG_RX_DETECT;
  if (!Dwc3ProbeWrite32 (DWC3_DCTL, Dctl) ||
      !Dwc3ProbeWrite32 (DWC3_DCTL, Dctl | DWC3_DCTL_RUN_STOP)) {
    Status = EFI_ACCESS_DENIED;
    goto RestoreHaltedController;
  }
  ControllerRunning = TRUE;
  SessionStarted     = TRUE;

  for (Millisecond = 0; Millisecond < B2L_MSC_STOP_TIMEOUT_MS;
       Millisecond++) {
    Dsts = Dwc3Read32 (DWC3_DSTS);
    if ((Dsts & DWC3_DSTS_DEVCTRLHLT) == 0) {
      break;
    }
    gBS->Stall (1000);
  }
  if ((Dsts & DWC3_DSTS_DEVCTRLHLT) != 0) {
    Status = EFI_TIMEOUT;
    Context.StopRequested = TRUE;
    goto StopController;
  }

  ShowMassStorageStatus (&Context, NULL);
  for (;;) {
    while (!Context.Configured && !Context.StopRequested) {
      if (PollQuitKey ()) {
        Context.StopRequested = TRUE;
        break;
      }
      if (NextDwc3Event (&Context, &Event)) {
        DispatchDwc3Event (&Context, Event);
      } else {
        gBS->Stall (1000);
      }
    }
    if (Context.StopRequested) {
      break;
    }

    Context.BusReset = FALSE;
    Status = BulkTransfer (&Context, FALSE, B2L_MSC_BULK_MAX_PACKET, &Actual);
    if (EFI_ERROR (Status)) {
      if (Context.BusReset && !Context.StopRequested) {
        continue;
      }
      break;
    }
    if (Actual != sizeof (B2L_USB_MSC_CBW)) {
      Print (L"BOT CBW boyu gecersiz: %u (beklenen 31).\n", Actual);
      Dwc3SendEpCommand (2, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
      Dwc3SendEpCommand (3, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
      continue;
    }

    CopyMem (&Cbw, Context.BulkBuffer, sizeof (Cbw));
    if ((Cbw.Signature != USB_MSC_CBW_SIGNATURE) ||
        (Cbw.Lun != 0) || (Cbw.CommandLength == 0) ||
        (Cbw.CommandLength > sizeof (Cbw.CommandBlock)) ||
        ((Cbw.Flags & ~BIT7) != 0)) {
      Print (L"BOT CBW gecersiz: sig=%08x lun=%u cdb=%u flags=%02x.\n",
             Cbw.Signature, Cbw.Lun, Cbw.CommandLength, Cbw.Flags);
      Dwc3SendEpCommand (2, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
      Dwc3SendEpCommand (3, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
      continue;
    }

    ZeroMem (&Csw, sizeof (Csw));
    Csw.Signature   = USB_MSC_CSW_SIGNATURE;
    Csw.Tag         = Cbw.Tag;
    Csw.DataResidue = Cbw.DataTransferLength;
    Csw.Status      = USB_MSC_CSW_PASSED;
    Context.CommandCount++;
    ExecuteScsiCommand (&Context, &Cbw, &Csw);
    if (Context.BusReset || Context.StopRequested) {
      continue;
    }
    Status = SendCommandStatusWrapper (&Context, &Csw);
    if (EFI_ERROR (Status)) {
      break;
    }
    ShowMassStorageStatus (&Context, &Cbw);
    if (Context.Ejected) {
      Context.StopRequested = TRUE;
      break;
    }
  }

StopController:
  Status = Context.CacheBlockIo->FlushBlocks (Context.CacheBlockIo);
  Context.FlushSucceeded = !EFI_ERROR (Status);
  for (Endpoint = 0; Endpoint < 4; Endpoint++) {
    EndEndpointTransfer (&Context, Endpoint);
  }
  Dwc3ProbeWrite32 (DWC3_DALEPENA, 0);
  Dwc3ProbeWrite32 (DWC3_DEVTEN, 0);
  Dctl  = Dwc3Read32 (DWC3_DCTL);
  Dctl &= ~DWC3_DCTL_ULSTCHNGREQ_MASK;
  Dctl |= DWC3_DCTL_ULSTCHNG_RX_DETECT;
  Dwc3ProbeWrite32 (DWC3_DCTL, Dctl);
  Dwc3ProbeWrite32 (DWC3_DCTL, Dctl & ~DWC3_DCTL_RUN_STOP);
  gBS->Stall (50000);
  for (Millisecond = 0; Millisecond < B2L_MSC_STOP_TIMEOUT_MS;
       Millisecond++) {
    while (NextDwc3Event (&Context, &Event)) {
      // Stop path only acknowledges events; it must not arm new transfers.
    }
    Dsts = Dwc3Read32 (DWC3_DSTS);
    if ((Dsts & DWC3_DSTS_DEVCTRLHLT) != 0) {
      break;
    }
    gBS->Stall (1000);
  }
  if ((Dsts & DWC3_DSTS_DEVCTRLHLT) == 0) {
    CacheMassStorageFailStopped (
      &Context,
      L"DWC3 2 saniyede halt olmadi."
      );
  }
  ControllerRunning = FALSE;

RestoreHaltedController:
  if (ControllerRunning) {
    CacheMassStorageFailStopped (
      &Context,
      L"Hata yolunda DWC3 halen calisiyor."
      );
  }
  Dwc3ProbeWrite32 (DWC3_DEVTEN, 0);
  while (NextDwc3Event (&Context, &Event)) {
    // Acknowledge all controller-owned event entries before detaching DMA.
  }
  Dwc3ProbeWrite32 (DWC3_DALEPENA, 0);
  Dwc3ProbeWrite32 (DWC3_GEVNTSIZ0, DWC3_GEVNTSIZ_INTMASK);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRLO0, Saved.EventAddressLow);
  Dwc3ProbeWrite32 (DWC3_GEVNTADRHI0, Saved.EventAddressHigh);
  Dwc3ProbeWrite32 (DWC3_GEVNTSIZ0, Saved.EventSize);
  Dwc3ProbeWrite32 (DWC3_DEVTEN, Saved.DeviceEventEnable);
  Dwc3ProbeWrite32 (DWC3_DCFG, Saved.DeviceConfig);
  Dwc3ProbeWrite32 (DWC3_DCTL, Saved.DeviceControl);
  if (PhyStateSaved) {
    RestoreHsPhy (&PhySaved);
  }

FreeDma:
  WriteBackInvalidateDataCacheRange (Context.Dma, B2L_MSC_DMA_BYTES);
  gBS->FreePages (
         DmaAddress,
         EFI_SIZE_TO_PAGES (B2L_MSC_DMA_BYTES)
         );
  if (SessionStarted) {
    CacheMassStorageRebootScreen (&Context);
  }
  return Status;
}
