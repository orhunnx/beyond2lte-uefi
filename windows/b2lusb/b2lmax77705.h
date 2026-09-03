#ifndef B2LMAX77705_H
#define B2LMAX77705_H

#include <ntddk.h>
#include <wdf.h>

#define B2L_MAX_HSI2C_PHYSICAL_BASE 0x15D60000ULL
#define B2L_MAX_USI_PHYSICAL_BASE    0x15C22000ULL
#define B2L_MAX_DEVICE_PAGE_LENGTH   0x00001000UL

#define B2L_MAX_TOP_ADDRESS          0x66U
#define B2L_MAX_USBC_ADDRESS         0x25U
#define B2L_MAX_CHARGER_ADDRESS      0x69U

#define B2L_MAX_POLL_MS              200UL
#define B2L_MAX_CC_STATUS_MASK       0x07U
#define B2L_MAX_CC_NO_CONNECTION     0x00U
#define B2L_MAX_CC_SINK              0x01U
#define B2L_MAX_CC_SOURCE            0x02U

#define B2L_MAX_STAGE_RESOURCES      0x10UL
#define B2L_MAX_STAGE_BUS            0x20UL
#define B2L_MAX_STAGE_IDENTITY       0x30UL
#define B2L_MAX_STAGE_CC             0x40UL
#define B2L_MAX_STAGE_AUTO_VBUS      0x48UL
#define B2L_MAX_STAGE_OTG_ON         0x50UL
#define B2L_MAX_STAGE_OTG_OFF        0x60UL
#define B2L_MAX_STAGE_READY          0x70UL

typedef struct _B2L_MAX_CONTEXT {
    WDFDEVICE Device;
    WDFTIMER PollTimer;
    WDFWAITLOCK BusLock;
    PUCHAR HsI2cRegisters;
    PUCHAR UsiRegisters;
    ULONG HsI2cLength;
    ULONG UsiLength;
    BOOLEAN HardwareReady;
    BOOLEAN IdentityReady;
    BOOLEAN PollingEnabled;
    BOOLEAN OtgEnabled;
    BOOLEAN AutoVbusDisabled;
    BOOLEAN BaselineValid;
    UCHAR PmicId;
    UCHAR PmicRev;
    UCHAR UsbcHwRev;
    UCHAR UsbcFwRev;
    UCHAR UsbcFwMinor;
    UCHAR CcStatus0;
    UCHAR CcStatus1;
    UCHAR BcStatus;
    UCHAR ChargerIntMaskBaseline;
    UCHAR ChargerModeBaseline;
    UCHAR ChargerIntMask;
    UCHAR ChargerMode;
    UCHAR ChargerIntOk;
    UCHAR ChargerDetails[3];
    UCHAR OpcodeResponse;
    ULONG HsI2cClockControl;
    ULONG HsI2cIntStatus;
    ULONG HsI2cErrorStatus;
    ULONG HsI2cTransferStatus;
    ULONG HsI2cFifoStatus;
    ULONG HsI2cTimingFs[3];
    ULONG HsI2cRetryCount;
    ULONG DiagnosticStage;
    NTSTATUS DiagnosticStatus;
} B2L_MAX_CONTEXT, *PB2L_MAX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(B2L_MAX_CONTEXT, B2lMaxGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD B2lMaxEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE B2lMaxEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE B2lMaxEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY B2lMaxEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT B2lMaxEvtD0Exit;
EVT_WDF_TIMER B2lMaxEvtPollTimer;

NTSTATUS B2lMaxHsI2cInitialize(PB2L_MAX_CONTEXT Context);
VOID B2lMaxHsI2cQuiesce(PB2L_MAX_CONTEXT Context);
NTSTATUS B2lMaxHsI2cWriteRead(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    const UCHAR *WriteBuffer,
    ULONG WriteLength,
    UCHAR *ReadBuffer,
    ULONG ReadLength
    );

#endif
