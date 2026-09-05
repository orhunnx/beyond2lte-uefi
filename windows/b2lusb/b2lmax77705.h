#ifndef B2LMAX77705_H
#define B2LMAX77705_H

#include <ntddk.h>
#include <wdf.h>
#include <Ucmcx.h>
#include "b2lmax77705_events.h"

#define B2L_MAX_CMGP_CMU_PHYSICAL_BASE     0x15C00000ULL
#define B2L_MAX_USI_PHYSICAL_BASE          0x15C22000ULL
#define B2L_MAX_CMGP_PINCTRL_PHYSICAL_BASE 0x15C30000ULL
#define B2L_MAX_HSI2C_PHYSICAL_BASE        0x15D60000ULL
#define B2L_MAX_HSI2C_GSIV                 114UL
#define B2L_MAX_GPIO_GSIV                  51UL
#define B2L_MAX_ALIVE_PHYSICAL_BASE        0x15850000ULL
#define B2L_MAX_CMGP_CMU_LENGTH            0x00008000UL
#define B2L_MAX_DEVICE_PAGE_LENGTH         0x00001000UL

#define B2L_MAX_TOP_ADDRESS          0x66U
#define B2L_MAX_USBC_ADDRESS         0x25U
#define B2L_MAX_CHARGER_ADDRESS      0x69U

#define B2L_MAX_IRQ_FRAMES 64UL
#define B2L_MAX_OPCODE_TIMEOUT_100NS 100000000ULL // Samsung 10*HZ queue deadline
#define B2L_MAX_IDLE_DIAGNOSTIC_POLLS 10UL
#define B2L_MAX_CC_STATUS_MASK       0x07U
#define B2L_MAX_CC_NO_CONNECTION     0x00U
#define B2L_MAX_CC_SINK              0x01U
#define B2L_MAX_CC_SOURCE            0x02U
#define B2L_MAX_DATA_UFP              0U
#define B2L_MAX_DATA_DFP              1U

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
#define B2L_MAX_STAGE_PD             0x69UL
#define B2L_MAX_STAGE_BYP            0x6AUL
#define B2L_MAX_STAGE_MUIC_DETECT    0x6BUL
#define B2L_MAX_STAGE_RID            0x6CUL
#define B2L_MAX_STAGE_PD_MESSAGE     0x6DUL
#define B2L_MAX_STAGE_PD_CAPS        0x6EUL
#define B2L_MAX_STAGE_READY          0x70UL
#define B2L_MAX_STAGE_GPIO_IRQ       0x24UL
#define B2L_MAX_STAGE_MFD_IRQ        0x34UL

#define B2L_MAX_HSI2C_PHASE_NONE     0x00UL
#define B2L_MAX_HSI2C_PHASE_PLATFORM 0x08UL
#define B2L_MAX_HSI2C_PHASE_BUS_IDLE 0x10UL
#define B2L_MAX_HSI2C_PHASE_WRITE    0x20UL
#define B2L_MAX_HSI2C_PHASE_READ     0x30UL

// Diagnostic IDs, not Samsung's muic_attached_dev_t ABI.
#define B2L_MAX_MUIC_NONE             0UL
#define B2L_MAX_MUIC_USB              1UL
#define B2L_MAX_MUIC_CDP              2UL
#define B2L_MAX_MUIC_OTG              3UL
#define B2L_MAX_MUIC_TA               4UL
#define B2L_MAX_MUIC_DCD_TIMEOUT      5UL
#define B2L_MAX_MUIC_UNOFFICIAL_TA    6UL
#define B2L_MAX_MUIC_JIG_USB_OFF      7UL
#define B2L_MAX_MUIC_JIG_USB_ON       8UL
#define B2L_MAX_MUIC_JIG_UART_OFF     9UL
#define B2L_MAX_MUIC_JIG_UART_ON     10UL
#define B2L_MAX_MUIC_INITIAL       0x100UL
#define B2L_MAX_MUIC_CC_NOTIFY     0x200UL
#define B2L_MAX_MUIC_RID_NOTIFY    0x400UL

