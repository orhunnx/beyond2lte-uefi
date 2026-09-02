#include <initguid.h>
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <wchar.h>

#include "b2lusb_public.h"

static HANDLE
B2lOpenController(VOID)
{
    HDEVINFO DeviceInfo;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    DWORD Required;
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
        SetupDiDestroyDeviceInfoList(DeviceInfo);
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
        HeapFree(GetProcessHeap(), 0, Detail);
        SetupDiDestroyDeviceInfoList(DeviceInfo);
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
    HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(DeviceInfo);
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
    wprintf(L"DWC3 id=%08lX gctl=%08lX gsts=%08lX dctl=%08lX dsts=%08lX\n",
        Status.GsnpsId, Status.Gctl, Status.Gsts, Status.Dctl, Status.Dsts);
    wprintf(L"xHCI usbsts=%08lX  PHY link=%08lX clkrst=%08lX utmi=%08lX\n",
        Status.XhciUsbSts, Status.PhyLinkCtrl, Status.PhyClkRst, Status.PhyUtmi);
    wprintf(L"PHY hsp=%08lX tune=%08lX  PMU[1586072C]=%08lX\n",
        Status.PhyHsp, Status.PhyHspTune, Status.PmuUsbPhy);
    return 0;
}

int
wmain(
    int ArgumentCount,
    wchar_t **Arguments
    )
{
    B2L_USB_SET_ROLE_INPUT Input;
    B2L_USB_ROLE Role;
    HANDLE Handle;
    DWORD Returned;
    int Result;

    if ((ArgumentCount != 2) ||
        ((_wcsicmp(Arguments[1], L"status") != 0) &&
         (_wcsicmp(Arguments[1], L"host") != 0) &&
         (_wcsicmp(Arguments[1], L"device") != 0) &&
         (_wcsicmp(Arguments[1], L"none") != 0))) {
        fwprintf(stderr, L"Kullanim: b2lusbctl.exe status|host|device|none\n");
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

    Role = B2lUsbRoleNone;
    if (_wcsicmp(Arguments[1], L"host") == 0) {
        Role = B2lUsbRoleHost;
    } else if (_wcsicmp(Arguments[1], L"device") == 0) {
        Role = B2lUsbRoleDevice;
    }

    ZeroMemory(&Input, sizeof(Input));
    Input.Version = B2L_USB_IOCTL_VERSION;
    Input.Size = (ULONG)sizeof(Input);
    Input.Role = Role;
    Returned = 0;
    if (!DeviceIoControl(
            Handle,
            IOCTL_B2L_USB_SET_ROLE,
            &Input,
            (DWORD)sizeof(Input),
            NULL,
            0,
            &Returned,
            NULL)) {
        fwprintf(stderr, L"SET_ROLE basarisiz: Win32=%lu\n", GetLastError());
        CloseHandle(Handle);
        return 1;
    }

    wprintf(L"URS istegi gonderildi: %ls\n", B2lRoleName(Role));
    Sleep(750);
    Result = B2lShowStatus(Handle);
    CloseHandle(Handle);
    return Result;
}
