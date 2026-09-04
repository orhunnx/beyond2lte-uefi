#!/usr/bin/env python3
"""Execute actual HSI2C ISR/DPC/transfer C with scripted peripheral interrupts.

Host tests prove state/error/lifetime behavior, not physical GSIV delivery or a
WDK build. No production completion is synthesized by the test instrumentation.
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
    pos = source.index(name + '(')
    start = source.rfind('\n\n', 0, pos) + 2
    # Some functions have preceding explanatory comments in the same block.
    brace = source.index('{', pos)
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
typedef uint8_t UCHAR, *PUCHAR;
typedef int BOOLEAN;
typedef void VOID;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef int32_t NTSTATUS;
typedef void *WDFOBJECT;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef int KEVENT;
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_PENDING ((NTSTATUS)0x103)
#define STATUS_TIMEOUT ((NTSTATUS)0x102)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)-1)
#define STATUS_NO_SUCH_DEVICE ((NTSTATUS)-2)
#define STATUS_DEVICE_PROTOCOL_ERROR ((NTSTATUS)-3)
#define STATUS_IO_TIMEOUT ((NTSTATUS)-4)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)-5)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define IO_NO_INCREMENT 0
#define Executive 0
#define KernelMode 0
struct CONTEXT;
typedef struct CONTEXT *PB2L_MAX_CONTEXT, *WDFINTERRUPT, *WDFDEVICE;
typedef struct CONTEXT {
    WDFINTERRUPT HsI2cInterrupt;
    KEVENT HsI2cCompletion;
    PUCHAR HsI2cMessageBuffer;
    ULONG HsI2cMessageLength, HsI2cMessagePosition;
    NTSTATUS HsI2cMessageStatus;
    BOOLEAN HsI2cMessageRead, HsI2cMessageActive, HsI2cIrqEnabled, PlatformPrepared;
    ULONG HsI2cInterruptCount, HsI2cCompletionCount, HsI2cLastInterruptStatus;
    ULONG HsI2cAttemptedAddress;
    ULONG HsI2cReadFailures, HsI2cLastReadFailureStatus;
    ULONG HsI2cLastReadFailureLength, HsI2cLastReadFailureTransferred;
} CONTEXT;
'''
harness += defines + '\n'
harness += r'''
static ULONG regs[64], received, written, consumed, available, supplied;
static ULONG burst, rx_limit, forced_irq, waits, sleeps, stale_wakes;
static ULONG acks, snapshots, interrupts_without_dpc;
static int irq_level, previous_level, inside_isr;
static int running, reading, lost_irq, missing_tx_done, drop_power, idle_busy;
static int stretch_sleeps, pending_dpc, late_error;
static ULONGLONG ticks;
static UCHAR tx_bytes[256];
static PB2L_MAX_CONTEXT current;
static BOOLEAN B2lMaxHsI2cEvtInterruptIsr(WDFINTERRUPT, ULONG);
static VOID B2lMaxHsI2cEvtInterruptDpc(WDFINTERRUPT, WDFOBJECT);
static NTSTATUS B2lMaxHsI2cEvtInterruptDisable(WDFINTERRUPT, WDFDEVICE);
static PB2L_MAX_CONTEXT B2lMaxGetContext(WDFDEVICE d) { return d; }
static WDFDEVICE WdfInterruptGetDevice(WDFINTERRUPT i) { return i; }
static void WdfInterruptAcquireLock(WDFINTERRUPT i) {
    assert(i == current && irq_level <= 2);
    previous_level=irq_level; irq_level=3;
}
static void WdfInterruptReleaseLock(WDFINTERRUPT i) {
    assert(i == current && irq_level == 3); irq_level=previous_level;
}
static ULONGLONG KeQueryInterruptTime(void) { return ticks; }
static void KeClearEvent(KEVENT *e) { assert(irq_level <= 2); *e=0; }
static int KeSetEvent(KEVENT *e, int boost, BOOLEAN wait) {
    assert(irq_level == 2 && boost == 0 && !wait); *e=1; return 0;
}
static BOOLEAN WdfInterruptQueueDpcForIsr(WDFINTERRUPT i) {
    assert(i == current && irq_level == 3 && inside_isr);
    pending_dpc=1; return TRUE;
}
static ULONG B2lMaxReadRegister(PB2L_MAX_CONTEXT c, ULONG offset) {
    assert(c == current);
    if (offset == HSI2C_FIFO_STATUS) {
        if (reading) return received < available ? 0 : HSI2C_RX_FIFO_EMPTY;
        return (written-consumed >= 16 ? HSI2C_TX_FIFO_FULL : 0) |
            (written==consumed ? HSI2C_TX_FIFO_EMPTY : 0);
    }
    if (offset == HSI2C_RX_DATA) {
        assert(inside_isr && irq_level == 3 && received < available);
        return (received++ + 0x40) & 0xff;
    }
    if (offset == HSI2C_TRANS_STATUS) return idle_busy ? 1 : 0;
    if (offset == HSI2C_INT_STATUS && late_error && reading && received == supplied)
        regs[offset/4] |= HSI2C_INT_NO_DEVICE;
    return regs[offset/4];
}
static void B2lMaxWriteRegister(PB2L_MAX_CONTEXT c, ULONG offset, ULONG value) {
    assert(c == current);
    if (offset == HSI2C_TX_DATA) {
        assert(inside_isr && irq_level == 3 && written-consumed < 16);
        assert(written < sizeof(tx_bytes)); tx_bytes[written++]=(UCHAR)value;
    } else if (offset == HSI2C_INT_STATUS) {
        assert((value & ~regs[offset/4]) == 0); // ack only captured W1C bits
        regs[offset/4] &= ~value; ++acks;
    } else {
        regs[offset/4]=value;
        if (offset == HSI2C_AUTO_CONF && (value & HSI2C_MASTER_RUN)) {
            assert(irq_level == 3 && c->HsI2cMessageBuffer != NULL && c->HsI2cMessageActive);
            assert(regs[HSI2C_INT_ENABLE/4] & HSI2C_INT_CHECK_STATE);
            running=1; reading=(value & HSI2C_READ_WRITE)!=0; supplied=value & 0xffff;
        }
    }
}
static void B2lMaxAndOrRegister(PB2L_MAX_CONTEXT c, ULONG r, ULONG a, ULONG o) {
    B2lMaxWriteRegister(c,r,(B2lMaxReadRegister(c,r)&a)|o);
}
static void B2lMaxCaptureControllerState(PB2L_MAX_CONTEXT c) {
    assert(c == current && irq_level == 0); ++snapshots;
}
static void B2lMaxSleepMicroseconds(ULONG us) {
    assert(irq_level == 0 && !current->HsI2cMessageActive && us == 1000);
    ticks += (ULONGLONG)us*10; ++sleeps;
    if (stretch_sleeps > 0 && --stretch_sleeps == 0) idle_busy=0;
}
static void dpc(void) {
    irq_level=2; B2lMaxHsI2cEvtInterruptDpc(current,NULL); irq_level=0;
    pending_dpc=0;
}
static void irq(void) {
    WdfInterruptAcquireLock(current); inside_isr=1;
    (void)B2lMaxHsI2cEvtInterruptIsr(current,0);
    inside_isr=0; WdfInterruptReleaseLock(current);
}
static NTSTATUS KeWaitForSingleObject(KEVENT *event, int why, int mode, BOOLEAN alertable, LARGE_INTEGER *timeout) {
    ULONGLONG limit=ticks+(ULONGLONG)(-timeout->QuadPart);
    assert(irq_level == 0 && why == 0 && mode == 0 && !alertable);
    assert(timeout->QuadPart < 0 && ++waits <= 200);
    while (ticks < limit) {
        if (*event) { *event=0; return STATUS_SUCCESS; }
        ticks += 100000; // scripted 10 ms peripheral/dispatch step
        if (ticks > limit) ticks=limit;
        if (stale_wakes) { --stale_wakes; dpc(); continue; }
        if (drop_power) {
            WdfInterruptAcquireLock(current);
            B2lMaxHsI2cEvtInterruptDisable(current,current);
            WdfInterruptReleaseLock(current); drop_power=0;
        }
        if (running && current->HsI2cIrqEnabled && current->HsI2cMessageActive) {
            if (forced_irq) regs[HSI2C_INT_STATUS/4] |= forced_irq;
            else if (reading) {
                available=received+burst;
                if (available > supplied) available=supplied;
                if (available > rx_limit) available=rx_limit;
                if (available > received) regs[HSI2C_INT_STATUS/4] |=
                    available-received >= 8 ? HSI2C_INT_RX_ALMOSTFULL : HSI2C_INT_TRAILING;
            } else {
                consumed=written;
                regs[HSI2C_INT_STATUS/4] |= consumed==supplied ?
                    (missing_tx_done ? 0 : HSI2C_INT_TRANSFER_DONE) : HSI2C_INT_TX_ALMOSTEMPTY;
            }
            if (!lost_irq) irq();
        }
        if (pending_dpc && !interrupts_without_dpc) dpc();
    }
    if (*event) { *event=0; return STATUS_SUCCESS; }
    return STATUS_TIMEOUT;
}
'''
for name in ('B2lMaxHsI2cEvtInterruptIsr','B2lMaxHsI2cEvtInterruptDpc',
             'B2lMaxHsI2cEvtInterruptEnable','B2lMaxHsI2cEvtInterruptDisable',
             'B2lMaxFinishClockStretch','B2lMaxTransferMessage'):
    # Match the prototype's linkage in this one-translation-unit harness.
    body=function(name)
    body=re.sub(r'(?m)^(BOOLEAN|VOID|NTSTATUS)\n(B2lMaxHsI2c)',r'static \1\n\2',body)
    harness += '\n'+body+'\n'
harness += r'''
static void reset(CONTEXT *c) {
    memset(c,0,sizeof(*c)); memset(regs,0,sizeof(regs)); memset(tx_bytes,0,sizeof(tx_bytes));
    current=c; c->HsI2cInterrupt=c; c->PlatformPrepared=TRUE;
    received=written=consumed=available=supplied=forced_irq=waits=sleeps=stale_wakes=0;
    acks=snapshots=interrupts_without_dpc=0; ticks=0;
    irq_level=previous_level=inside_isr=running=reading=lost_irq=missing_tx_done=0;
    drop_power=idle_busy=stretch_sleeps=pending_dpc=late_error=0; burst=16; rx_limit=256;
    WdfInterruptAcquireLock(c); assert(B2lMaxHsI2cEvtInterruptEnable(c,c)==0); WdfInterruptReleaseLock(c);
}
int main(void) {
    CONTEXT c; UCHAR data[128]; ULONG i,n;
    for (n=1;n<=64;++n) {
        reset(&c); memset(data,0xcd,sizeof(data));
        assert(B2lMaxTransferMessage(&c,0x25,data,n,TRUE,TRUE)==0);
        assert(received==n && c.HsI2cInterruptCount==(n+15)/16 && c.HsI2cCompletionCount==1);
        assert(c.HsI2cMessageBuffer==NULL && !c.HsI2cMessageActive);
        assert(sleeps==0 && c.HsI2cReadFailures==0 && data[n]==0xcd);
        for(i=0;i<n;++i) assert(data[i]==(UCHAR)(i+0x40));
        assert(c.HsI2cAttemptedAddress==(0x07000000|(0x25<<10)));
        assert(regs[HSI2C_INT_ENABLE/4]==0 && regs[HSI2C_INT_STATUS/4]==0);
        reset(&c); for(i=0;i<n;++i) data[i]=(UCHAR)i;
        assert(B2lMaxTransferMessage(&c,0x66,data,n,FALSE,FALSE)==0);
        assert(written==n && consumed==n && c.HsI2cCompletionCount==1);
        assert(memcmp(data,tx_bytes,n)==0 && !(regs[HSI2C_AUTO_CONF/4]&HSI2C_STOP_AFTER_TRANS));
    }
    reset(&c); lost_irq=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_IO_TIMEOUT);
    assert(available==8 && received==0 && c.HsI2cInterruptCount==0 && ticks==10000000);
    assert(c.HsI2cReadFailures==1 && c.HsI2cLastReadFailureTransferred==0);
    // A queued interrupt after timeout has no buffer to touch and cannot complete the old request.
    irq(); assert(c.HsI2cCompletionCount==0 && c.HsI2cMessageBuffer==NULL);
    reset(&c); burst=3; rx_limit=3;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_IO_TIMEOUT);
    assert(c.HsI2cLastReadFailureTransferred==3 && c.HsI2cLastReadFailureLength==8);
    // A later short success must retain the earlier failed read record.
    running=0; received=available=0; rx_limit=256; regs[HSI2C_INT_STATUS/4]=0;
    assert(B2lMaxTransferMessage(&c,0x25,data,1,TRUE,TRUE)==0);
    assert(c.HsI2cReadFailures==1 && c.HsI2cLastReadFailureTransferred==3);
    reset(&c); forced_irq=HSI2C_INT_NO_DEVICE|HSI2C_INT_TRANSFER_DONE;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_NO_SUCH_DEVICE);
    assert(received==0 && c.HsI2cCompletionCount==1 && snapshots==1);
    reset(&c); late_error=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_NO_SUCH_DEVICE);
    assert(received==8 && c.HsI2cLastReadFailureTransferred==8);
    reset(&c); forced_irq=HSI2C_INT_RX_ALMOSTFULL;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,FALSE,TRUE)==STATUS_IO_TIMEOUT);
    assert(c.HsI2cInterruptCount==0 && written==0); // masked status is not our interrupt
    reset(&c); forced_irq=0x100;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_DEVICE_PROTOCOL_ERROR);
    reset(&c); forced_irq=HSI2C_INT_RX_ALMOSTFULL|HSI2C_INT_FIFO_ERROR;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_DEVICE_PROTOCOL_ERROR);
    reset(&c); missing_tx_done=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,FALSE,TRUE)==STATUS_IO_TIMEOUT);
    assert(written==8 && consumed==8 && c.HsI2cCompletionCount==0);
    reset(&c); stale_wakes=3;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==0);
    assert(waits==4 && c.HsI2cInterruptCount==1 && received==8);
    reset(&c); stale_wakes=200;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_IO_TIMEOUT);
    assert(ticks==10000000 && c.HsI2cInterruptCount==0); // stale wakes never extend deadline
    reset(&c); interrupts_without_dpc=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==0);
    assert(c.HsI2cInterruptCount==1 && pending_dpc && ticks==10000000);
    // Deliver the previous DPC while a new message is active.
    running=0; received=available=0; interrupts_without_dpc=0; stale_wakes=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==0);
    assert(c.HsI2cInterruptCount==2 && c.HsI2cCompletionCount==2);
    reset(&c); drop_power=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_DEVICE_NOT_READY);
    assert(c.HsI2cInterruptCount==0 && c.HsI2cMessageBuffer==NULL);
    reset(&c); c.HsI2cIrqEnabled=FALSE;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_DEVICE_NOT_READY && waits==0);
    reset(&c); idle_busy=1; stretch_sleeps=3;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==0 && sleeps==3);
    reset(&c); idle_busy=1;
    assert(B2lMaxTransferMessage(&c,0x25,data,8,TRUE,TRUE)==STATUS_IO_TIMEOUT);
    assert(c.HsI2cCompletionCount==1 && sleeps==100 && c.HsI2cLastReadFailureTransferred==8);
    reset(&c);
    assert(B2lMaxTransferMessage(&c,0x80,data,8,TRUE,TRUE)==STATUS_INVALID_PARAMETER);
    assert(B2lMaxTransferMessage(&c,0x25,NULL,8,TRUE,TRUE)==STATUS_INVALID_PARAMETER);
    assert(B2lMaxTransferMessage(&c,0x25,data,0,TRUE,TRUE)==STATUS_INVALID_PARAMETER);
    assert(B2lMaxTransferMessage(&c,0x25,data,0x10000,TRUE,TRUE)==STATUS_INVALID_PARAMETER);
    assert(waits==0 && !running);
    puts("MAX77705 real ISR/DPC/transfer contract tests: PASS (hardware delivery unverified)");
}
'''
# Windows ULONG is 32-bit; host LP64 UL must not change expressions in this test.
harness=re.sub(r'\b(0[xX][0-9A-Fa-f]+|[0-9]+)UL\b',r'\1U',harness)
with tempfile.TemporaryDirectory(prefix='b2l-irq-test-') as temp:
    c=Path(temp)/'irq.c'; exe=Path(temp)/'irq'; c.write_text(harness)
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-O0',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