#define B2L_MAX_CONNECTOR_EVENTS 64UL
typedef struct _B2L_MAX_CONNECTOR_EVENT {
    BOOLEAN Attached;
    UCHAR DataRole, PowerRole, CcStatus0;
} B2L_MAX_CONNECTOR_EVENT;

// Requests belong to a waiting probe/shutdown caller or a fixed MUIC slot.
// Queue ownership and slot reuse are protected by the passive ISR MfdLock.
typedef struct _B2L_MAX_OPCODE_REQUEST {
    struct _B2L_MAX_OPCODE_REQUEST *Next;
    struct _B2L_MAX_MUIC_PAIR *MuicPair;
    struct _B2L_MAX_PD_REQUEST *PdRequest;
    KEVENT Completion;
    ULONGLONG SentAt;
    NTSTATUS Status;
    BOOLEAN Complete;
    UCHAR Opcode, WriteLength, ReadLength, WriteData, ResponseData;
    UCHAR Response[33]; // opcode plus up to 32 bytes, MAX77705 mailbox limit
} B2L_MAX_OPCODE_REQUEST, *PB2L_MAX_OPCODE_REQUEST;

#define B2L_MAX_PD_SLOTS 16UL
typedef struct _B2L_MAX_PD_REQUEST {
    B2L_MAX_OPCODE_REQUEST Request;
    ULONGLONG Generation;
    BOOLEAN InUse;
} B2L_MAX_PD_REQUEST, *PB2L_MAX_PD_REQUEST;

#define B2L_MAX_MUIC_SLOTS 32UL
typedef struct _B2L_MAX_MUIC_PAIR {
    B2L_MAX_OPCODE_REQUEST Write, Read;
    ULONGLONG Generation;
    UCHAR Path;
    BOOLEAN InUse;
} B2L_MAX_MUIC_PAIR, *PB2L_MAX_MUIC_PAIR;

typedef struct _B2L_MAX_IRQ_FRAME {
    ULONGLONG Time;
    UCHAR Top, Raw[4], Active[4], Charger;
    UCHAR Status[8];
    NTSTATUS SnapshotStatus;
    BOOLEAN SnapshotAttempted;
    BOOLEAN DetachRecheck;
} B2L_MAX_IRQ_FRAME, *PB2L_MAX_IRQ_FRAME;

