#include <initguid.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <wchar.h>
#include <conio.h>
#include <wctype.h>

#include "b2lusb_public.h"
#include "b2lmax77705_events.h"

#define B2L_XHCI_PORTSC_CCS          (1UL << 0)
#define B2L_XHCI_PORTSC_PED          (1UL << 1)
#define B2L_XHCI_PORTSC_OCA          (1UL << 3)
#define B2L_XHCI_PORTSC_PLS_SHIFT    5U
#define B2L_XHCI_PORTSC_PLS_MASK     0xFUL
#define B2L_XHCI_PORTSC_PP           (1UL << 9)
#define B2L_XHCI_PORTSC_SPEED_SHIFT  10U
#define B2L_XHCI_PORTSC_SPEED_MASK   0xFUL
#define B2L_XHCI_PORTSC_CHANGE_MASK  0x00FE0000UL

// The reduced WinPE image has no findstr.exe. Keep literal, case-insensitive
// log filtering in this already-staged tool; no shell or driver is involved.
static BOOL
B2lLogLineMatches(const WCHAR *Line, int Count, WCHAR **Patterns)
{
    const WCHAR *Start;
    size_t Index;
    int Pattern;

    for (Pattern = 0; Pattern < Count; ++Pattern) {
        for (Start = Line; *Start != 0; ++Start) {
            for (Index = 0; Patterns[Pattern][Index] != 0; ++Index) {
                if ((Start[Index] == 0) ||
                    (towlower(Start[Index]) != towlower(Patterns[Pattern][Index]))) {
                    break;
                }
            }
            if (Patterns[Pattern][Index] == 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

static int
B2lFilterLog(const WCHAR *Path, int Count, WCHAR **Patterns)
{
    FILE *Input;
    unsigned char Bom[3] = {0, 0, 0};
    WCHAR Line[16384];
    size_t Length;
    ULONG Matches = 0;

    if (_wfopen_s(&Input, Path, L"rb") != 0) {
        wprintf(L"LOG_FILTER_OPEN_ERROR: %ls\n", Path);
        return 2;
    }
    (VOID)fread(Bom, 1, sizeof(Bom), Input);
    fclose(Input);
    if ((Bom[0] == 0xFE) && (Bom[1] == 0xFF)) {
        wprintf(L"LOG_FILTER_UNSUPPORTED_UTF16BE: %ls\n", Path);
        return 2;
    }
    // UCRT honors a UTF-8 or UTF-16LE BOM in Unicode mode. BOM-less reg.exe
    // and pnputil redirection uses the ordinary text mode/code page instead.
    if (_wfopen_s(&Input, Path,
        (((Bom[0] == 0xFF) && (Bom[1] == 0xFE)) ||
         ((Bom[0] == 0xEF) && (Bom[1] == 0xBB) && (Bom[2] == 0xBF))) ?
        L"rt, ccs=UNICODE" : L"rt") != 0) {
        wprintf(L"LOG_FILTER_OPEN_ERROR: %ls\n", Path);
        return 2;
    }
    while (fgetws(Line, (int)ARRAYSIZE(Line), Input) != NULL) {
        Length = wcslen(Line);
        if ((Length == ARRAYSIZE(Line) - 1) && (Line[Length - 1] != L'\n')) {
            wprintf(L"LOG_FILTER_LINE_TOO_LONG: %ls\n", Path);
            fclose(Input);
            return 2;
        }
        if (B2lLogLineMatches(Line, Count, Patterns)) {
            wprintf(L"%ls", Line);
            if ((Length != 0) && (Line[Length - 1] != L'\n')) {
                wprintf(L"\n");
            }
            ++Matches;
        }
    }
    if (ferror(Input)) {
        wprintf(L"LOG_FILTER_READ_ERROR: %ls\n", Path);
        fclose(Input);
        return 2;
    }
    fclose(Input);
    if (Matches == 0) {
        wprintf(L"LOG_FILTER_NO_MATCH: %ls\n", Path);
    }
    return 0;
}

static HANDLE
B2lOpenController(VOID)
{
    HDEVINFO DeviceInfo;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    DWORD Required;
    DWORD Error;
    HANDLE Handle;

    DeviceInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_B2L_USB,
        NULL,
        NULL,
        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT
        );
    if (DeviceInfo == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = (DWORD)sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(
            DeviceInfo,
            NULL,
            &GUID_DEVINTERFACE_B2L_USB,
            0,
            &InterfaceData)) {
        Error = GetLastError();
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(Error);
        return INVALID_HANDLE_VALUE;
    }

    Required = 0;
    (VOID)SetupDiGetDeviceInterfaceDetailW(
        DeviceInfo,
        &InterfaceData,
        NULL,
        0,
        &Required,
        NULL
        );
    if (Required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }

    Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        Required
        );
    if (Detail == NULL) {
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    Detail->cbSize = (DWORD)sizeof(*Detail);
    if (!SetupDiGetDeviceInterfaceDetailW(
            DeviceInfo,
            &InterfaceData,
            Detail,
            Required,
            NULL,
            NULL)) {
        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, Detail);
        SetupDiDestroyDeviceInfoList(DeviceInfo);
        SetLastError(Error);
        return INVALID_HANDLE_VALUE;
    }

    Handle = CreateFileW(
        Detail->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    Error = GetLastError();
    HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(DeviceInfo);
    SetLastError(Error);
    return Handle;
}

static const wchar_t *
B2lRoleName(
    B2L_USB_ROLE Role
    )
{
    if (Role == B2lUsbRoleHost) {
        return L"HOST";
    }
    if (Role == B2lUsbRoleDevice) {
        return L"DEVICE";
    }
    return L"NONE";
}

static const wchar_t *
B2lPortLinkStateName(
    ULONG PortLinkState
    )
{
    static const wchar_t *Names[16] = {
        L"U0", L"U1", L"U2", L"U3",
        L"Disabled", L"RxDetect", L"Inactive", L"Polling",
        L"Recovery", L"HotReset", L"Compliance", L"Test",
        L"Reserved12", L"Reserved13", L"Reserved14", L"Resume"
    };

    return Names[PortLinkState & B2L_XHCI_PORTSC_PLS_MASK];
}

static VOID
B2lShowPortStatus(
    ULONG PortNumber,
    ULONG PortSc
    )
{
    ULONG PortLinkState;
    ULONG Speed;

    PortLinkState = (PortSc >> B2L_XHCI_PORTSC_PLS_SHIFT) &
        B2L_XHCI_PORTSC_PLS_MASK;
    Speed = (PortSc >> B2L_XHCI_PORTSC_SPEED_SHIFT) &
        B2L_XHCI_PORTSC_SPEED_MASK;
    wprintf(
        L"xHCI port%lu raw=%08lX ccs=%lu ped=%lu pp=%lu oca=%lu pls=%lu(%ls) speed=%lu change=%08lX\n",
        PortNumber,
        PortSc,
        (ULONG)((PortSc & B2L_XHCI_PORTSC_CCS) != 0),
        (ULONG)((PortSc & B2L_XHCI_PORTSC_PED) != 0),
        (ULONG)((PortSc & B2L_XHCI_PORTSC_PP) != 0),
        (ULONG)((PortSc & B2L_XHCI_PORTSC_OCA) != 0),
        PortLinkState,
        B2lPortLinkStateName(PortLinkState),
        Speed,
        PortSc & B2L_XHCI_PORTSC_CHANGE_MASK
        );
}

static int
B2lShowStatus(
    HANDLE Handle
    )
{
    B2L_USB_STATUS Status;
    DWORD Returned;

    ZeroMemory(&Status, sizeof(Status));
    Returned = 0;
    if (!DeviceIoControl(
            Handle,
            IOCTL_B2L_USB_GET_STATUS,
            NULL,
            0,
            &Status,
            (DWORD)sizeof(Status),
            &Returned,
            NULL)) {
        fwprintf(stderr, L"GET_STATUS basarisiz: Win32=%lu\n", GetLastError());
        return 1;
    }
    if ((Returned != (DWORD)sizeof(Status)) ||
        (Status.Version != B2L_USB_IOCTL_VERSION) ||
        (Status.Size != (ULONG)sizeof(Status))) {
        fwprintf(stderr, L"GET_STATUS surum/boyut uyusmazligi\n");
        return 1;
    }

    wprintf(L"requested=%ls current=%ls last_ntstatus=0x%08lX flags=0x%08lX\n",
        B2lRoleName(Status.RequestedRole),
        B2lRoleName(Status.CurrentRole),
        (ULONG)Status.LastRoleStatus,
        Status.Flags);
    if ((Status.Flags & B2L_USB_STATUS_REGISTERS_VALID) == 0UL) {
        wprintf(L"Denetleyici etkin degil; register okunmadi.\n");
        return 0;
    }
    wprintf(L"DWC3 id=%08lX ver=%08lX resets=%lu\n",
        Status.GsnpsId, Status.Dwc3VerNumber, Status.CoreResetCount);
    wprintf(L"DWC3 gctl=%08lX gsts=%08lX dctl=%08lX dsts=%08lX\n",
        Status.Gctl, Status.Gsts, Status.Dctl, Status.Dsts);
    wprintf(L"DWC3 gusb2=%08lX gusb3=%08lX\n",
        Status.Dwc3Gusb2PhyCfg, Status.Dwc3Gusb3PipeCtl);
    if (Status.CurrentRole != B2lUsbRoleHost) {
        wprintf(L"Function rolu; xHCI register okunmadi.\n");
        return 0;
    }
    wprintf(L"xHCI cap=%08lX hcs1=%08lX hcc1=%08lX\n",
        Status.XhciCapability, Status.XhciHcsParams1, Status.XhciHccParams1);
    wprintf(L"xHCI hcs2=%08lX hcs3=%08lX hcc2=%08lX\n",
        Status.XhciHcsParams2, Status.XhciHcsParams3, Status.XhciHccParams2);
    wprintf(L"xHCI db=%08lX rts=%08lX xecp=%08lX\n",
        Status.XhciDbOff, Status.XhciRtsOff, Status.XhciExtCapOffset);
    wprintf(L"xHCI cmd=%08lX sts=%08lX page=%08lX\n",
        Status.XhciUsbCmd, Status.XhciUsbSts, Status.XhciPageSize);
    wprintf(L"xHCI dn=%08lX cfg=%08lX crcr=%08lX%08lX\n",
        Status.XhciDnCtrl, Status.XhciConfig,
        Status.XhciCrcrHigh, Status.XhciCrcrLow);
    wprintf(L"xHCI dcbaap=%08lX%08lX portsc=%08lX/%08lX\n",
        Status.XhciDcbaapHigh, Status.XhciDcbaapLow,
        Status.XhciPortSc1, Status.XhciPortSc2);
    B2lShowPortStatus(1, Status.XhciPortSc1);
    B2lShowPortStatus(2, Status.XhciPortSc2);
    wprintf(L"xECP +00=%08lX +04=%08lX +08=%08lX +0C=%08lX\n",
        Status.XhciExtCap[0], Status.XhciExtCap[1],
        Status.XhciExtCap[2], Status.XhciExtCap[3]);
    wprintf(L"xECP +10=%08lX +14=%08lX +18=%08lX +1C=%08lX\n",
        Status.XhciExtCap[4], Status.XhciExtCap[5],
        Status.XhciExtCap[6], Status.XhciExtCap[7]);
    wprintf(L"xECP +20=%08lX +24=%08lX +28=%08lX +2C=%08lX\n",
        Status.XhciExtCap[8], Status.XhciExtCap[9],
        Status.XhciExtCap[10], Status.XhciExtCap[11]);
    wprintf(L"xHCI obs flags=%08lX samples=%lu transitions=%lu\n",
        Status.XhciObserveFlags, Status.XhciObserveSamples,
        Status.XhciObserveTransitions);
    wprintf(L"xHCI obs first=%08lX/%08lX last=%08lX/%08lX or=%08lX/%08lX\n",
        Status.XhciObserveFirstUsbCmd, Status.XhciObserveFirstUsbSts,
        Status.XhciObserveLastUsbCmd, Status.XhciObserveLastUsbSts,
        Status.XhciObserveUsbCmdOr, Status.XhciObserveUsbStsOr);
    wprintf(L"xHCI obs seen reset=%lu run=%lu hch0=%lu cnr=%lu hse=%lu ring=%lu dcbaa=%lu cfg=%lu\n",
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_HCRST) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_RUN) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_HCH_CLEAR) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CNR) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_HSE) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CRCR) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_DCBAAP) != 0),
        (ULONG)((Status.XhciObserveFlags & B2L_XHCI_OBSERVE_SEEN_CONFIG) != 0));
    wprintf(L"xHCI obs ring=%08lX%08lX dcbaa=%08lX%08lX cfg=%08lX\n",
        Status.XhciObserveCrcrHigh, Status.XhciObserveCrcrLow,
        Status.XhciObserveDcbaapHigh, Status.XhciObserveDcbaapLow,
        Status.XhciObserveConfig);
    wprintf(L"PHY link=%08lX clkrst=%08lX utmi=%08lX\n",
        Status.PhyLinkCtrl, Status.PhyClkRst, Status.PhyUtmi);
    wprintf(L"PHY hsp=%08lX tune=%08lX  PMU[1586072C]=%08lX\n",
        Status.PhyHsp, Status.PhyHspTune, Status.PmuUsbPhy);
    return 0;
}

