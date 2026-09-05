#!/usr/bin/env python3
"""Host-only fault-injection tests of the driver's actual event/opcode code.

No WDK, device, firmware build, MMIO or USB access. C helpers are extracted
unchanged and linked to a scripted I2C transport in a temporary directory.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "windows/b2lusb"
source = (SRC / "b2lmax77705.c").read_text()


def function(name, text=source):
    start = text.rfind("\nstatic ", 0, text.index(name + "(")) + 1
    brace = text.index("{", text.index(name + "(", start))
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


defines = "\n".join(re.findall(r"^#define MAX77705_.*(?:\\\n.*)*", source, re.M))
harness = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef uint8_t UCHAR;
typedef uint32_t ULONG;
typedef unsigned long long ULONGLONG;
typedef int32_t NTSTATUS;
typedef void VOID;
#define TRUE 1
#define C_ASSERT(x) _Static_assert(x, #x)
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)-1)
#define STATUS_DEVICE_PROTOCOL_ERROR ((NTSTATUS)-2)
#define STATUS_IO_TIMEOUT ((NTSTATUS)-3)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define B2L_MAX_USBC_ADDRESS 0x25
#include "b2lmax77705_events.h"
typedef struct {
    B2L_MAX_EVENT_LOG EventLog;
    UCHAR CcStatus0, CcStatus1, UicInterruptSeen;
    int EventLogDirty, DiagnosticDirty;
} B2L_MAX_CONTEXT, *PB2L_MAX_CONTEXT;
static UCHAR samples[400], status_bytes[8], response_opcode;
static ULONG sample_count, sample_index, reads, writes, waits;
static NTSTATUS snapshot_result, uic_result;
static ULONGLONG clock_ticks;
static ULONGLONG KeQueryInterruptTime(void) { return ++clock_ticks; }
static NTSTATUS B2lMaxReadBlock(PB2L_MAX_CONTEXT c, UCHAR a, UCHAR r, UCHAR *v, ULONG n) {
    (void)c; assert(a == 0x25);
    if (r == 0x06) {
        assert(n == 8); ++reads; memcpy(v, status_bytes, 8); return snapshot_result;
    }
    assert(r == 0x51 && n >= 1 && n <= 2);
    v[0] = response_opcode; if (n == 2) v[1] = 0x64; return 0;
}
'''
harness += defines + "\n"
harness += "\n".join(function(n) for n in (
    "B2lMaxCaptureEvent",))
harness += r'''
#include <stdarg.h>
#include <wchar.h>
#include <stdio.h>
typedef uint32_t DWORD;
typedef int32_t LSTATUS;
typedef wchar_t WCHAR;
typedef int HKEY;
typedef UCHAR *LPBYTE;
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define KEY_QUERY_VALUE 1
#define HKEY_LOCAL_MACHINE 0
#define RRF_RT_REG_BINARY 8
#define REG_BINARY 3
#define REG_DWORD 4
#define ERROR_SUCCESS 0
#define ZeroMemory(p,n) memset(p,0,n)
static B2L_MAX_EVENT_LOG fixture;
static DWORD fixture_size, fixture_type;
static LSTATUS registry_error;
static ULONGLONG printed[64];
static ULONG printed_count;
static ULONG output_lines, max_width;
static ULONG invalid_rows;
static LSTATUS RegGetValueW(int key, const wchar_t *path, const wchar_t *name,
        DWORD flags, DWORD *type, void *buffer, DWORD *size) {
    (void)key; (void)path; (void)name; assert(flags == RRF_RT_REG_BINARY);
    assert(*size == sizeof(fixture)); memcpy(buffer, &fixture, sizeof(fixture));
    *type = fixture_type; *size = fixture_size; return registry_error;
}
static LSTATUS RegOpenKeyExW(HKEY root, const WCHAR *path, DWORD options,
        DWORD access, HKEY *key) {
    (void)root; (void)path; assert(options == 0 && access == KEY_QUERY_VALUE);
    *key=1; return registry_error;
}
static LSTATUS RegQueryValueExW(HKEY key, const WCHAR *name, void *reserved,
        DWORD *type, LPBYTE value, DWORD *size) {
    DWORD sample=0x70;
    (void)name; assert(key == 1 && reserved == NULL && *size == 4);
    *type=REG_DWORD; memcpy(value, &sample, 4); return 0;
}
static LSTATUS RegCloseKey(HKEY key) { assert(key == 1); return 0; }
static int mock_wprintf(const wchar_t *format, ...) {
    wchar_t normalized[256], output[1024];
    const wchar_t *p;
    wchar_t *d = normalized;
    int conversion = 0;
    ULONG width = 0;
    va_list args, render;
    // Windows ULONG is 32 bits even though host unsigned long is 64 bits.
    // Normalize only numeric long format lengths for the host renderer.
    for (p=format; *p; ++p) {
        if (*p == L'%') conversion = 1;
        if (conversion && *p == L'l' && p[1] != L'l' &&
            (p == format || p[-1] != L'l') &&
            (p[1] == L'u' || p[1] == L'X' || p[1] == L'd')) continue;
        *d++ = *p;
        if (conversion && (*p == L'u' || *p == L'X' || *p == L'd' || *p == L's')) conversion = 0;
    }
    *d = 0;
    va_start(args, format); va_copy(render, args);
    assert(vswprintf(output, 1024, normalized, render) >= 0);
    if (wcsstr(output, L"r=OKUNAMADI") != NULL) ++invalid_rows;
    va_end(render);
    for (p=output; *p; ++p) {
        if (*p == L'\n') {
            if (width > max_width) max_width = width;
            ++output_lines; width = 0;
        } else ++width;
    }
    if (format[0] == L'#') {
        assert(printed_count < 64); printed[printed_count++] = va_arg(args, ULONGLONG);
    }
    va_end(args); return 0;
}
static int mock_fwprintf(FILE *stream, const wchar_t *format, ...) {
    (void)stream; (void)format; return 0;
}
#define wprintf mock_wprintf
#define fwprintf mock_fwprintf
'''
control = (SRC / "b2lusbctl.c").read_text()
harness += "\n".join(function(name, control) for name in (
    "B2lReadMaxEvents", "B2lMaxEventIndex", "B2lShowMaxEvents"))
harness += "\n" + re.search(r"^#define B2L_MAX_PHOTO_ROWS .*", control, re.M)[0] + "\n"
harness += function("B2lPrintMaxEventPage", control)
harness += function("B2lPrintMaxPhotoOverview", control)

harness += r"""
typedef int HANDLE;
typedef int HDEVINFO;
typedef int BOOL;
#define FALSE 0
#define INVALID_HANDLE_VALUE (-1)
#define DIGCF_ALLCLASSES 4
#define DIGCF_PRESENT 2
#define ERROR_NO_MORE_ITEMS 259
#define B2L_USB_IOCTL_VERSION 6
#define IOCTL_B2L_USB_GET_STATUS 123
#define B2L_XHCI_PORTSC_CCS 1
#define B2L_XHCI_PORTSC_PED 2
#define B2L_XHCI_PORTSC_PP 512
typedef struct { DWORD cbSize; } SP_DEVINFO_DATA;
typedef struct {
    ULONG Version, Size, RequestedRole, CurrentRole, LastRoleStatus;
    ULONG XhciUsbCmd, XhciUsbSts, XhciConfig, XhciPortSc1, XhciPortSc2;
} B2L_USB_STATUS;
static int usb_failure;
static DWORD pnp_count;
static HANDLE B2lOpenController(void) { return usb_failure == 1 ? -1 : 1; }
static const WCHAR *B2lRoleName(ULONG role) { (void)role; return L"HOST"; }
static DWORD GetLastError(void) { return ERROR_NO_MORE_ITEMS; }
static BOOL CloseHandle(HANDLE h) { assert(h == 1); return 1; }
static BOOL DeviceIoControl(HANDLE h, DWORD code, void *in, DWORD n,
        B2L_USB_STATUS *s, DWORD size, DWORD *returned, void *overlap) {
    assert(h == 1 && code == IOCTL_B2L_USB_GET_STATUS && in == NULL && n == 0);
    assert(size == sizeof(*s) && overlap == NULL);
    *returned = sizeof(*s); s->Version = 6; s->Size = sizeof(*s);
    s->XhciPortSc1 = 0x203;
    if (usb_failure == 3) s->Version=0;
    return usb_failure != 2;
}
static HDEVINFO SetupDiGetClassDevsW(void *guid, const WCHAR *name, void *window, DWORD flags) {
    assert(guid == NULL && window == NULL && flags == (DIGCF_ALLCLASSES|DIGCF_PRESENT));
    assert(wcscmp(name,L"USB")==0 || wcscmp(name,L"HID")==0);
    return usb_failure == 4 ? -1 : 1;
}
static BOOL SetupDiEnumDeviceInfo(HDEVINFO h,DWORD count,SP_DEVINFO_DATA *d) {
    assert(h == 1 && d->cbSize == sizeof(*d)); return count < pnp_count;
}
static BOOL SetupDiGetDeviceInstanceIdW(HDEVINFO h,SP_DEVINFO_DATA *d,WCHAR *id,DWORD size,void *required) {
    (void)d; assert(h == 1 && size == 256 && required == NULL);
    wmemset(id,L'X',255); id[255]=0; return usb_failure != 5;
}
static BOOL SetupDiDestroyDeviceInfoList(HDEVINFO h) { assert(h == 1); return 1; }
"""
harness += function("B2lPrintUsbPhotoStatus", control)
harness += r'''
static void reset(B2L_MAX_CONTEXT *c) {
    memset(c, 0, sizeof(*c)); memset(samples, 0, sizeof(samples));
    c->CcStatus0 = 0x40; c->CcStatus1 = 0;
    c->EventLog.Version = B2L_MAX_EVENT_VERSION;
    c->EventLog.Size = sizeof(c->EventLog);
    sample_count = sample_index = reads = writes = waits = 0;
    snapshot_result = uic_result = 0;
    memset(status_bytes, 0, sizeof(status_bytes));
    status_bytes[1] = 0x03; status_bytes[4] = 0x42; status_bytes[5] = 0x04;
    response_opcode = 0x1c;
}
int main(void) {
    B2L_MAX_CONTEXT c;
    ULONG i, index;
    reset(&c);
    for (i=0; i<113; ++i)
        B2lMaxCaptureEvent(&c, B2L_MAX_EVENT_POLL, 0xff, 0x2000, 0);
    assert(c.EventLog.Count == 64 && c.EventLog.Total == 113 && c.EventLog.Overwritten == 49);
    assert(c.EventLog.AttachErrorEvents == 113 && c.EventLog.SysmsgEvents == 0);
    for (i=0; i<16; ++i) assert(c.EventLog.Events[i].Sequence == i+1);
    for (i=0; i<48; ++i) {
        index=16+(c.EventLog.NextTail+i)%48;
        assert(c.EventLog.Events[index].Sequence == 66+i);
        assert(c.EventLog.Events[index].PolicyCc == 0x40);
    }
    assert(c.CcStatus0 == 0x40 && c.EventLogDirty && c.DiagnosticDirty);
    fixture=c.EventLog; fixture_size=sizeof(fixture); fixture_type=REG_BINARY;
    assert(B2lShowMaxEvents() == 0 && printed_count == 64);
    for (i=0; i<16; ++i) assert(printed[i] == i+1);
    for (i=16; i<64; ++i) assert(printed[i] == 50+i);
    printed_count=0;
    fixture.Count=65; assert(B2lShowMaxEvents() == 1 && printed_count == 0);
    fixture.Count=64; fixture.NextTail=48; assert(B2lShowMaxEvents() == 1);
    fixture.NextTail=0; fixture.Version=0; assert(B2lShowMaxEvents() == 1);
    fixture.Version=1; fixture_size=0; assert(B2lShowMaxEvents() == 1);
    fixture_size=sizeof(fixture); fixture_type=1; assert(B2lShowMaxEvents() == 1);
    fixture_type=REG_BINARY; registry_error=2; assert(B2lShowMaxEvents() == 1);
    registry_error=0;
    reset(&c);
    for (i=0; i<18; ++i) B2lMaxCaptureEvent(&c, B2L_MAX_EVENT_POLL, 0xff, 0, 0);
    fixture=c.EventLog;
    assert(B2lShowMaxEvents() == 0 && printed_count == 18);
    for (i=0; i<18; ++i) assert(printed[i] == i+1);
    reset(&c);
    B2lMaxCaptureEvent(&c, B2L_MAX_EVENT_CC_FAILED, 0xff, 0x2000, -9);
    assert(reads == 0 && writes == 0 && c.EventLog.Count == 1);
    assert(c.EventLog.Events[0].Interrupts == 0x2000 && c.EventLog.Events[0].Valid == 0);
    assert(c.EventLog.Events[0].ReadStatus == (ULONG)-9);
    assert(c.EventLog.Events[0].PolicyCc == 0xffffffffU);
    // Every preserved record appears exactly once across camera pages, in
    // chronological order, and fits the phone's 64-column / 41-line console.
    reset(&c);
    for (i=0; i<113; ++i) B2lMaxCaptureEvent(&c, B2L_MAX_EVENT_POLL, 0xff, 0x2000, 0);
    printed_count=0;
    for (i=0; i<6; ++i) {
        output_lines=max_width=0;
        B2lPrintMaxEventPage(&c.EventLog, i, 1);
        assert(output_lines <= 33 && max_width < 64);
    }
    assert(printed_count == 64);
    for (i=0; i<16; ++i) assert(printed[i] == i+1);
    for (i=16; i<64; ++i) assert(printed[i] == 50+i);
    reset(&c); output_lines=max_width=printed_count=0;
    B2lPrintMaxEventPage(&c.EventLog, 0, 2);
    assert(printed_count == 0 && output_lines <= 33 && max_width < 64);
    snapshot_result=STATUS_IO_TIMEOUT;
    B2lMaxCaptureEvent(&c, B2L_MAX_EVENT_POLL, 0xff, 0x2802, 0);
    invalid_rows=0; output_lines=max_width=0;
    B2lPrintMaxEventPage(&c.EventLog, 0, 2);
    assert(invalid_rows==1 && output_lines<=33 && max_width<64);
    output_lines=max_width=0;
    B2lPrintMaxPhotoOverview(2);
    assert(output_lines == 37 && max_width < 64);
    output_lines=max_width=0; registry_error=2;
    B2lPrintMaxPhotoOverview(3);
    assert(output_lines == 3 && max_width < 64);
    for (usb_failure=0; usb_failure<=5; ++usb_failure) {
        output_lines=max_width=0; pnp_count=100;
        B2lPrintUsbPhotoStatus(3);
        assert(output_lines<=33 && max_width<64);
    }
    output_lines=max_width=0; usb_failure=0; pnp_count=0;
    B2lPrintUsbPhotoStatus(4);
    assert(output_lines<=33 && max_width<64);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="b2l-event-test-") as directory:
    temp = Path(directory)
    (temp / "test.c").write_text(harness)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                              "-I", str(SRC), str(temp / "test.c"),
                              "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True)
print("MAX77705 event/photo fault-injection checks: PASS")
