#ifndef B2LMAX77705_H
#define B2LMAX77705_H

#include <ntddk.h>
#include <wdf.h>

#define B2L_MAX_CMGP_CMU_PHYSICAL_BASE     0x15C00000ULL
#define B2L_MAX_USI_PHYSICAL_BASE          0x15C22000ULL
#define B2L_MAX_CMGP_PINCTRL_PHYSICAL_BASE 0x15C30000ULL
#define B2L_MAX_HSI2C_PHYSICAL_BASE        0x15D60000ULL
#define B2L_MAX_CMGP_CMU_LENGTH            0x00008000UL
#define B2L_MAX_DEVICE_PAGE_LENGTH         0x00001000UL

#define B2L_MAX_TOP_ADDRESS          0x66U
#define B2L_MAX_USBC_ADDRESS         0x25U
#define B2L_MAX_CHARGER_ADDRESS      0x69U

#define B2L_MAX_INITIAL_POLL_MS      1UL
#define B2L_MAX_ATTACH_POLL_MS       20UL
#define B2L_MAX_ATTACH_FAST_POLLS    50UL
#define B2L_MAX_POLL_MS              200UL
#define B2L_MAX_RETRY_POLL_MS        2000UL
#define B2L_MAX_IDLE_DIAGNOSTIC_POLLS 10UL
#define B2L_MAX_CC_STATUS_MASK       0x07U
#define B2L_MAX_CC_NO_CONNECTION     0x00U
#define B2L_MAX_CC_SINK              0x01U
#define B2L_MAX_CC_SOURCE            0x02U

#define B2L_MAX_STAGE_RESOURCES      0x10UL
#define B2L_MAX_STAGE_BUS            0x20UL
#define B2L_MAX_STAGE_IDENTITY       0x30UL
#define B2L_MAX_STAGE_CHARGER_UNLOCK 0x38UL
#define B2L_MAX_STAGE_AUTO_IBUS      0x40UL
#define B2L_MAX_STAGE_USBC_MASKS     0x44UL
#define B2L_MAX_STAGE_AUTO_VBUS      0x48UL
#define B2L_MAX_STAGE_CC_CONTROL     0x4CUL
#define B2L_MAX_STAGE_RUNTIME_MASKS  0x4DUL
#define B2L_MAX_STAGE_MUIC_USB       0x4EUL
#define B2L_MAX_STAGE_CC_OPEN        0x4FUL
#define B2L_MAX_STAGE_OTG_ON         0x50UL
#define B2L_MAX_STAGE_OTG_OFF        0x60UL
#define B2L_MAX_STAGE_MUIC_OPEN      0x64UL
#define B2L_MAX_STAGE_CC             0x68UL
#define B2L_MAX_STAGE_READY          0x70UL

#define B2L_MAX_HSI2C_PHASE_NONE     0x00UL
#define B2L_MAX_HSI2C_PHASE_PLATFORM 0x08UL
#define B2L_MAX_HSI2C_PHASE_BUS_IDLE 0x10UL
#define B2L_MAX_HSI2C_PHASE_WRITE    0x20UL
#define B2L_MAX_HSI2C_PHASE_READ     0x30UL

typedef struct _B2L_MAX_CONTEXT {
    WDFDEVICE Device;
    WDFTIMER PollTimer;
    WDFWAITLOCK BusLock;
    PUCHAR HsI2cRegisters;
    PUCHAR UsiRegisters;
    PUCHAR CmgpCmuRegisters;
    PUCHAR CmgpPinctrlRegisters;
    ULONG HsI2cLength;
    ULONG UsiLength;
    ULONG CmgpCmuLength;
    ULONG CmgpPinctrlLength;
    BOOLEAN HardwareReady;
    BOOLEAN PlatformPrepared;
    BOOLEAN IdentityReady;
    BOOLEAN PollingEnabled;
    BOOLEAN OtgEnabled;
    BOOLEAN OtgHardwareActive;
    BOOLEAN ChargerUnlocked;
    BOOLEAN AutoVbusDisabled;
    BOOLEAN AutoIbusConfigured;
    BOOLEAN UsbcMasksConfigured;
    BOOLEAN UsbcRuntimeMasksConfigured;
    BOOLEAN CcControlConfigured;
    BOOLEAN CcOpenNormalized;
    BOOLEAN MuicPathVerified;
    BOOLEAN BaselineValid;
    BOOLEAN CcStatusValid;
    BOOLEAN CcStateChanged;
    BOOLEAN DiagnosticDirty;
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
    UCHAR ChargerConfig06;
    UCHAR ChargerIntOk;
    UCHAR ChargerDetails[3];
    UCHAR OpcodeResponse;
    UCHAR AutoIbusMode;
    UCHAR UicInterrupt;
    UCHAR CcInterrupt;
    UCHAR PdInterrupt;
    UCHAR VdmInterrupt;
    UCHAR UicInterruptSeen;
    UCHAR CcInterruptSeen;
    UCHAR PdInterruptSeen;
    UCHAR VdmInterruptSeen;
    UCHAR TopInterruptSource;
    UCHAR TopInterruptSourceSeen;
    UCHAR TopInterruptSourceMask;
    UCHAR CcState;
    UCHAR PreviousCcState;
    UCHAR UicInterruptMask;
    UCHAR CcInterruptMask;
    UCHAR PdInterruptMask;
    UCHAR VdmInterruptMask;
    UCHAR CcControlResponse;
    UCHAR CcControlReadBack;
    UCHAR Control3Before;
    UCHAR Control3After;
    UCHAR MuicPathRequested;
    UCHAR MuicControl1;
    ULONG HsI2cControl;
    ULONG HsI2cClockControl;
    ULONG HsI2cIntStatus;
    ULONG HsI2cErrorStatus;
    ULONG HsI2cTransferStatus;
    ULONG HsI2cFifoStatus;
    ULONG HsI2cTimingFs[3];
    ULONG HsI2cConfiguration;
    ULONG HsI2cAutoConfiguration;
    ULONG HsI2cAddress;
    ULONG HsI2cUsiControl;
    ULONG HsI2cAttemptedAddress;
    ULONG HsI2cRetryCount;
    ULONG HsI2cFailurePhase;
    ULONG CmgpMuxUsi3;
    ULONG CmgpDivUsi3;
    ULONG CmgpQchUsi3;
    ULONG Gpm12Con;
    ULONG Gpm13Con;
    ULONG ChargerUnlockAttempts;
    ULONG CcSampleCount;
    ULONG CcTransitionCount;
    ULONG CcInterruptCount;
    ULONG CcSourceEventCount;
    ULONG DiagnosticPollCount;
    ULONGLONG CcLastSampleTime;
    ULONGLONG CcLastTransitionTime;
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