static int
B2lReadMaxEvents(B2L_MAX_EVENT_LOG *Log)
{
    DWORD Size = (DWORD)sizeof(*Log);
    DWORD Type = 0;
    LSTATUS Error;

    // One read of one binary value: no per-field process or registry loop,
    // and no dependency on BYN0002 being ready to expose its interface.
    ZeroMemory(Log, sizeof(*Log));
    Error = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705",
        B2L_MAX_EVENT_VALUE, RRF_RT_REG_BINARY, &Type, Log, &Size);
    if (Error != ERROR_SUCCESS) {
        fwprintf(stderr, L"MAX_EVENT_READ_ERROR=%ld\n", Error);
        return 1;
    }
    if ((Type != REG_BINARY) || (Size != (DWORD)sizeof(*Log)) ||
        (Log->Size != (ULONG)sizeof(*Log)) ||
        (Log->Version != B2L_MAX_EVENT_VERSION) ||
        (Log->Count > B2L_MAX_EVENT_CAPACITY) ||
        (Log->NextTail >= B2L_MAX_EVENT_TAIL)) {
        fwprintf(stderr, L"MAX_EVENT_FORMAT_ERROR\n");
        return 1;
    }
    return 0;
}

static ULONG
B2lMaxEventIndex(const B2L_MAX_EVENT_LOG *Log, ULONG Order)
{
    if ((Order >= B2L_MAX_EVENT_FIRST) &&
        (Log->Count == B2L_MAX_EVENT_CAPACITY)) {
        return B2L_MAX_EVENT_FIRST +
            ((Log->NextTail + Order - B2L_MAX_EVENT_FIRST) % B2L_MAX_EVENT_TAIL);
    }
    return Order;
}

