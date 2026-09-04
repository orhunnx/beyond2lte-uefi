#!/usr/bin/env python3
"""Execute the real HSI2C transfer loop with scripted FIFO/error/timer hardware.

Host-only regression tests; no WDK build, physical MMIO or USB access.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'windows/b2lusb/b2lmax77705_hw.c').read_text()

def function(name):
    start = source.rfind('\nstatic ', 0, source.index(name + '(')) + 1
    brace = source.index('{', source.index(name + '(', start))
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

defines = '\n'.join(re.findall(r'^#define (?:HSI2C_|B2L_HSI2C_).*', source, re.M))
harness = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;
typedef uint8_t UCHAR;
typedef int BOOLEAN;
typedef void VOID;
typedef uint64_t ULONGLONG;
typedef int32_t NTSTATUS;
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)-1)
#define STATUS_NO_SUCH_DEVICE ((NTSTATUS)-2)
#define STATUS_DEVICE_PROTOCOL_ERROR ((NTSTATUS)-3)
#define STATUS_IO_TIMEOUT ((NTSTATUS)-4)
typedef struct {
    ULONG HsI2cAttemptedAddress;
    ULONG HsI2cReadFailures, HsI2cLastReadFailureStatus;
    ULONG HsI2cLastReadFailureLength, HsI2cLastReadFailureTransferred;
} CONTEXT, *PB2L_MAX_CONTEXT;
'''
harness += defines + '\n'
harness += r'''
static ULONG registers[64], sleeps, received, written, consumed, available;
static ULONG burst, first_tick, supplied, rx_limit, snapshots;
static ULONG injected_interrupt, injected_state;
static int error_after_rx;
static int running, reading, stuck_empty, stuck_full, done_enabled;
static ULONGLONG ticks;
static UCHAR tx_bytes[128];
static ULONGLONG KeQueryInterruptTime(void) { return ticks; }
static ULONG B2lMaxReadRegister(PB2L_MAX_CONTEXT c, ULONG offset) {
    (void)c;
    if (offset == HSI2C_FIFO_STATUS) {
        if (!running) return HSI2C_RX_FIFO_EMPTY | HSI2C_TX_FIFO_EMPTY;
        if (reading) return (received < available) ? 0 : HSI2C_RX_FIFO_EMPTY;
        return stuck_full ? 16 :
            (written > consumed ? written - consumed : HSI2C_TX_FIFO_EMPTY);
    }
    if (offset == HSI2C_RX_DATA) {
        assert(running && reading && received < available);
        return (received++ + 0x40) & 0xff;
    }
    if (offset == HSI2C_INT_STATUS && running) {
        if (error_after_rx && received == supplied) return HSI2C_INT_NO_DEVICE;
        if (sleeps && injected_interrupt) return injected_interrupt;
        if (done_enabled && !reading && consumed == supplied)
            return HSI2C_INT_TRANSFER_DONE;
    }
    if (offset == HSI2C_TRANS_STATUS && sleeps && injected_state) return injected_state;
    return registers[offset / 4];
}
static void B2lMaxWriteRegister(PB2L_MAX_CONTEXT c, ULONG offset, ULONG value) {
    (void)c;
    if (offset == HSI2C_TX_DATA) {
        assert(written < sizeof(tx_bytes));
        assert(written - consumed < 16);
        tx_bytes[written++] = (UCHAR)value;
    } else if (offset == HSI2C_INT_STATUS) {
        registers[offset / 4] &= ~value;
    } else {
        registers[offset / 4] = value;
        if (offset == HSI2C_AUTO_CONF && (value & HSI2C_MASTER_RUN)) {
            running = 1; reading = (value & HSI2C_READ_WRITE) != 0;
            supplied = value & 0xffff;
        }
    }
}
static void B2lMaxAndOrRegister(PB2L_MAX_CONTEXT c, ULONG r, ULONG and, ULONG or) {
    B2lMaxWriteRegister(c, r, (B2lMaxReadRegister(c, r) & and) | or);
}
static void B2lMaxCaptureControllerState(PB2L_MAX_CONTEXT c) { (void)c; ++snapshots; }
static void B2lMaxSleepMicroseconds(ULONG us) {
    assert(us == B2L_HSI2C_POLL_DELAY_US);
    // A coarse/late scheduling model, not a claim about the phone's clock.
    ticks += 156250; ++sleeps;
    if (running && reading && !stuck_empty && sleeps >= first_tick) {
        available += burst;
        if (available > supplied) available = supplied;
        if (available > rx_limit) available = rx_limit;
    } else if (running && !reading && !stuck_full) consumed = written;
}
'''
harness += '\n'.join(function(n) for n in (
    'B2lMaxPollingDeadline', 'B2lMaxCheckTransferError', 'B2lMaxTransferMessage'))
harness += r'''
static void reset(void) {
    memset(registers, 0, sizeof(registers));
    memset(tx_bytes, 0, sizeof(tx_bytes));
    sleeps=received=written=consumed=available=supplied=snapshots=0;
    ticks=0; running=reading=stuck_empty=stuck_full=0;
    injected_interrupt=injected_state=0;
    error_after_rx=0;
    burst=16; first_tick=1; rx_limit=128; done_enabled=1;
}
int main(void) {
    CONTEXT c={0}; UCHAR data[64]; ULONG i, n;
    for (n=1; n<=32; ++n) {
        reset(); memset(data, 0xcd, sizeof(data));
        assert(B2lMaxTransferMessage(&c, 0x25, data, n, TRUE, TRUE) == 0);
        assert(received == n && sleeps <= 2 && snapshots == 0);
        for (i=0; i<n; ++i) assert(data[i] == (UCHAR)(0x40+i));
        assert(data[n] == 0xcd); // never read beyond the requested count
        assert(registers[HSI2C_INT_ENABLE/4] == 0);
        assert(c.HsI2cAttemptedAddress == (0x07000000 | (0x25 << 10)));
        assert((registers[HSI2C_AUTO_CONF/4] & HSI2C_STOP_AFTER_TRANS) != 0);
    }
    reset(); burst=2;
    assert(B2lMaxTransferMessage(&c, 0x25, data, 8, TRUE, TRUE) == 0);
    assert(received == 8 && sleeps == 4);
    reset(); stuck_empty=1;
    assert(B2lMaxTransferMessage(&c, 0x25, data, 8, TRUE, TRUE) == STATUS_IO_TIMEOUT);
    assert(received == 0 && sleeps == 7 && snapshots == 1);
    assert(c.HsI2cReadFailures == 1 && c.HsI2cLastReadFailureTransferred == 0);
    reset(); rx_limit=3;
    assert(B2lMaxTransferMessage(&c, 0x25, data, 8, TRUE, TRUE) == STATUS_IO_TIMEOUT);
    assert(c.HsI2cLastReadFailureLength == 8 && c.HsI2cLastReadFailureTransferred == 3);
    reset();
    assert(B2lMaxTransferMessage(&c, 0x25, data, 1, TRUE, TRUE) == 0);
    assert(c.HsI2cReadFailures == 2 && c.HsI2cLastReadFailureTransferred == 3);
    for (i=0; i<3; ++i) {
        reset(); injected_interrupt = i == 0 ? HSI2C_INT_NO_DEVICE : 0;
        injected_state = i == 1 ? HSI2C_MASTER_NO_ACK : (i == 2 ? HSI2C_MASTER_LOST : 0);
        assert(B2lMaxTransferMessage(&c, 0x25, data, 8, TRUE, TRUE) ==
            (i == 2 ? STATUS_DEVICE_PROTOCOL_ERROR : STATUS_NO_SUCH_DEVICE));
        assert(received == 0 && snapshots == 1 && registers[HSI2C_INT_ENABLE/4] == 0);
    }
    reset(); error_after_rx=1;
    assert(B2lMaxTransferMessage(&c, 0x25, data, 8, TRUE, TRUE) == STATUS_NO_SUCH_DEVICE);
    assert(received == 8 && c.HsI2cLastReadFailureTransferred == 8);
    for (n=1; n<=32; ++n) {
        reset(); for (i=0; i<n; ++i) data[i]=(UCHAR)i;
        assert(B2lMaxTransferMessage(&c, 0x25, data, n, FALSE, FALSE) == 0);
        assert(written == n && consumed == n && sleeps <= 2);
        assert(memcmp(data, tx_bytes, n) == 0);
        assert((registers[HSI2C_AUTO_CONF/4] & HSI2C_STOP_AFTER_TRANS) == 0);
    }
    reset(); done_enabled=0;
    assert(B2lMaxTransferMessage(&c, 0x25, data, 3, FALSE, TRUE) == STATUS_IO_TIMEOUT);
    reset(); stuck_full=1;
    assert(B2lMaxTransferMessage(&c, 0x25, data, 32, FALSE, TRUE) == STATUS_IO_TIMEOUT);
    assert(written == 16);
    reset();
    assert(B2lMaxTransferMessage(&c, 0x80, data, 1, TRUE, TRUE) == STATUS_INVALID_PARAMETER);
    assert(B2lMaxTransferMessage(&c, 0x25, data, 0, TRUE, TRUE) == STATUS_INVALID_PARAMETER);
    assert(B2lMaxTransferMessage(&c, 0x25, data, 0x10000, TRUE, TRUE) == STATUS_INVALID_PARAMETER);
    assert(B2lMaxTransferMessage(&c, 0x25, NULL, 1, TRUE, TRUE) == STATUS_INVALID_PARAMETER);
    puts("MAX77705 FIFO/timing/error tests: PASS");
    return 0;
}
'''
# Windows UL literals have 32 bits; preserve that ABI on LP64 hosts.
harness = re.sub(r'\b(0x[\da-fA-F]+|\d+)UL\b', r'\1U', harness)
with tempfile.TemporaryDirectory(prefix='b2l-transport-test-') as directory:
    temp = Path(directory)
    (temp/'test.c').write_text(harness)
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run(compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror',
        str(temp/'test.c'), '-o', str(temp/'test')], check=True)
    subprocess.run([str(temp/'test')], check=True)
    # Re-run exactly the previous RX loop against one delayed 8-byte burst.
    old = harness.replace('while ((Index < BufferLength) &&\n                   ((B2lMaxReadRegister',
                          'if ((Index < BufferLength) &&\n                   ((B2lMaxReadRegister', 1)
    assert old != harness
    old = old[:old.index('int main(void)')] + r"""
int main(void) {
    CONTEXT c={0}; UCHAR data[8]; NTSTATUS result;
    reset();
    result = B2lMaxTransferMessage(&c, 0x25, data, 8, TRUE, TRUE);
    return result == STATUS_IO_TIMEOUT && received == 7 ? 8 : 1;
}
"""
    (temp/'old.c').write_text(old)
    subprocess.run(compiler + ['-std=c11', str(temp/'old.c'), '-o', str(temp/'old')], check=True)
    old_result = subprocess.run([str(temp/'old')], capture_output=True)
    assert old_result.returncode == 8, 'old RX loop must time out after 7/8 bytes'
print('Previous RX loop times out at 7/8 bytes in the same test: confirmed')
