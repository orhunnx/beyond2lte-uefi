#!/usr/bin/env python3
"""Execute actual MAX EINT, opcode queue and probe C with scripted bus/IRQ I/O.
Host fixtures test ownership/order/failure handling; no WDK or hardware build.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
DIR=ROOT/'windows/b2lusb'
header=(DIR/'b2lmax77705.h').read_text()
main=(DIR/'b2lmax77705.c').read_text()
irq=(DIR/'b2lmax77705_irq.c').read_text().replace('#include "b2lmax77705.h"','')
def function(name):
    pos=main.index(name+'(');start=main.rfind('\n\n',0,pos)+2
    brace=main.index('{',pos);end=brace+1;depth=1
    while depth: depth+=(main[end]=='{')-(main[end]=='}');end+=1
    return main[start:end]
a=header.index('typedef struct _B2L_MAX_OPCODE_REQUEST');b=header.index('WDF_DECLARE_CONTEXT_TYPE')
h=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UCHAR,*PUCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG,*PULONG;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef int32_t NTSTATUS;
typedef int BOOLEAN;
typedef void VOID;
typedef void *WDFDEVICE,*WDFWORKITEM,*WDFWAITLOCK,*WDFINTERRUPT,*WDFTIMER;
typedef struct { int signaled; } KEVENT;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { ULONG Vector; } WDF_INTERRUPT_INFO;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_NOT_SUPPORTED (-9)
#define STATUS_INSUFFICIENT_RESOURCES (-10)
#define STATUS_PENDING 0x103
#define STATUS_TIMEOUT 0x102
#define STATUS_DEVICE_NOT_READY (-1)
#define STATUS_DEVICE_CONFIGURATION_ERROR (-2)
#define STATUS_INVALID_PARAMETER (-3)
#define STATUS_IO_TIMEOUT (-4)
#define STATUS_DEVICE_PROTOCOL_ERROR (-5)
#define STATUS_BUFFER_OVERFLOW (-6)
#define STATUS_NO_SUCH_DEVICE (-7)
#define STATUS_DEVICE_DATA_ERROR (-8)
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
#define NT_ASSERT(x) assert(x)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(p,q,n) memcpy(p,q,n)
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof(a[0]))
#define C_ASSERT(x) _Static_assert(x,#x)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define WDF_INTERRUPT_INFO_INIT(p) ((p)->Vector=0)
#define SynchronizationEvent 0
#define IO_NO_INCREMENT 0
#define Executive 0
#define KernelMode 0
#include "b2lmax77705_events.h"
'''
h+='\n'.join(re.findall(r'^#define B2L_MAX_.*',header,re.M))+'\n'+header[a:b]
h+='\n'.join(re.findall(r'^#define MAX77705_.*',main.replace('\\\n',' '),re.M))+'\n'
h+=r'''
static PB2L_MAX_CONTEXT current;
static ULONG regs[0x1000/4], mmio_ack, held, queued, r2c_reads, responses, commands, cancels;
static UCHAR top, raw[4], reg_values[128][256], last_opcode, last_payload, reply_opcode;
static UCHAR written_op[64], written_data[64];
static ULONGLONG ticks, timer_due;
static UCHAR muic_actual;
static int muic_bad_read, trace_count;
static char trace[1024];
static NTSTATUS read_error, write_error, snap_error, normalize_status;
static int drop_irq, wrong_reply, stale_wakes, probe_test;
static BOOLEAN B2lMaxEvtMaxInterruptIsr(WDFINTERRUPT,ULONG);
static void WdfWaitLockAcquire(WDFWAITLOCK l,void *timeout) {
    assert(l==current->MfdLock && !held && timeout==NULL); held=1;
}
static void WdfWaitLockRelease(WDFWAITLOCK l) { assert(l==current->MfdLock && held); held=0; }
static void KeInitializeEvent(KEVENT *e,int t,int v) { assert(t==0);e->signaled=v; }
static int KeSetEvent(KEVENT *e,int n,int wait) { assert(n==0 && !wait); e->signaled=1;return 0; }
static ULONGLONG KeQueryInterruptTime(void) { return ticks; }
static BOOLEAN WdfTimerStop(WDFTIMER t,BOOLEAN wait) { (void)t; assert(!wait);timer_due=0;return 0; }
static BOOLEAN WdfTimerStart(WDFTIMER t,LONGLONG due) { (void)t;assert(due<0);timer_due=ticks+(ULONGLONG)-due;return 0; }
static WDFDEVICE WdfTimerGetParentObject(WDFTIMER t) { assert(t==current);return current; }
static NTSTATUS B2lMaxEnableOtg(PB2L_MAX_CONTEXT c) {assert(held);c->OtgEnabled=c->OtgHardwareActive=1;return 0;}
static NTSTATUS B2lMaxDisableOtg(PB2L_MAX_CONTEXT c) {assert(held);c->OtgEnabled=c->OtgHardwareActive=0;return 0;}
static NTSTATUS B2lMaxUnlockCharger(PB2L_MAX_CONTEXT c) {(void)c;assert(held);return 0;}
static NTSTATUS B2lMaxRequestMuicLocked(PB2L_MAX_CONTEXT,UCHAR);
static PB2L_MAX_CONTEXT B2lMaxGetContext(WDFDEVICE d) { assert(d==current); return current; }
static WDFDEVICE WdfInterruptGetDevice(WDFINTERRUPT i) { assert(i==current); return current; }
static void WdfInterruptGetInfo(WDFINTERRUPT i,WDF_INTERRUPT_INFO *p) { assert(i==current);p->Vector=0xa02; }
static void WdfWorkItemEnqueue(WDFWORKITEM w) { assert(w==current && current->PolicyEnabled && held); ++queued; }
static ULONG READ_REGISTER_ULONG(PULONG p) { assert(p>=regs && p<regs+RTL_NUMBER_OF(regs));return *p; }
static void WRITE_REGISTER_ULONG(PULONG p,ULONG value) {
    assert(p>=regs && p<regs+RTL_NUMBER_OF(regs));
    if (p==regs+0xa04/4) { mmio_ack=value; *p &= ~value; }
    else *p=value;
}
static NTSTATUS B2lMaxReadByte(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,UCHAR *v) {
    assert(c==current && held); if (read_error) return read_error;
    if(a==0x69 && r==0xb5) trace[trace_count++]='B';
    if(a==0x25 && r==0x0b) trace[trace_count++]='W';
    if(a==0x25 && r==0x0d) trace[trace_count++]='P';
    if(a==0x66 && r==0x22) *v=top;
    else if(a==0x25 && r==0x07) *v=0;
    else if(a==0x69 && r==0xb0) *v=1;
    else *v=reg_values[a][r];
    return 0;
}
static NTSTATUS B2lMaxReadBlock(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,UCHAR *v,ULONG n) {
    assert(c==current && held && a==0x25);
    if(read_error) return read_error;
    if(r==0x0a) { assert(n==2);trace[trace_count++]='C';v[0]=reg_values[a][r];v[1]=reg_values[a][r+1];return 0; }
    if(r==0x02) { assert(n==4); ++r2c_reads; memcpy(v,raw,4); memset(raw,0,4); return 0; }
    if(r==0x06) { assert(n==8); memset(v,0x81,n); return snap_error; }
    assert(r==0x51 && n>=1 && n<=2); ++responses;
    trace[trace_count++]='A';
    v[0]=wrong_reply ? reply_opcode : last_opcode;
    if(n==2) v[1]=(last_opcode==0x0b || last_opcode==0x0c) ? 0x1f : 0x64;
    if(last_opcode==0x05 && n==2) v[1]=muic_bad_read ? 0x11 : muic_actual;
    return 0;
}
static NTSTATUS B2lMaxWriteBlock(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,const UCHAR *v,ULONG n) {
    assert(c==current && held && a==0x25 && r==0x21 && n>=1 && n<=2);
    if(write_error) return write_error;
    assert(commands<64); written_op[commands]=last_opcode=v[0];
    written_data[commands]=last_payload=(n==2 ? v[1] : 0); ++commands;
    if(v[0]==0x06) muic_actual=v[1];
    return 0;
}
static NTSTATUS B2lMaxWriteByte(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,UCHAR v) {
    assert(c==current && held); if(write_error) return write_error;
    reg_values[a][r]=v;
    if(a==0x25 && r==0x41) { assert(v==0); raw[0]|=0x80; top|=8; }
    return 0;
}
static NTSTATUS B2lMaxUpdateByte(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,UCHAR value,UCHAR mask,UCHAR *back) {
    UCHAR old; NTSTATUS status=B2lMaxReadByte(c,a,r,&old); if(status) return status;
    status=B2lMaxWriteByte(c,a,r,(UCHAR)((old & ~mask)|(value & mask)));if(status) return status;
    *back=reg_values[a][r];
    if(probe_test && a==0x66 && r==0x23 && mask==8 && !value) {
        PB2L_MAX_OPCODE_REQUEST q=c->OpcodeHead;int count=0;
        while(q) { ++count;q=q->Next; }
        assert(count==(reg_values[0x25][0x0a]==1 && !(reg_values[0x25][0x0b]&2) ? 6 : 4));
        assert(commands==1 && responses==0 && last_opcode==0x57);
        assert(reg_values[0x25][0x10]==0x1c && reg_values[0x25][0x11]==0xff);
    }
    return 0;
}
static NTSTATUS B2lMaxNormalizeCcOpen(PB2L_MAX_CONTEXT c) {
    assert(c==current && !held && c->OpcodeHead==NULL);
    assert(responses==(reg_values[0x25][0x0a]==1 && !(reg_values[0x25][0x0b]&2) ? 6U : 4U));
    return normalize_status;
}
static NTSTATUS KeWaitForSingleObject(KEVENT *event,int r,int m,int alert,LARGE_INTEGER *t) {
    assert(!held && r==0 && m==0 && !alert && t->QuadPart<0);
    if(event->signaled) { event->signaled=0;return 0; }
    if(stale_wakes) { --stale_wakes;ticks+=1000;return 0; }
    if(drop_irq) { ticks+=(ULONGLONG)-t->QuadPart;return STATUS_TIMEOUT; }
    // While a caller sleeps, other queued opcodes can complete first. Only
    // its own event releases the waiter (as the kernel wait would behave).
    for(unsigned attempt=0;attempt<128;++attempt) {
        regs[0xa04/4]|=1U<<6;
        WdfWaitLockAcquire(current->MfdLock,NULL);
        assert(B2lMaxEvtMaxInterruptIsr(current,0));
        WdfWaitLockRelease(current->MfdLock);
        ticks+=10000;
        if(event->signaled) { event->signaled=0;return 0; }
        if(!current->OpcodeHead) break;
    }
    ticks+=(ULONGLONG)-t->QuadPart;return STATUS_TIMEOUT;
}
'''
for name in ('B2lMaxSetDiagnosticLocked','B2lMaxReadCcStatus','B2lMaxEvaluateConnection','B2lMaxReadWaterState','B2lMaxHandleDataRole','B2lMaxHandleBypass','B2lMaxDispatchBypassLocked','B2lMaxObserveIrqLocked','B2lMaxDispatchCcPdLocked'):
    h+='\n'+function(name)+'\n'
# Match the forward prototype linkage of the real callback in this TU.
h+=irq.replace('BOOLEAN\nB2lMaxEvtMaxInterruptIsr','static BOOLEAN\nB2lMaxEvtMaxInterruptIsr')
h+='\n'+function('B2lMaxWriteUsbcMasks')+'\n'+function('B2lMaxInitializeUsbc')
h+='\n'+function('B2lMaxRecordIrqFrame')
h+=r'''
static void reset(PB2L_MAX_CONTEXT c) {
    memset(c,0,sizeof(*c)); memset(regs,0xa5,sizeof(regs)); memset(reg_values,0,sizeof(reg_values));
    memset(raw,0,4); current=c; ticks=1234; held=queued=r2c_reads=responses=commands=cancels=0;
    mmio_ack=0; read_error=write_error=snap_error=normalize_status=0;
    drop_irq=wrong_reply=stale_wakes=probe_test=0; last_opcode=last_payload=reply_opcode=0;
    c->Device=c->MaxInterrupt=c->PolicyWork=c->OpcodeDeadline=c;
    timer_due=0;muic_actual=0;muic_bad_read=trace_count=0;memset(trace,0,sizeof(trace)); c->MfdLock=(void *)1;
    c->AliveRegisters=(PUCHAR)regs; c->AliveLength=0x1000; c->MaxTranslatedIrq=0xa02;
    c->PolicyEnabled=TRUE; c->MfdReady=TRUE; top=8;
    assert(B2lMaxPrepareGpio(c)==0);
    assert(B2lMaxEvtMaxInterruptEnable(c,c)==0);
    B2lMaxGpioUnmask(c);
}
static BOOLEAN deliver(void) {
    BOOLEAN result; regs[0xa04/4]|=1U<<6;
    WdfWaitLockAcquire(current->MfdLock,NULL); result=B2lMaxEvtMaxInterruptIsr(current,0);
    WdfWaitLockRelease(current->MfdLock); return result;
}
static void queue(PB2L_MAX_CONTEXT c,PB2L_MAX_OPCODE_REQUEST r,UCHAR op) {
    B2lMaxInitOpcodeRequest(r,op,0,0,1);
    WdfWaitLockAcquire(c->MfdLock,NULL);assert(B2lMaxQueueOpcodeLocked(c,r)==0);WdfWaitLockRelease(c->MfdLock);
}
int main(void) {
    B2L_MAX_CONTEXT c; B2L_MAX_OPCODE_REQUEST a,b,d; ULONG i,other;
    reset(&c);
    // GPA1-6 only: preserve every unrelated pin's configuration/mask/pending.
    assert(regs[0x20/4]==0xafa5a5a5 && regs[0x28/4]==0xa0a5a5a5);
    assert(regs[0x2c/4]==0xa0a5a5a5 && regs[0x704/4]==0xa0a5a5a5);
    assert(mmio_ack==(1U<<6)); other=regs[0xa04/4];
    assert(!B2lMaxEvtMaxInterruptIsr(&c,0) && r2c_reads==0);
    assert(regs[0xa04/4]==other);
    c.MaxTranslatedIrq=123;assert(B2lMaxEvtMaxInterruptEnable(&c,&c)<0);c.MaxTranslatedIrq=0xa02;
    B2lMaxGpioMask(&c);assert(!deliver() && r2c_reads==0);B2lMaxGpioUnmask(&c);
    queue(&c,&a,0x1c);queue(&c,&b,0x0b);queue(&c,&d,0x05);
    assert(commands==1 && responses==0 && a.Next==&b && b.Next==&d);
    assert(B2lMaxWaitOpcode(&c,&a)==0 && a.ResponseData==0x64);
    assert(commands==2 && c.OpcodeHead==&b && a.Next==NULL);
    assert(B2lMaxWaitOpcode(&c,&b)==0 && b.ResponseData==0x1f);
    assert(B2lMaxWaitOpcode(&c,&d)==0 && c.OpcodeHead==NULL && c.OpcodeTail==NULL);
    assert(c.OpcodeCompletions==3 && c.MaxInterruptCount==3 && responses==3 && r2c_reads==3);
    // No source role is inferred by the IRQ or its diagnostics alone.
    assert(!c.OtgEnabled && !c.CcStatusValid && !c.CcSourceEventCount);
    raw[0]=0x80; assert(deliver() && c.OpcodeUnexpected==1 && c.OpcodeCompletions==3);
    reset(&c); queue(&c,&a,0x1c); queue(&c,&b,0x05); drop_irq=1;stale_wakes=4;
    assert(B2lMaxWaitOpcode(&c,&a)==STATUS_IO_TIMEOUT);
    assert(ticks==a.SentAt+B2L_MAX_OPCODE_TIMEOUT_100NS && commands==1 && responses==0);
    assert(a.Complete && b.Complete && a.Next==NULL && b.Next==NULL && c.OpcodeHead==NULL);
    assert(B2lMaxWaitOpcode(&c,&b)==STATUS_IO_TIMEOUT);
    reset(&c);queue(&c,&a,0x1c);queue(&c,&b,0x05);wrong_reply=1;reply_opcode=0x0b;
    assert(B2lMaxWaitOpcode(&c,&a)==STATUS_DEVICE_PROTOCOL_ERROR && commands==1);
    assert(b.Complete && !c.OpcodeHead && c.MaxInterruptFailures==1 && c.UicInterruptSeen==0x80);
    assert(c.IrqFrameCount==1 && c.IrqFrames[0].Raw[0]==0x80);
    assert(NT_SUCCESS(c.IrqFrames[0].SnapshotStatus)); // opcode fault does not erase status evidence
    assert(regs[0x904/4]&(1U<<6)); // failed mailbox is not silently resumed
    reset(&c); queue(&c,&a,0x1c);read_error=STATUS_NO_SUCH_DEVICE;
    assert(B2lMaxWaitOpcode(&c,&a)==STATUS_NO_SUCH_DEVICE && a.Next==NULL);
    reset(&c);queue(&c,&a,0x1c);snap_error=STATUS_IO_TIMEOUT;
    assert(B2lMaxWaitOpcode(&c,&a)==0 && c.IrqFrameCount==1);
    assert(c.IrqFrames[0].SnapshotStatus==STATUS_IO_TIMEOUT);
    for(i=0;i<8;++i) assert(c.IrqFrames[0].Status[i]==0);
    B2lMaxRecordIrqFrame(&c,&c.IrqFrames[0]);
    assert(c.EventLog.Events[0].Origin==6 && !c.EventLog.Events[0].Valid && c.EventLog.SnapshotFailures==1);
    reset(&c);raw[0]=0x40;raw[1]=0x21;raw[2]=0x04;raw[3]=0xff;
    assert(deliver() && c.IrqFrameCount==1);
    assert(c.IrqFrames[0].Raw[1]==0x21 && c.IrqFrames[0].Active[1]==1);
    assert(c.IrqFrames[0].Active[2]==4 && c.IrqFrames[0].Active[3]==0x0f);
    B2lMaxRecordIrqFrame(&c,&c.IrqFrames[0]);
    assert(c.EventLog.SysmsgEvents==1 && c.EventLog.AttachErrorEvents==1);
    assert(c.EventLog.Events[0].Time==c.IrqFrames[0].Time && c.EventLog.Events[0].Valid);
    reset(&c);raw[0]=0x20;assert(deliver() && c.IrqFrames[0].DetachRecheck);
    assert(c.IrqFrames[0].Raw[1]==0 && c.IrqFrames[0].Active[1]==0 && c.MaxInterruptCount==1);
    reset(&c);raw[0]=0x20;reg_values[0x25][0x08]=0x80;
    assert(deliver() && !c.IrqFrames[0].DetachRecheck);
    reset(&c);raw[0]=0x20;reg_values[0x25][0x0a]=2;
    assert(deliver() && !c.IrqFrames[0].DetachRecheck);
    reset(&c);top=1;raw[0]=0x80;assert(deliver() && r2c_reads==0 && c.IrqFrames[0].Charger==1);
    B2lMaxRecordIrqFrame(&c,&c.IrqFrames[0]);
    assert(!c.EventLog.Events[0].Valid && c.EventLog.SnapshotFailures==0); // CHG-only: no USBC snapshot attempted
    reset(&c);c.IrqFrameCount=B2L_MAX_IRQ_FRAMES;assert(deliver());
    assert(c.IrqFrameOverflows==1 && c.MfdFault==STATUS_BUFFER_OVERFLOW);
    reset(&c);queue(&c,&a,0x1c);assert(B2lMaxEvtMaxInterruptDisable(&c,&c)==0);
    assert(a.Complete && a.Status==STATUS_DEVICE_NOT_READY && !c.OpcodeHead && !c.MfdReady);
    assert(!deliver());
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    assert(B2lMaxInitializeUsbc(&c)==0);
    assert(commands==4 && responses==4 && c.UsbcInitialized && c.CcControlConfigured);
    assert(written_op[0]==0x57 && written_data[0]==1 && written_op[1]==0x54 && written_data[1]==0);
    assert(written_op[2]==0x0c && written_data[2]==0x1f && written_op[3]==0x0b);
    assert(reg_values[0x66][0x23]==6 && reg_values[0x25][0x10]==0x1c && reg_values[0x25][0x11]==0xff);
    assert(!c.UsbcRuntimeMasksConfigured && c.OpcodeHead==NULL);
    reset(&c);probe_test=1;drop_irq=1;assert(B2lMaxInitializeUsbc(&c)==STATUS_IO_TIMEOUT);
    assert(c.OpcodeHead==NULL && c.OpcodeTail==NULL && !c.AutoIbusConfigured && !c.UsbcInitialized);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0d]=0x80;
    assert(B2lMaxInitializeUsbc(&c)==0 && c.UsbcInitialized);
    assert(commands==6 && responses==6 && c.MuicPathVerified && c.MuicControl1==9);
    assert(written_op[0]==0x57 && written_op[1]==6 && written_op[2]==5 && written_op[3]==0x54);
    assert(c.PowerRole==1 && c.DataRole==1 && c.CcSourceEventCount==0);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0d]=0;
    assert(B2lMaxInitializeUsbc(&c)==0 && c.UsbcInitialized && c.MfdFault==0);
    assert(c.DataRoleStatus==STATUS_NOT_SUPPORTED && c.MuicControl1==0x3f);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0b]=2;reg_values[0x25][0x0d]=0x80;
    assert(B2lMaxInitializeUsbc(&c)==0 && c.UsbcInitialized && c.MfdFault==0);
    assert(commands==4 && c.WaterDetected && c.DataRoleStatus==STATUS_DEVICE_NOT_READY);
    // No publication worker runs below. BYP/CC/PD must still execute while
    // a previous MUIC command awaits APCMD, in the real passive ISR.
    reset(&c);c.PowerRoleValid=c.DataRoleValid=TRUE;c.PowerRole=2;c.DataRole=1;
    c.OtgEnabled=c.OtgHardwareActive=TRUE;reg_values[0x25][0x0a]=2;
    reg_values[0x25][0x0d]=0x80;reg_values[0x69][0xb5]=1;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING && commands==1);
    WdfWaitLockRelease(c.MfdLock);
    assert(!c.MuicPathVerified && c.MuicOutstanding==1);
    top=9;raw[0]=0x80;raw[1]=0x11;raw[2]=0x20;
    assert(deliver() && strcmp(trace,"BAWCP")==0);
    assert(!c.OtgEnabled && c.OvercurrentEvents==1 && c.MuicOutstanding==1);
    assert(c.DataRoleStatus==STATUS_PENDING && !c.MuicPathVerified);
    top=8;assert(deliver() && c.MuicPathVerified && c.MuicControl1==9);
    assert(c.MuicOutstanding==0 && c.DataRoleStatus==0 && timer_due==0);
    // An old USB read-back cannot verify a newer detach request.
    reset(&c);c.PowerRoleValid=c.DataRoleValid=TRUE;c.PowerRole=2;c.DataRole=1;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING);
    assert(B2lMaxRequestMuicLocked(&c,0x3f)==STATUS_PENDING);
    WdfWaitLockRelease(c.MfdLock);
    assert(c.MuicOutstanding==2 && commands==1);
    assert(deliver() && deliver() && !c.MuicPathVerified && c.MuicStatus==STATUS_PENDING);
    assert(c.MuicControl1==9 && c.MuicOutstanding==1);
    assert(deliver() && deliver() && c.MuicPathVerified && c.MuicControl1==0x3f);
    assert(c.MuicOutstanding==0 && !c.OpcodeHead);
    // A WATER event arriving before a successful switch response keeps the
    // data-role attach blocked; callback success must not clear that reason.
    reset(&c);c.DataRoleValid=TRUE;c.DataRole=1;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING);WdfWaitLockRelease(c.MfdLock);
    raw[1]=0x10;reg_values[0x25][0x0b]=2;assert(deliver() && deliver());
    assert(c.MuicPathVerified && c.WaterDetected && c.DataRoleStatus==STATUS_DEVICE_NOT_READY);
    // Read-back mismatch is not a verified switch or a transport success.
    reset(&c);WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING);WdfWaitLockRelease(c.MfdLock);
    muic_bad_read=1;assert(deliver() && deliver());
    assert(!c.MuicPathVerified && c.MuicStatus==STATUS_DEVICE_DATA_ERROR && !c.MuicOutstanding);
    // Deadline never reads I2C or creates a completion. A stale timer callback
    // must instead rearm the actual current head's remaining deadline.
    reset(&c);WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING);WdfWaitLockRelease(c.MfdLock);
    ticks+=1000;B2lMaxEvtOpcodeDeadline(&c);
    assert(c.MuicOutstanding==1 && commands==1 && responses==0);
    ticks=timer_due;B2lMaxEvtOpcodeDeadline(&c);
    assert(c.MfdFault==STATUS_IO_TIMEOUT && c.MuicStatus==STATUS_IO_TIMEOUT);
    assert(!c.MuicPathVerified && !c.MuicOutstanding && !c.OpcodeHead && commands==1 && responses==0);
    // Capacity failure cancels old queued paths; no detach can be silently lost.
    reset(&c);WdfWaitLockAcquire(c.MfdLock,NULL);
    for(i=0;i<B2L_MAX_MUIC_SLOTS;++i) assert(B2lMaxRequestMuicLocked(&c,(i&1)?0x3f:9)==STATUS_PENDING);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_INSUFFICIENT_RESOURCES);
    WdfWaitLockRelease(c.MfdLock);
    assert(!c.OpcodeHead && !c.MuicOutstanding && !c.MuicPathVerified);
    for(i=0;i<B2L_MAX_MUIC_SLOTS;++i) assert(!c.MuicPairs[i].InUse);
    puts("MAX77705 GPIO IRQ, queue, asynchronous roles, deadline and probe order: PASS (hardware delivery unverified)");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-max-irq-test-') as directory:
    temp=Path(directory);(temp/'test.c').write_text(h)
    compiler=shlex.split(os.environ.get('CC','cc'))
    subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror','-I',str(DIR),str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