static int
B2lShowMaxEvents(VOID)
{
    B2L_MAX_EVENT_LOG Log;
    const B2L_MAX_EVENT *Event;
    ULONG Order;

    if (B2lReadMaxEvents(&Log) != 0) {
        return 1;
    }
    wprintf(L"MAX_EVENTS v=%lu count=%lu total=%llu overwritten=%llu\n",
        Log.Version, Log.Count, Log.Total, Log.Overwritten);
    wprintf(L"MAX_EVENT_START_100NS=%llu OPCODE_UIC=%02lX\n",
        Log.StartTime, Log.OpcodeUicSeen);
    wprintf(L"MAX_SYSMSG_IRQS=%lu ATTACH_ERR_IRQS=%lu SNAP_FAIL=%lu\n",
        Log.SysmsgEvents, Log.AttachErrorEvents, Log.SnapshotFailures);
    wprintf(L"origin: 1=baseline 2=opcode-drain 3=opcode-wait 4=poll\n");
    wprintf(L"        5=policy-CC-read-failed (policy unavailable)\n");
    wprintf(L"irq=VDM/PD/CC/UIC (only UIC for 2/3); policy=CC1/CC0\n");
    wprintf(L"raw=USBC1/SYSMSG/BC/FWMIN/CC0/CC1/PD0/PD1; v=0 invalid\n");
    for (Order = 0UL; Order < Log.Count; ++Order) {
        Event = &Log.Events[B2lMaxEventIndex(&Log, Order)];
        wprintf(L"#%llu t=%llu ms o=%lu op=%02lX irq=%08lX policy=%08lX\n",
            Event->Sequence, (Event->Time - Log.StartTime) / 10000ULL,
            Event->Origin, Event->Opcode, Event->Interrupts, Event->PolicyCc);
        wprintf(L"  v=%lu nt=%08lX raw=%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X\n",
            Event->Valid, Event->ReadStatus,
            (unsigned int)Event->Status[0], (unsigned int)Event->Status[1],
            (unsigned int)Event->Status[2], (unsigned int)Event->Status[3],
            (unsigned int)Event->Status[4], (unsigned int)Event->Status[5],
            (unsigned int)Event->Status[6], (unsigned int)Event->Status[7]);
        if (Order == Log.Count - 1UL) {
            if (Event->Valid != 0UL) {
                wprintf(L"MAX_LAST v=1 SYSMSG=%02X CC=%02X/%02X\n",
                    (unsigned int)Event->Status[1],
                    (unsigned int)Event->Status[4], (unsigned int)Event->Status[5]);
            } else {
                wprintf(L"MAX_LAST v=0 nt=%08lX raw=OKUNAMADI\n", Event->ReadStatus);
            }
        }
    }
    return 0;
}

#define B2L_MAX_PHOTO_ROWS 12UL

