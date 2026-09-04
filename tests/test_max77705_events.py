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
typedef uint64_t ULONGLONG;
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
static void B2lMaxDelayMicroseconds(ULONG us) { (void)us; ++waits; }
static NTSTATUS B2lMaxReadByte(PB2L_MAX_CONTEXT c, UCHAR a, UCHAR r, UCHAR *v) {
    (void)c; assert(a == 0x25 && r == 0x02);
    if (!NT_SUCCESS(uic_result)) return uic_result;
    assert(sample_index < sample_count); *v = samples[sample_index++]; return 0;
}
static NTSTATUS B2lMaxReadBlock(PB2L_MAX_CONTEXT c, UCHAR a, UCHAR r, UCHAR *v, ULONG n) {
    (void)c; assert(a == 0x25);
    if (r == 0x06) {
        assert(n == 8); ++reads; memcpy(v, status_bytes, 8); return snapshot_result;
    }
    assert(r == 0x51 && n >= 1 && n <= 2);
    v[0] = response_opcode; if (n == 2) v[1] = 0x64; return 0;
}
static NTSTATUS B2lMaxWriteBlock(PB2L_MAX_CONTEXT c, UCHAR a, UCHAR r, const UCHAR *v, ULONG n) {
    (void)c; (void)v; assert(a == 0x25 && r == 0x21 && n >= 1 && n <= 2); ++writes; return 0;
}
static NTSTATUS B2lMaxWriteByte(PB2L_MAX_CONTEXT c, UCHAR a, UCHAR r, UCHAR v) {
    (void)c; assert(a == 0x25 && r == 0x41 && v == 0); ++writes; return 0;
}
'''
harness += defines + "\n"
harness += "\n".join(function(n) for n in (
    "B2lMaxCaptureEvent", "B2lMaxRecordOpcodeUic", "B2lMaxExchangeOpcode"))
harness += r'''
#include <stdarg.h>
#include <wchar.h>
#include <stdio.h>
typedef uint32_t DWORD;
typedef int32_t LSTATUS;
#define HKEY_LOCAL_MACHINE 0
#define RRF_RT_REG_BINARY 8
#define REG_BINARY 3
#define ERROR_SUCCESS 0
#define ZeroMemory(p,n) memset(p,0,n)
static B2L_MAX_EVENT_LOG fixture;
static DWORD fixture_size, fixture_type;
static LSTATUS registry_error;
static ULONGLONG printed[64];
static ULONG printed_count;
static LSTATUS RegGetValueW(int key, const wchar_t *path, const wchar_t *name,
        DWORD flags, DWORD *type, void *buffer, DWORD *size) {
    (void)key; (void)path; (void)name; assert(flags == RRF_RT_REG_BINARY);
    assert(*size == sizeof(fixture)); memcpy(buffer, &fixture, sizeof(fixture));
    *type = fixture_type; *size = fixture_size; return registry_error;
}
static int mock_wprintf(const wchar_t *format, ...) {
    va_list args; va_start(args, format);
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
harness += function("B2lShowMaxEvents", (SRC / "b2lusbctl.c").read_text())
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
    UCHAR result = 0;
    ULONG i, index;
    reset(&c);
    // Stale SYSMSG + empty wait + combined SYSMSG/APCMDRES must all survive.
    samples[0]=0x40; samples[1]=0; samples[2]=0xc0; sample_count=3;
    assert(B2lMaxExchangeOpcode(&c, 0x1c, NULL, 0, 1, &result) == 0);
    assert(result == 0x64 && writes == 2 && waits == 1 && reads == 2);
    assert(c.UicInterruptSeen == 0xc0 && c.EventLog.OpcodeUicSeen == 0xc0);
    assert(c.EventLog.SysmsgEvents == 2 && c.EventLog.Count == 2);
    assert(c.EventLog.Events[0].Origin == B2L_MAX_EVENT_DRAIN);
    assert(c.EventLog.Events[1].Origin == B2L_MAX_EVENT_WAIT);
    assert(c.EventLog.Events[1].Status[1] == 3);
    assert(c.EventLog.Events[1].PolicyCc == 0xffffffffU);
    assert(c.CcStatus0 == 0x40 && c.CcStatus1 == 0); // snapshot must not enable SOURCE

    reset(&c);
    snapshot_result = -7; samples[1] = 0x80; sample_count = 2;
    assert(B2lMaxExchangeOpcode(&c, 0x1c, NULL, 0, 1, &result) == 0);
    assert(c.EventLog.SnapshotFailures == 1 && c.EventLog.Events[0].Valid == 0);
    assert(c.EventLog.Events[0].ReadStatus == (ULONG)-7);
    for (i=0; i<8; ++i) assert(c.EventLog.Events[0].Status[i] == 0);

    reset(&c);
    uic_result = -9;
    assert(B2lMaxExchangeOpcode(&c, 0x1c, NULL, 0, 1, &result) == -9);
    assert(c.EventLog.Count == 0 && reads == 0 && writes == 0);

    reset(&c);
    samples[1]=0x80; sample_count=2; response_opcode=0x05;
    assert(B2lMaxExchangeOpcode(&c, 0x1c, NULL, 0, 1, &result) == STATUS_DEVICE_PROTOCOL_ERROR);
    assert(c.EventLog.Count == 1); // response mismatch still has event evidence

    reset(&c);
    sample_count=301;
    assert(B2lMaxExchangeOpcode(&c, 0x1c, NULL, 0, 1, &result) == STATUS_IO_TIMEOUT);
    assert(reads == 0 && c.EventLog.Count == 0 && waits == 300);

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
print("MAX77705 event/opcode fault-injection checks: PASS")