typedef struct _B2L_MAX_CONTEXT {
    WDFDEVICE Device;
    WDFWORKITEM PolicyWork;
    UCMCONNECTOR Connector;
    WDFWORKITEM ConnectorWork;
    B2L_MAX_CONNECTOR_EVENT ConnectorEvents[B2L_MAX_CONNECTOR_EVENTS];
    B2L_MAX_CONNECTOR_EVENT ConnectorLast, ConnectorReported;
    BOOLEAN ConnectorRunning, ConnectorAttached, ConnectorOverflow;
    ULONG ConnectorRead, ConnectorCount, ConnectorQueued, ConnectorAttempts;
    NTSTATUS ConnectorStatus, ConnectorSwapStatus;
    WDFTIMER PdRecoveryTimer;
    B2L_MAX_PD_REQUEST PdRequests[B2L_MAX_PD_SLOTS];
    ULONGLONG PdGeneration, PdRecoveryDeadline;
    ULONG PdMessageEvents, PdUnhandledEvents, PsRdyEvents;
    ULONG PdReadsQueued, PdReadsPending, PdReadCompletions, PdStaleResponses;
    ULONG PdPdos[7], PdRecoveryAttempts;
    UCHAR PdMessage, PsRdyRaw, PdPrSwap, PdCapsHeader, PdPdoCount, PdCurrentPdo;
    BOOLEAN PsRdyReceived, PdSupported, PdCapsValid, PdRecoveryPending;
    NTSTATUS PdMessageStatus, PsRdyStatus, PdCapsStatus, PdRecoveryStatus;
    WDFTIMER OpcodeDeadline;
    B2L_MAX_MUIC_PAIR MuicPairs[B2L_MAX_MUIC_SLOTS];
    ULONGLONG MuicGeneration;
    ULONG MuicOutstanding;
    NTSTATUS MuicStatus;
    UCHAR MuicSample[5], MuicAdc, MuicEffectiveBc, MuicChargerType;
    UCHAR RidRaw, MuicRid;
    BOOLEAN MuicSampleValid, MuicClassified, MuicRprd;
    ULONG MuicType, MuicCause, MuicSamples, RidChanges;
    NTSTATUS MuicDetectStatus;
    WDFWAITLOCK TransportLock;
    WDFWAITLOCK MfdLock;
    WDFINTERRUPT MaxInterrupt;
    PUCHAR AliveRegisters;
    ULONG AliveLength, MaxRawIrq, MaxTranslatedIrq;
    ULONG MaxInterruptCount, MaxInterruptFailures, MaxLastInterruptStatus;
    ULONG OpcodeCompletions, OpcodeUnexpected, IrqFrameOverflows;
    BOOLEAN MaxIrqEnabled, MfdReady, UsbcInitialized, PolicyEnabled;
    NTSTATUS MfdFault;
    PB2L_MAX_OPCODE_REQUEST OpcodeHead, OpcodeTail;
    B2L_MAX_IRQ_FRAME IrqFrames[B2L_MAX_IRQ_FRAMES];
    ULONG IrqFrameRead, IrqFrameCount;
    WDFWAITLOCK BusLock;
    WDFINTERRUPT HsI2cInterrupt;
    KEVENT HsI2cCompletion;
    // Protected by the HSI2C interrupt lock; TransportLock serializes callers.
    PUCHAR HsI2cMessageBuffer;
    ULONG HsI2cMessageLength;
    ULONG HsI2cMessagePosition;
    NTSTATUS HsI2cMessageStatus;
    BOOLEAN HsI2cMessageRead;
    BOOLEAN HsI2cMessageActive;
    BOOLEAN HsI2cIrqEnabled;
    ULONG HsI2cRawIrq;
    ULONG HsI2cTranslatedIrq;
    volatile ULONG HsI2cInterruptCount;
    volatile ULONG HsI2cCompletionCount;
    volatile ULONG HsI2cLastInterruptStatus;
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
    BOOLEAN PowerRoleValid, DataRoleValid, WaterDetected;
    UCHAR PowerRole, DataRole, PdStatus1;
    ULONG DataRoleEvents, DataRoleChanges, OvercurrentEvents;
    ULONG DetachRechecks;
    NTSTATUS DataRoleStatus;
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
    ULONG HsI2cReadFailures;
    ULONG HsI2cLastReadFailureStatus;
    ULONG HsI2cLastReadFailureLength;
    ULONG HsI2cLastReadFailureTransferred;
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
    B2L_MAX_EVENT_LOG EventLog;
    BOOLEAN EventLogDirty;
    BOOLEAN EventCcValid;
    USHORT EventLastCc;
} B2L_MAX_CONTEXT, *PB2L_MAX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(B2L_MAX_CONTEXT, B2lMaxGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD B2lMaxEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE B2lMaxEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE B2lMaxEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY B2lMaxEvtD0Entry;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED B2lMaxEvtD0EntryPostInterruptsEnabled;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED B2lMaxEvtD0ExitPreInterruptsDisabled;
EVT_WDF_WORKITEM B2lMaxEvtPolicyWork;
EVT_WDF_INTERRUPT_ISR B2lMaxEvtMaxInterruptIsr;
EVT_WDF_INTERRUPT_ENABLE B2lMaxEvtMaxInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE B2lMaxEvtMaxInterruptDisable;

EVT_WDF_TIMER B2lMaxEvtOpcodeDeadline;
NTSTATUS B2lMaxRequestMuicLocked(PB2L_MAX_CONTEXT Context, UCHAR Path);
NTSTATUS B2lMaxReapplyMuicLocked(PB2L_MAX_CONTEXT Context, UCHAR Path);
NTSTATUS B2lMaxDispatchBypassLocked(PB2L_MAX_CONTEXT Context);
VOID B2lMaxObserveIrqLocked(PB2L_MAX_CONTEXT Context, const B2L_MAX_IRQ_FRAME *Frame);
VOID B2lMaxDispatchCcPdLocked(PB2L_MAX_CONTEXT Context, const B2L_MAX_IRQ_FRAME *Frame);
VOID B2lMaxDispatchMuicLocked(PB2L_MAX_CONTEXT Context, const B2L_MAX_IRQ_FRAME *Frame);
NTSTATUS B2lMaxPrepareGpio(PB2L_MAX_CONTEXT Context);
VOID B2lMaxGpioMask(PB2L_MAX_CONTEXT Context);
VOID B2lMaxGpioUnmask(PB2L_MAX_CONTEXT Context);
VOID B2lMaxCancelOpcodesLocked(PB2L_MAX_CONTEXT Context, NTSTATUS Status);
NTSTATUS B2lMaxQueueOpcodeLocked(PB2L_MAX_CONTEXT Context, PB2L_MAX_OPCODE_REQUEST Request);
NTSTATUS B2lMaxWaitOpcode(PB2L_MAX_CONTEXT Context, PB2L_MAX_OPCODE_REQUEST Request);
VOID B2lMaxInitOpcodeRequest(PB2L_MAX_OPCODE_REQUEST Request, UCHAR Opcode,
    UCHAR WriteLength, UCHAR WriteData, UCHAR ReadLength);
NTSTATUS B2lMaxReadByte(PB2L_MAX_CONTEXT Context, UCHAR Address, UCHAR Register, UCHAR *Value);
NTSTATUS B2lMaxReadBlock(PB2L_MAX_CONTEXT Context, UCHAR Address, UCHAR Register, UCHAR *Values, ULONG Count);
NTSTATUS B2lMaxWriteByte(PB2L_MAX_CONTEXT Context, UCHAR Address, UCHAR Register, UCHAR Value);
NTSTATUS B2lMaxWriteBlock(PB2L_MAX_CONTEXT Context, UCHAR Address, UCHAR Register, const UCHAR *Values, ULONG Count);
EVT_WDF_INTERRUPT_ISR B2lMaxHsI2cEvtInterruptIsr;
EVT_WDF_INTERRUPT_DPC B2lMaxHsI2cEvtInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE B2lMaxHsI2cEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE B2lMaxHsI2cEvtInterruptDisable;

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

EVT_UCM_CONNECTOR_SET_DATA_ROLE B2lMaxEvtSetDataRole;
EVT_WDF_WORKITEM B2lMaxEvtConnectorWork;
NTSTATUS B2lMaxCreateConnector(PB2L_MAX_CONTEXT Context);
VOID B2lMaxQueueConnectorLocked(PB2L_MAX_CONTEXT Context);
VOID B2lMaxStopConnector(PB2L_MAX_CONTEXT Context);
NTSTATUS B2lMaxEnableOtg(PB2L_MAX_CONTEXT Context);
NTSTATUS B2lMaxDisableOtg(PB2L_MAX_CONTEXT Context);
NTSTATUS B2lMaxHandlePdMessageLocked(PB2L_MAX_CONTEXT Context);
NTSTATUS B2lMaxHandlePsRdyLocked(PB2L_MAX_CONTEXT Context);
VOID B2lMaxRetirePdRequestLocked(PB2L_MAX_CONTEXT Context, PB2L_MAX_PD_REQUEST Request, NTSTATUS Status);
VOID B2lMaxResetPdConnectionLocked(PB2L_MAX_CONTEXT Context);
VOID B2lMaxCancelPdRecoveryLocked(PB2L_MAX_CONTEXT Context, NTSTATUS Status);
EVT_WDF_TIMER B2lMaxEvtPdRecoveryTimer;

#endif