static VOID
B2lPrintMaxPhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxStage", L"B2LMaxStatus", L"B2LMaxPmicId",
        L"B2LMaxUsbcFwRev", L"B2LMaxUsbcFwMinor",
        L"B2LMaxCcStatus0", L"B2LMaxCcStatus1", L"B2LMaxCcState",
        L"B2LMaxCcSampleCount", L"B2LMaxCcTransitionCount",
        L"B2LMaxCcSourceEventCount", L"B2LMaxCcControlReadBack",
        L"B2LMaxCcOpenNormalized", L"B2LMaxControl3Before",
        L"B2LMaxControl3After", L"B2LMaxMuicControl1",
        L"B2LMaxOtgEnabled", L"B2LMaxOtgHardwareActive",
        L"B2LMaxHsI2cReadFailures", L"B2LMaxHsI2cLastReadFailureStatus",
        L"B2LMaxHsI2cLastReadFailureLength", L"B2LMaxHsI2cLastReadFailureTransferred",
        L"B2LMaxHsI2cRawIrq", L"B2LMaxHsI2cInterruptCount", L"B2LMaxHsI2cCompletionCount",
        L"B2LMaxMaxRawIrq", L"B2LMaxMaxInterruptCount", L"B2LMaxMaxLastInterruptStatus",
        L"B2LMaxOpcodeCompletions", L"B2LMaxMfdFault",
        L"B2LMaxPowerRole", L"B2LMaxDataRole", L"B2LMaxDataRoleStatus",
        L"B2LMaxMuicStatus", L"B2LMaxMuicOutstanding", L"B2LMaxMuicPathVerified"
    };
    static const WCHAR *Labels[] = {
        L"STAGE", L"STATUS", L"PMIC_ID", L"FW", L"FW_MINOR",
        L"CC0", L"CC1", L"CC_STATE", L"CC_SAMPLES", L"CC_TRANS",
        L"CC_SRC_EV", L"CCCTRL1", L"CCOPEN_OK", L"CTRL3_BEFORE",
        L"CTRL3_AFTER", L"MUIC_CTL1", L"OTG_SW", L"OTG_HW",
        L"RX_FAIL", L"RX_NT", L"RX_WANTED", L"RX_READ",
        L"I2C_GSIV", L"I2C_IRQ", L"I2C_DONE",
        L"MAX_GSIV", L"MAX_IRQ", L"MAX_NT", L"OP_DONE", L"MFD_FAULT",
        L"PWR_ROLE", L"DATA_ROLE", L"DATA_NT", L"MUIC_NT", L"MUIC_Q", L"MUIC_OK"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / GENEL DURUM\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MAX_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"MUIC_NT=103 bekler; 0/OK=1: analog yol dogrulandi.\n");
    wprintf(L"STAGE=70: MAX init, USB baglantisi degil. Q/Esc: cikis.\n");
}

static VOID
B2lPrintConnectorPhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxConnectorStatus", L"B2LMaxConnectorSwapStatus",
        L"B2LMaxConnectorRunning", L"B2LMaxConnectorAttached",
        L"B2LMaxConnectorQueued", L"B2LMaxConnectorAttempts",
        L"B2LMaxConnectorCount", L"B2LMaxConnectorOverflow",
        L"B2LMaxPdRid", L"B2LMaxPdRidChanges", L"B2LMaxRidUsbDetaches",
        L"B2LMaxRidUsbAttachBlocks", L"B2LMaxUsbRoleAttached"
    };
    static const WCHAR *Labels[] = {
        L"UCM_NT", L"SWAP_NT", L"UCM_RUN", L"UCM_ATTACHED",
        L"QUEUED", L"ATTEMPTS", L"PENDING", L"OVERFLOW",
        L"PD_RID", L"RID_TRANS", L"RID_DETACH", L"RID_BLOCK", L"USB_NOTIFY"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / WINDOWS ROL BILDIRIMI\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MAX_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"UCM_NT: Windows bildirim sonucu; USB testi degil.\n");
    wprintf(L"UCM_ATTACHED=1: UCM attach kabul etti.\n");
    wprintf(L"USB_NOTIFY: USB tuketicisine giden gercek rol olayi.\n");
    wprintf(L"PD_RID=0: tanimsiz, 1..7: RID, FF: okunmadi.\n");
    wprintf(L"RID_DETACH: USB bildirimi; fiziksel CC detach degil.\n");
    wprintf(L"Host/function PnP sonucu USB sayfasinda.\n");
    wprintf(L"SWAP_NT=C00000BB: OS PD rol takasi henuz eksik.\n");
    wprintf(L"Source Rp akimi bilinmiyorsa tahmin edilmez.\n");
    wprintf(L"Alanlar sirayla okunur; atomik kayit degil.\n");
    wprintf(L"Q/Esc: cikis. Sonraki sayfa: 20 sn.\n");
}

static VOID
B2lPrintPdPhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxPdMessage",
        L"B2LMaxPdMessageStatus",
        L"B2LMaxPdMessageEvents",
        L"B2LMaxPdUnhandledEvents",
        L"B2LMaxPsRdyRaw",
        L"B2LMaxPsRdyStatus",
        L"B2LMaxPsRdyEvents",
        L"B2LMaxPdSupported",
        L"B2LMaxPdCapsValid",
        L"B2LMaxPdCapsStatus",
        L"B2LMaxPdCapsHeader",
        L"B2LMaxPdPdoCount",
        L"B2LMaxPdCurrentPdo",
        L"B2LMaxPdReadsQueued",
        L"B2LMaxPdReadsPending",
        L"B2LMaxPdReadCompletions",
        L"B2LMaxPdStaleResponses",
        L"B2LMaxPdRecoveryPending",
        L"B2LMaxPdRecoveryStatus",
        L"B2LMaxPdRecoveryAttempts",
        L"B2LMaxPdPrSwap",
        L"B2LMaxPdPdo1",
        L"B2LMaxPdPdo2",
        L"B2LMaxPdPdo3",
        L"B2LMaxPdPdo4",
        L"B2LMaxPdPdo5",
        L"B2LMaxPdPdo6",
        L"B2LMaxPdPdo7"
    };
    static const WCHAR *Labels[] = {
        L"PD_MSG",
        L"PD_NT",
        L"PD_EVENTS",
        L"UNHANDLED",
        L"PSRDY_RAW",
        L"PSRDY_NT",
        L"PSRDY_EVENTS",
        L"PD_SEEN",
        L"CAPS_VALID",
        L"CAPS_NT",
        L"CAPS_HEADER",
        L"PDO_COUNT",
        L"CURRENT_PDO",
        L"READ_QUEUED",
        L"READ_PENDING",
        L"READ_DONE",
        L"STALE_DONE",
        L"REC_PENDING",
        L"REC_NT",
        L"REC_ATTEMPTS",
        L"PR_SWAP",
        L"PDO1",
        L"PDO2",
        L"PDO3",
        L"PDO4",
        L"PDO5",
        L"PDO6",
        L"PDO7"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / PD OLAYLARI VE PDO\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MAX_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"PD_SEEN: bagli portta PS_RDY goruldu; UCM PD degil.\n");
    wprintf(L"CAPS_VALID: 0x30 yaniti gecerli; yeni voltaj istenmedi.\n");
    wprintf(L"DONE/STALE_DONE hata veya iptal de icerebilir.\n");
    wprintf(L"PD_NT=C00000BB: bu PDMSG'nin isleyicisi eksik.\n");
    wprintf(L"Sonraki sayfa: 20 sn. Q/Esc: cikis.\n");
}

