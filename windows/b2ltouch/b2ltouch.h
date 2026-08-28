#ifndef B2LTOUCH_H
#define B2LTOUCH_H

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#include "b2ltouch_parser.h"

#define B2L_HSI2C_PHYSICAL_BASE 0x104B0000ULL
#define B2L_USI_PHYSICAL_BASE    0x10421000ULL
#define B2L_DEVICE_PAGE_LENGTH   0x00001000UL

#define B2L_HSI2C_SLAVE          0x48U
#define B2L_TOUCH_POLL_MS        20UL
#define B2L_TOUCH_MAX_EVENTS     32U

#pragma pack(push, 1)
typedef struct _B2L_TOUCH_REPORT {
    UCHAR ReportId;
    UCHAR Tip;
    UCHAR ContactId;
    USHORT X;
    USHORT Y;
    UCHAR ContactCount;
} B2L_TOUCH_REPORT;
#pragma pack(pop)

typedef struct _B2L_TOUCH_CONTEXT {
    WDFDEVICE Device;
    WDFTIMER PollTimer;
    VHFHANDLE VhfHandle;
    PUCHAR HsI2cRegisters;
    PUCHAR UsiRegisters;
    ULONG HsI2cLength;
    ULONG UsiLength;
    BOOLEAN HardwareReady;
    BOOLEAN TouchReady;
    ULONG ConsecutiveErrors;
    B2L_SEC_TS_POINTER Pointer;
    UCHAR HidContactId;
    B2L_TOUCH_REPORT Report;
    HID_XFER_PACKET ReportPacket;
} B2L_TOUCH_CONTEXT, *PB2L_TOUCH_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(B2L_TOUCH_CONTEXT, B2lTouchGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD B2lTouchEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE B2lTouchEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE B2lTouchEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY B2lTouchEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT B2lTouchEvtD0Exit;
EVT_WDF_TIMER B2lTouchEvtTimer;
EVT_WDF_OBJECT_CONTEXT_CLEANUP B2lTouchEvtCleanup;
EVT_VHF_ASYNC_OPERATION B2lTouchEvtGetFeature;

NTSTATUS B2lHsI2cInitialize(PB2L_TOUCH_CONTEXT Context);
VOID B2lHsI2cQuiesce(PB2L_TOUCH_CONTEXT Context);
NTSTATUS B2lHsI2cWriteRead(
    PB2L_TOUCH_CONTEXT Context,
    const UCHAR *WriteBuffer,
    ULONG WriteLength,
    UCHAR *ReadBuffer,
    ULONG ReadLength
    );

#endif