static VOID
B2lPrintSysmsgPhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxSysmsgValue",
        L"B2LMaxSysmsgStatus",
        L"B2LMaxSysmsgValid",
        L"B2LMaxSysmsgSample0",
        L"B2LMaxSysmsgSample1",
        L"B2LMaxSysmsgSample2",
        L"B2LMaxSysmsgSampleCount",
        L"B2LMaxSysmsgEvents",
        L"B2LMaxSysmsgMismatches",
        L"B2LMaxSysmsgUnhandled",
        L"B2LMaxSysmsgRetryOpcode",
        L"B2LMaxSysmsgRetryStatus",
        L"B2LMaxSysmsgRetries",
        L"B2LMaxAfcClockAttempts",
        L"B2LMaxAfcClockStatus",
        L"B2LMaxAfcRelockStatus"
    };
    static const WCHAR *Labels[] = {
        L"SYSMSG",
        L"SYS_NT",
        L"SYS_VALID",
        L"READ0",
        L"READ1",
        L"READ2",
        L"READ_N",
        L"SYS_EVENTS",
        L"MISMATCHES",
        L"UNHANDLED",
        L"RETRY_OP",
        L"RETRY_NT",
        L"RETRY_TRIES",
        L"AFC_TRIES",
        L"AFC_NT",
        L"RELOCK_NT"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / SYSMSG / OPCODE HATASI\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MAX_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"SYS_VALID=1: uc canli SYSMSG okumasi ayni.\n");
    wprintf(L"RETRY_NT=0: tekrar gonderildi; APCMD beklenir.\n");
    wprintf(L"RETRY/AFC alanlari son islemin sonucudur.\n");
    wprintf(L"SYS_NT=C00000BB: ilgili isleyici henuz eksik.\n");
    wprintf(L"RELOCK_NT: AFC TKEY kilidini geri kapatma sonucu.\n");
    wprintf(L"Sonraki sayfa: 20 sn. Q/Esc: cikis.\n");
}

static VOID
B2lPrintRecoveryPhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxProbeStarted",
        L"B2LMaxControlStatus",
        L"B2LMaxControlPending",
        L"B2LMaxUsbcInitialized",
        L"B2LMaxResetStage",
        L"B2LMaxResetStatus",
        L"B2LMaxResetPriorFault",
        L"B2LMaxWatchdogResets",
        L"B2LMaxPorResets",
        L"B2LMaxResetUicRaw",
        L"B2LMaxSavedUic",
        L"B2LMaxLastUicReplay",
        L"B2LMaxUicReplayCount",
        L"B2LMaxQueueClears",
        L"B2LMaxDetachQueueClears",
        L"B2LMaxQueueDiscarded",
        L"B2LMaxRecoverMask",
        L"B2LMaxGrlRequests",
        L"B2LMaxGrlStatus",
        L"B2LMaxAltModeStartRequested",
        L"B2LMaxAltModeStatus"
    };
    static const WCHAR *Labels[] = {
        L"INIT_STARTED",
        L"INIT_NT",
        L"INIT_Q",
        L"INIT_DONE",
        L"RESET_PHASE",
        L"RESET_NT",
        L"PRIOR_FAULT",
        L"WDT_COUNT",
        L"POR_COUNT",
        L"RESET_UIC",
        L"UIC_PENDING",
        L"UIC_REPLAY",
        L"REPLAY_COUNT",
        L"QUEUE_CLEARS",
        L"DETACH_CLEARS",
        L"QUEUE_DROPS",
        L"RECOVER_MASK",
        L"GRL_COUNT",
        L"GRL_NT",
        L"ALT_START_REQ",
        L"ALT_NT"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / BASLANGIC / RESET / GRL\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MAX_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"INIT_NT=103: komut yanitlari bekleniyor.\n");
    wprintf(L"INIT_NT=C0000120: komut grubu iptal edildi.\n");
    wprintf(L"RESET_PHASE: 1=VBUS 2=mask 3=reset 4=IRQ maskeleri\n");
    wprintf(L"5=UIC oku 6=init komutlari 7=unmask 8=sira tamam.\n");
    wprintf(L"RESET_NT=0: reset sirasi; INIT_NT ayri sonuc.\n");
    wprintf(L"UIC_REPLAY: reset sirasinda gercek okunan bitler.\n");
    wprintf(L"GRL_NT=0: 70 yaniti ve trim yazisi tamamlandi.\n");
    wprintf(L"ALT_START_REQ: istek; hazir tuketici anlami tasimaz.\n");
    wprintf(L"Sonraki sayfa: 20 sn. Q/Esc: cikis.\n");
}

static VOID
B2lPrintResponsePhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxResponseMismatches",
        L"B2LMaxResponseUnhandled",
        L"B2LMaxResponseShort",
        L"B2LMaxResponseExpected",
        L"B2LMaxResponseOpcode",
        L"B2LMaxResponseLength",
        L"B2LMaxResponseStatus",
        L"B2LMaxResponseRequestStatus",
        L"B2LMaxResponseRaw0",
        L"B2LMaxResponseRaw1",
        L"B2LMaxControl3RawValid",
        L"B2LMaxControl3Raw",
        L"B2LMaxAltModeAcknowledged",
        L"B2LMaxPdRequestRetry",
        L"B2LMaxSbuValid",
        L"B2LMaxSbuRaw0",
        L"B2LMaxSbuRaw1",
        L"B2LMaxSelftestValid",
        L"B2LMaxSelftestCc",
        L"B2LMaxSelftestSbu0",
        L"B2LMaxSelftestSbu1"
    };
    static const WCHAR *Labels[] = {
        L"RX_MISMATCH",
        L"RX_UNHANDLED",
        L"RX_SHORT",
        L"EXPECTED",
        L"RECEIVED",
        L"RX_LEN",
        L"DISPATCH_NT",
        L"REQ_NT",
        L"RAW0",
        L"RAW1",
        L"CTRL3_VALID",
        L"CTRL3_RAW",
        L"ALT_ACK",
        L"PDO_RETRY",
        L"SBU_VALID",
        L"SBU1_RAW",
        L"SBU2_RAW",
        L"SELF_VALID",
        L"SELF_CC",
        L"SELF_SBU1",
        L"SELF_SBU2"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / GERCEK OPCODE YANITI\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MAX_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"REQ_NT: beklenen komutun sonucu.\n");
    wprintf(L"DISPATCH_NT: gercek yanitin tuketici sonucu.\n");
    wprintf(L"RAW0/RAW1: dusuk bayt once; yalniz RX_LEN bayt gecerli.\n");
    wprintf(L"ALT_ACK: firmware yaniti; READY / DP baglantisi degil.\n");
    wprintf(L"PDO_RETRY: secim degil; yeni guc istegi gondermez.\n");
    wprintf(L"Alanlar atomik degil. Sonraki sayfa: 20 sn. Q/Esc: cikis.\n");
}

static VOID
B2lPrintMuicPhotoOverview(ULONG Cycle)
{
    static const WCHAR *Names[] = {
        L"B2LMaxMuicDetectStatus", L"B2LMaxMuicType", L"B2LMaxMuicCause",
        L"B2LMaxMuicSamples", L"B2LMaxMuicSampleValid", L"B2LMaxMuicClassified",
        L"B2LMaxMuicRaw06", L"B2LMaxMuicRaw07", L"B2LMaxMuicRaw08",
        L"B2LMaxMuicRaw09", L"B2LMaxMuicRaw0A", L"B2LMaxMuicAdc",
        L"B2LMaxMuicEffectiveBc", L"B2LMaxMuicChargerType",
        L"B2LMaxRidRaw", L"B2LMaxMuicRid", L"B2LMaxRidChanges", L"B2LMaxMuicRprd",
        L"B2LMaxMuicPathRequested", L"B2LMaxMuicControl1", L"B2LMaxMuicPathVerified",
        L"B2LMaxMuicStatus", L"B2LMaxMuicOutstanding", L"B2LMaxDataRoleStatus"
    };
    static const WCHAR *Labels[] = {
        L"DETECT_NT", L"CABLE", L"CAUSE", L"SAMPLES", L"RAW_VALID", L"CLASS_VALID",
        L"RAW06_USBC1", L"RAW07_SYS", L"RAW08_BC", L"RAW09", L"RAW0A_CC0",
        L"ADC_USED", L"BC_USED", L"CHARGER_USED", L"FCT_RAW", L"RID_CACHED",
        L"RID_CHANGES", L"RPRD", L"PATH_REQ", L"PATH_READ", L"PATH_OK",
        L"PATH_NT", L"PATH_Q", L"DATA_NT"
    };
    HKEY Key;
    LSTATUS Error;
    DWORD Value;
    DWORD Size;
    DWORD Type;
    ULONG Index;

    C_ASSERT(ARRAYSIZE(Names) == ARRAYSIZE(Labels));
    wprintf(L"MAX FOTO v14 / tur %lu / MUIC / RID\n", Cycle);
    wprintf(L"Alanlar sirayla okunur. Sonraki sayfa: 20 sn.\n");
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\b2lmax77705", 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS) {
        wprintf(L"MUIC_OVERVIEW_READ_ERROR=%ld\n", Error);
        return;
    }
    // One key, no reg.exe processes. Fields are sequential reads, not one
    // atomic policy snapshot; event pages carry their own snapshot identity.
    for (Index = 0UL; Index < ARRAYSIZE(Names); ++Index) {
        Size = (DWORD)sizeof(Value);
        Type = 0;
        Value = 0;
        Error = RegQueryValueExW(Key, Names[Index], NULL, &Type,
            (LPBYTE)&Value, &Size);
        if ((Error == ERROR_SUCCESS) && (Type == REG_DWORD) &&
            (Size == (DWORD)sizeof(Value))) {
            wprintf(L"%-14ls = %08lX\n", Labels[Index], Value);
        } else {
            wprintf(L"%-14ls = YOK / hata=%ld tip=%lu boyut=%lu\n",
                Labels[Index], Error, Type, Size);
        }
    }
    RegCloseKey(Key);
    wprintf(L"CABLE: 0 none, 1 USB, 2 CDP, 3 OTG, 4 TA\n");
    wprintf(L"5 DCD timeout, 6 unofficial TA, 7/8 USB jig\n");
    wprintf(L"9/A UART jig: UART tuketicisi henuz eksik.\n");
    wprintf(L"CAUSE: 1..20 UIC bit, 100 init, 200 CC, 400 RID\n");
    wprintf(L"RAW_VALID=0: ham baytlar onceki okumaya ait.\n");
    wprintf(L"CLASS_VALID=0: bu olay yeni sinif karari vermedi.\n");
    wprintf(L"DETECT_NT: okuma/sinif. PATH_NT: gercek opcode.\n");
    wprintf(L"PATH_OK=1 USB aktarimini kanitlamaz. Q/Esc: cikis.\n");
}

static VOID
B2lPrintMaxEventPage(const B2L_MAX_EVENT_LOG *Log, ULONG Page, ULONG Cycle)
{
    ULONG Pages = (Log->Count + B2L_MAX_PHOTO_ROWS - 1UL) / B2L_MAX_PHOTO_ROWS;
    ULONG Order;
    const B2L_MAX_EVENT *Event;

    if (Pages == 0UL) {
        Pages = 1UL;
    }
    // At most 33 lines on the WinPE 64x41 console. One immutable registry
    // snapshot for the whole cycle: new events cannot reorder later pages.
    wprintf(L"MAX FOTO v14 / tur %lu / sayfa %lu-%lu\n", Cycle, Page + 1UL, Pages);
    wprintf(L"Bu ekrani fotograflayin. Sonraki sayfa: 20 sn\n");
    wprintf(L"D0=%llu count=%lu total=%llu\n", Log->StartTime, Log->Count, Log->Total);
    wprintf(L"kayip=%llu UIC=%02lX SYS=%lu ERR=%lu FAIL=%lu\n",
        Log->Overwritten, Log->OpcodeUicSeen, Log->SysmsgEvents,
        Log->AttachErrorEvents, Log->SnapshotFailures);
    wprintf(L"o:1=ilk 4=CC 5=CC-hata 6=gercek-IRQ (2/3=eski)\n");
    wprintf(L"irq=VDM/PD/CC/UIC; p=CC1CC0; r=06..0D ham baytlar\n");
    wprintf(L"r: USBC1 SYS BC FW CC0 CC1 PD0 PD1; v=0 gecersiz\n");
    for (Order = Page * B2L_MAX_PHOTO_ROWS;
         (Order < Log->Count) && (Order < (Page + 1UL) * B2L_MAX_PHOTO_ROWS);
         ++Order) {
        Event = &Log->Events[B2lMaxEventIndex(Log, Order)];
        wprintf(L"#%llu t=%llu ms o=%lu op=%02lX irq=%08lX\n",
            Event->Sequence, (Event->Time - Log->StartTime) / 10000ULL,
            Event->Origin, Event->Opcode, Event->Interrupts);
        if (Event->Valid != 0UL) {
            wprintf(L"v=1 nt=%08lX p=%08lX r=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                Event->ReadStatus, Event->PolicyCc,
                (unsigned int)Event->Status[0], (unsigned int)Event->Status[1],
                (unsigned int)Event->Status[2], (unsigned int)Event->Status[3],
                (unsigned int)Event->Status[4], (unsigned int)Event->Status[5],
                (unsigned int)Event->Status[6], (unsigned int)Event->Status[7]);
        } else {
            wprintf(L"v=0 nt=%08lX p=%08lX r=OKUNAMADI\n",
                Event->ReadStatus, Event->PolicyCc);
        }
    }
    if (Log->Count == 0UL) {
        wprintf(L"Henuz olay yok; yeni turda yeniden okunacak.\n");
    }
    wprintf(L"Yeni turda kayit yenilenir. Klavye gerekmez.\n");
    wprintf(L"Varsa Q/Esc: cikis. Diger tus: sonraki sayfa.\n");
}

static BOOL
B2lClearPhotoConsole(HANDLE Output)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    COORD Origin = {0, 0};
    DWORD Written;
    DWORD Cells;

    fflush(stdout);
    if (!GetConsoleScreenBufferInfo(Output, &Info)) {
        return FALSE;
    }
    Cells = (DWORD)Info.dwSize.X * (DWORD)Info.dwSize.Y;
    if (!FillConsoleOutputCharacterW(Output, L' ', Cells, Origin, &Written) ||
        !FillConsoleOutputAttribute(Output, Info.wAttributes, Cells, Origin, &Written)) {
        return FALSE;
    }
    return SetConsoleCursorPosition(Output, Origin);
}

static VOID
B2lPrintUsbPhotoStatus(ULONG Cycle)
{
    B2L_USB_STATUS Status;
    HANDLE Controller;
    DWORD Returned = 0;
    ULONG Ports[2];
    ULONG Index;
    HDEVINFO Devices;
    SP_DEVINFO_DATA Device;
    WCHAR Id[256];
    DWORD Count;
    DWORD Error;

    wprintf(L"USB FOTO v3 / tur %lu / PORT VE AYGITLAR\n", Cycle);
    Controller = B2lOpenController();
    if (Controller == INVALID_HANDLE_VALUE) {
        wprintf(L"USB_ARAYUZ_HATASI=%lu\n", GetLastError());
    } else {
        ZeroMemory(&Status, sizeof(Status));
        if (!DeviceIoControl(Controller, IOCTL_B2L_USB_GET_STATUS,
                NULL, 0, &Status, (DWORD)sizeof(Status), &Returned, NULL)) {
            wprintf(L"USB_DURUM_HATASI=%lu\n", GetLastError());
        } else if ((Returned != sizeof(Status)) ||
                   (Status.Version != B2L_USB_IOCTL_VERSION) ||
                   (Status.Size != sizeof(Status))) {
            wprintf(L"USB_DURUM_SURUM_BOYUT_HATASI\n");
        } else {
            wprintf(L"Istenen=%ls simdiki=%ls NT=%08lX\n",
                B2lRoleName(Status.RequestedRole), B2lRoleName(Status.CurrentRole),
                (ULONG)Status.LastRoleStatus);
            if ((Status.Flags & B2L_USB_STATUS_REGISTERS_VALID) &&
                Status.CurrentRole == B2lUsbRoleHost) {
                wprintf(L"xHCI CMD=%08lX STS=%08lX CFG=%08lX\n",
                    Status.XhciUsbCmd, Status.XhciUsbSts, Status.XhciConfig);
                Ports[0] = Status.XhciPortSc1;
                Ports[1] = Status.XhciPortSc2;
                for (Index = 0; Index < ARRAYSIZE(Ports); ++Index) {
                    wprintf(L"Port%lu raw=%08lX CCS=%lu PED=%lu PP=%lu\n", Index + 1,
                        Ports[Index], (ULONG)((Ports[Index] & B2L_XHCI_PORTSC_CCS) != 0),
                        (ULONG)((Ports[Index] & B2L_XHCI_PORTSC_PED) != 0),
                        (ULONG)((Ports[Index] & B2L_XHCI_PORTSC_PP) != 0));
                }
            } else {
                wprintf(L"Etkin HOST yok; xHCI/PORTSC okunmadi.\n");
            }
        }
        CloseHandle(Controller);
    }
    wprintf(L"PP=1 konektorde VBUS olctugumuz anlamina gelmez.\n");
    // Read the current PnP inventory, without starting a bus rescan/restart.
    // URS lists the role child; USB the root hub/receiver; HID the interfaces.
    for (Index = 0; Index < 3; ++Index) {
        const WCHAR *Enumerator = Index == 0 ? L"URS" : Index == 1 ? L"USB" : L"HID";
        Devices = SetupDiGetClassDevsW(NULL, Enumerator, NULL,
            DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (Devices == INVALID_HANDLE_VALUE) {
            wprintf(L"%ls_PNP_HATASI=%lu\n", Enumerator, GetLastError());
            continue;
        }
        Count = 0;
        ZeroMemory(&Device, sizeof(Device));
        Device.cbSize = (DWORD)sizeof(Device);
        while (SetupDiEnumDeviceInfo(Devices, Count, &Device)) {
            if (Count < 6) {
                if (SetupDiGetDeviceInstanceIdW(Devices, &Device, Id,
                        (DWORD)ARRAYSIZE(Id), NULL)) {
                    wprintf(L"%ls: %.48ls\n", Enumerator, Id);
                } else {
                    wprintf(L"%ls_KIMLIK_HATASI=%lu\n", Enumerator, GetLastError());
                }
            }
            ++Count;
        }
        Error = GetLastError();
        if (Error != ERROR_NO_MORE_ITEMS) {
            wprintf(L"%ls_LISTE_HATASI=%lu\n", Enumerator, Error);
        }
        wprintf(L"%ls_PRESENT=%lu (ilk 6 kimlik, en fazla 48 karakter)\n", Enumerator, Count);
        SetupDiDestroyDeviceInfoList(Devices);
    }
    wprintf(L"PRESENT: PnP kaydi; calisan fare kaniti degildir.\n");
    wprintf(L"Tak/cikar sonrasi yeni turu cekin. Sonraki sayfa: 20 sn\n");
}

static int
B2lShowMaxPhotoScreen(VOID)
{
    B2L_MAX_EVENT_LOG Log;
    HANDLE Output = GetStdHandle(STD_OUTPUT_HANDLE);
    ULONG Cycle = 0UL;
    ULONG Page;
    ULONG Pages;
    ULONG Tick;
    int Key;
    int Result;

    for (;;) {
        if (!B2lClearPhotoConsole(Output)) {
            fwprintf(stderr, L"MAX_FOTO_CONSOLE_ERROR=%lu\n", GetLastError());
            return 1;
        }
        ++Cycle;
        Result = B2lReadMaxEvents(&Log);
        Pages = (Result == 0) ?
            (Log.Count + B2L_MAX_PHOTO_ROWS - 1UL) / B2L_MAX_PHOTO_ROWS : 1UL;
        if (Pages == 0UL) {
            Pages = 1UL;
        }
        // Policy, USB/PnP, MUIC, UCM, PD, SYSMSG, recovery, response, then events.
        // Repeat indefinitely so photos never require a keyboard.
        for (Page = 0UL; Page <= Pages + 7UL; ++Page) {
            if ((Page != 0UL) && !B2lClearPhotoConsole(Output)) {
                return 1;
            }
            if (Page == 0UL) {
                B2lPrintMaxPhotoOverview(Cycle);
            } else if (Page == 1UL) {
                B2lPrintUsbPhotoStatus(Cycle);
            } else if (Page == 2UL) {
                B2lPrintMuicPhotoOverview(Cycle);
            } else if (Page == 3UL) {
                B2lPrintConnectorPhotoOverview(Cycle);
            } else if (Page == 4UL) {
                B2lPrintPdPhotoOverview(Cycle);
            } else if (Page == 5UL) {
                B2lPrintSysmsgPhotoOverview(Cycle);
            } else if (Page == 6UL) {
                B2lPrintRecoveryPhotoOverview(Cycle);
            } else if (Page == 7UL) {
                B2lPrintResponsePhotoOverview(Cycle);
            } else if (Result == 0) {
                B2lPrintMaxEventPage(&Log, Page - 8UL, Cycle);
            } else {
                // Read errors were initially printed before the overview;
                // repeat the actual error on this page after clearing it.
                if (B2lReadMaxEvents(&Log) == 0) {
                    wprintf(L"Olay kaydi simdi hazir; sonraki turda gosterilecek.\n");
                } else {
                    wprintf(L"MAX FOTO: yukaridaki hatayi fotograflayin.\n");
                }
                wprintf(L"20 sn sonra yeniden denenecek. Q/Esc: cikis.\n");
            }
            fflush(stdout);
            // UI-only sleep; no driver calls, I2C or repeated PnP scans.
            for (Tick = 0UL; Tick < 80UL; ++Tick) {
                if (_kbhit()) {
                    Key = _getwch();
                    if ((Key == L'q') || (Key == L'Q') || (Key == 27)) {
                        return 0;
                    }
                    if ((Key == 0) || (Key == 0xE0)) {
                        (VOID)_getwch();
                    }
                    break;
                }
                Sleep(250UL);
            }
        }
    }
}

int
wmain(
    int ArgumentCount,
    wchar_t **Arguments
    )
{
    HANDLE Handle;
    DWORD Returned;
    int Result;

    if ((ArgumentCount >= 4) && (_wcsicmp(Arguments[1], L"filter") == 0)) {
        return B2lFilterLog(Arguments[2], ArgumentCount - 3, &Arguments[3]);
    }
    if ((ArgumentCount == 2) && (_wcsicmp(Arguments[1], L"maxevents") == 0)) {
        return B2lShowMaxEvents();
    }
    if ((ArgumentCount == 2) && (_wcsicmp(Arguments[1], L"maxscreen") == 0)) {
        return B2lShowMaxPhotoScreen();
    }
    if ((ArgumentCount != 2) ||
        ((_wcsicmp(Arguments[1], L"status") != 0) &&
         (_wcsicmp(Arguments[1], L"observe") != 0))) {
        fwprintf(stderr, L"Kullanim: b2lusbctl.exe status|observe|maxevents|maxscreen\n");
        fwprintf(stderr, L"          b2lusbctl.exe filter dosya kelime [kelime ...]\n");
        return 2;
    }

    Handle = B2lOpenController();
    if (Handle == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"BYN0002 arayuzu acilamadi: Win32=%lu\n", GetLastError());
        return 1;
    }

    if (_wcsicmp(Arguments[1], L"status") == 0) {
        Result = B2lShowStatus(Handle);
        CloseHandle(Handle);
        return Result;
    }

    if (_wcsicmp(Arguments[1], L"observe") == 0) {
        Returned = 0;
        if (!DeviceIoControl(
                Handle,
                IOCTL_B2L_USB_ARM_XHCI_OBSERVER,
                NULL,
                0,
                NULL,
                0,
                &Returned,
                NULL)) {
            fwprintf(stderr, L"XHCI_OBSERVER basarisiz: Win32=%lu\n",
                GetLastError());
            CloseHandle(Handle);
            return 1;
        }
        wprintf(L"xHCI startup observer 5 saniye icin kuruldu.\n");
        CloseHandle(Handle);
        return 0;
    }

    CloseHandle(Handle);
    return 2;
}
