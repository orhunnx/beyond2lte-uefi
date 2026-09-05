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
pd=(DIR/'b2lmax77705_pd.c').read_text().replace('#include "b2lmax77705.h"','')
def function(name):
    pos=main.index(name+'(');start=main.rfind('\n\n',0,pos)+2
    brace=main.index('{',pos);end=brace+1;depth=1
    while depth: depth+=(main[end]=='{')-(main[end]=='}');end+=1
    return main[start:end]
a=header.index('typedef struct _B2L_MAX_CONNECTOR_EVENT');b=header.index('WDF_DECLARE_CONTEXT_TYPE')
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
typedef void *UCMCONNECTOR, *WDFOBJECT;
typedef void *WDFDEVICE,*WDFWORKITEM,*WDFWAITLOCK,*WDFINTERRUPT,*WDFTIMER;
typedef struct { int signaled; } KEVENT;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { ULONG Vector; } WDF_INTERRUPT_INFO;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_CANCELLED (-11)
#define STATUS_DEVICE_POWER_FAILURE (-12)
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
static unsigned connector_notifications;
static void B2lMaxQueueConnectorLocked(PB2L_MAX_CONTEXT c) { (void)c; ++connector_notifications; }
static PB2L_MAX_CONTEXT current;
static ULONG regs[0x1000/4], mmio_ack, held, queued, r2c_reads, responses, commands, cancels;
static UCHAR top, raw[4], reg_values[128][256], last_opcode, last_payload, reply_opcode;
static UCHAR written_op[64], written_data[64];
static ULONGLONG ticks, timer_due, pd_due;
static UCHAR pd_response[32];
static int otg_on, otg_off;
static NTSTATUS otg_error;
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
static BOOLEAN WdfTimerStop(WDFTIMER t,BOOLEAN wait) { (void)t; assert(!wait);if(t==current->PdRecoveryTimer) pd_due=0; else timer_due=0;return 0; }
static BOOLEAN WdfTimerStart(WDFTIMER t,LONGLONG due) { (void)t;assert(due<0);if(t==current->PdRecoveryTimer) pd_due=ticks+(ULONGLONG)-due; else timer_due=ticks+(ULONGLONG)-due;return 0; }
static WDFDEVICE WdfTimerGetParentObject(WDFTIMER t) { assert(t==current || t==current->PdRecoveryTimer);return current; }
static NTSTATUS B2lMaxEnableOtg(PB2L_MAX_CONTEXT c) {assert(held);++otg_on;if(otg_error)return otg_error;c->OtgEnabled=c->OtgHardwareActive=1;return 0;}
static NTSTATUS B2lMaxDisableOtg(PB2L_MAX_CONTEXT c) {assert(held);++otg_off;if(otg_error)return otg_error;c->OtgEnabled=c->OtgHardwareActive=0;return 0;}
static NTSTATUS B2lMaxUnlockCharger(PB2L_MAX_CONTEXT c) {(void)c;assert(held);return 0;}
static NTSTATUS B2lMaxRequestMuicLocked(PB2L_MAX_CONTEXT,UCHAR);
NTSTATUS B2lMaxHandlePdMessageLocked(PB2L_MAX_CONTEXT);
NTSTATUS B2lMaxHandlePsRdyLocked(PB2L_MAX_CONTEXT);
VOID B2lMaxResetPdConnectionLocked(PB2L_MAX_CONTEXT);
VOID B2lMaxCancelPdRecoveryLocked(PB2L_MAX_CONTEXT,NTSTATUS);
VOID B2lMaxRetirePdRequestLocked(PB2L_MAX_CONTEXT,PB2L_MAX_PD_REQUEST,NTSTATUS);
static NTSTATUS B2lMaxReapplyMuicLocked(PB2L_MAX_CONTEXT,UCHAR);
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
    if(a==0x25 && r==0x0a) trace[trace_count++]='I';
    if(a==0x25 && r==0x0d) trace[trace_count++]='P';
    if(a==0x25 && r==0x0c) trace[trace_count++]='S';
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
    if(r==0x06) { if(n==5) { trace[trace_count++]='M'; memcpy(v,&reg_values[a][r],5); return 0; } assert(n==8); memset(v,0x81,n); return snap_error; }
    assert(r==0x51 && n>=1 && n<=33); ++responses;
    if(last_opcode==0x30) {
        assert(n==32); memcpy(v,pd_response,32);
        v[0]=wrong_reply ? reply_opcode : last_opcode;
        trace[trace_count++]='A';return 0;
    }
    assert(n<=2);
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
        assert(count==(reg_values[0x25][0x0a]==1 && !(reg_values[0x25][0x0b]&2) && (reg_values[0x25][0x0d]&0x80) ? 6 : 4));
        assert(commands==1 && responses==0 && last_opcode==0x57);
        assert(reg_values[0x25][0x10]==0x1c && reg_values[0x25][0x11]==0xff);
    }
    return 0;
}
static NTSTATUS B2lMaxNormalizeCcOpen(PB2L_MAX_CONTEXT c) {
    assert(c==current && !held && c->OpcodeHead==NULL);
    assert(responses==commands);
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
for name in ('B2lMaxSetDiagnosticLocked','B2lMaxReadCcStatus','B2lMaxEvaluateConnection','B2lMaxReadWaterState','B2lMaxReadCurrentAdvertisement','B2lMaxDetectMuicLocked','B2lMaxReadRidLocked','B2lMaxDispatchMuicLocked','B2lMaxHandleDataRole','B2lMaxHandleBypass','B2lMaxDispatchBypassLocked','B2lMaxObserveIrqLocked','B2lMaxDispatchCcPdLocked'):
    h+='\n'+function(name)+'\n'
# Match the forward prototype linkage of the real callback in this TU.
h+=irq.replace('BOOLEAN\nB2lMaxEvtMaxInterruptIsr','static BOOLEAN\nB2lMaxEvtMaxInterruptIsr')
h+='\n'+pd
h+='\n'+function('B2lMaxWriteUsbcMasks')+'\n'+function('B2lMaxInitializeUsbc')
h+='\n'+function('B2lMaxRecordIrqFrame')
h+=r'''
static void reset(PB2L_MAX_CONTEXT c) {
    memset(c,0,sizeof(*c)); memset(regs,0xa5,sizeof(regs)); memset(reg_values,0,sizeof(reg_values));
    memset(raw,0,4); current=c; ticks=1234; held=queued=r2c_reads=responses=commands=cancels=0;
    mmio_ack=0; read_error=write_error=snap_error=normalize_status=0;
    drop_irq=wrong_reply=stale_wakes=probe_test=0; last_opcode=last_payload=reply_opcode=0;
    c->Device=c->MaxInterrupt=c->PolicyWork=c->OpcodeDeadline=c;
    timer_due=pd_due=0;otg_on=otg_off=0;otg_error=0;memset(pd_response,0,sizeof(pd_response));c->PdRecoveryTimer=(void *)2;muic_actual=0;muic_bad_read=trace_count=0;connector_notifications=0;memset(trace,0,sizeof(trace)); c->MfdLock=(void *)1;
    c->AliveRegisters=(PUCHAR)regs; c->AliveLength=0x1000; c->MaxTranslatedIrq=0xa02;
    c->PolicyEnabled=TRUE; c->MfdReady=TRUE; c->MuicRid=7; c->RidRaw=0xff; top=8;
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
    assert(c.DataRoleStatus==STATUS_SUCCESS && commands==4 && c.MuicType==B2L_MAX_MUIC_NONE);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0b]=2;reg_values[0x25][0x0d]=0x80;
    assert(B2lMaxInitializeUsbc(&c)==0 && c.UsbcInitialized && c.MfdFault==0);
    assert(commands==4 && c.WaterDetected && c.DataRoleStatus==STATUS_DEVICE_NOT_READY);
    // No publication worker runs below. BYP/CC/PD must still execute while
    // a previous MUIC command awaits APCMD, in the real passive ISR.
    reset(&c);c.PowerRoleValid=c.DataRoleValid=TRUE;c.PowerRole=2;c.DataRole=1;
    c.DataRoleStatus=STATUS_PENDING; c.OtgEnabled=c.OtgHardwareActive=TRUE;reg_values[0x25][0x0a]=2;
    reg_values[0x25][0x0d]=0x80;reg_values[0x69][0xb5]=1;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING && commands==1);
    WdfWaitLockRelease(c.MfdLock);
    assert(!c.MuicPathVerified && c.MuicOutstanding==1);
    top=9;raw[0]=0x80;raw[1]=0x15;raw[2]=0x20;
    assert(deliver() && strcmp(trace,"BAWICP")==0);
    assert(!c.OtgEnabled && c.OvercurrentEvents==1 && c.MuicOutstanding==1);
    assert(c.DataRoleStatus==STATUS_PENDING && !c.MuicPathVerified);
    top=8;assert(deliver() && c.MuicPathVerified && c.MuicControl1==9);
    assert(c.MuicOutstanding==0 && c.DataRoleStatus==0 && timer_due==0);
    // CCI alone updates the live Rp advertisement without creating CC/DR.
    reset(&c);c.PowerRoleValid=c.DataRoleValid=TRUE;c.PowerRole=1;c.DataRole=0;
    c.CcStatus0=0x11;reg_values[0x25][0x0a]=0x31;raw[1]=4;
    assert(deliver() && strcmp(trace,"I")==0 && c.CcStatus0==0x31);
    assert(connector_notifications==1 && c.PowerRole==1 && c.DataRole==0 && !commands);
    reset(&c);reg_values[0x25][0x0a]=0x32;raw[1]=4;
    assert(deliver() && !connector_notifications && !c.PowerRoleValid && !c.DataRoleValid);
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;raw[1]=4;
    assert(deliver() && !connector_notifications); // CCI is not local source Rp
    reset(&c);c.CcStatus0=0x11;read_error=STATUS_IO_TIMEOUT;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxReadCurrentAdvertisement(&c)==STATUS_IO_TIMEOUT && c.CcStatus0==0x11 && !connector_notifications);
    WdfWaitLockRelease(c.MfdLock);
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
    // Independent cable vectors from Samsung's VPS table/resolve_chgtyp.
    // Raw UIDADC is deliberately GND in every case: OPMODE_CCIC must use RID.
    {
        static const struct { UCHAR bc,rid; ULONG cause,type,path; } cases[] = {
            {0x00,7,0x100,0,0}, {0x81,7,0x100,1,9}, {0x82,7,0x02,2,9},
            {0x83,7,0x02,4,0}, {0x85,7,0x02,5,9}, {0x85,7,0x08,1,9},
            {0x8d,7,0x100,5,9}, {0x8d,7,0x08,6,0},
            {0x89,7,0x02,6,0}, {0x92,7,0x02,6,0},
            {0xa9,7,0x02,6,0}, {0xb1,7,0x02,1,9},
            {0xb2,7,0x02,2,9}, {0x83,1,0x200,3,9}, {0,1,0x400,3,9},
            {0x81,2,0x02,1,9}, {0x80,3,0x02,7,9}, {0,3,0x02,0,0},
            {0x80,4,0x02,8,9}, {0x80,5,0x02,9,0}, {0x83,5,0x02,0,0},
            {0x83,6,0x02,10,0}, {0,6,0x02,10,0}, {3,6,0x02,0,0}
        };
        for(i=0;i<RTL_NUMBER_OF(cases);++i) {
            reset(&c);c.MuicRid=cases[i].rid;reg_values[0x25][8]=cases[i].bc;
            WdfWaitLockAcquire(c.MfdLock,NULL);
            assert(NT_SUCCESS(B2lMaxDetectMuicLocked(&c,cases[i].cause)));
            WdfWaitLockRelease(c.MfdLock);
            assert(c.MuicSampleValid && c.MuicClassified && c.MuicSamples==1);
            assert(c.MuicSample[2]==cases[i].bc && c.MuicType==cases[i].type);
            assert(!c.PowerRoleValid && !c.DataRoleValid && !c.OtgEnabled);
            if(cases[i].path) {
                assert(commands==1 && !c.MuicPathVerified);
                assert(deliver() && deliver() && c.MuicPathVerified);
                assert(c.MuicControl1==cases[i].path && c.MuicStatus==0);
            } else assert(commands==0);
            if(cases[i].type==9 || cases[i].type==10) assert(c.MuicDetectStatus==STATUS_NOT_SUPPORTED);
        }
    }
    // A live USB cable before CC/PD probe queues its path BEFORE AUTOIBUS.
    reset(&c);c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][8]=0x81;reg_values[0x25][0x0a]=1;reg_values[0x25][0x0d]=7;
    assert(B2lMaxInitializeUsbc(&c)==0 && commands==6 && responses==6);
    assert(written_op[0]==6 && written_op[1]==5 && written_op[2]==0x57);
    assert(written_op[3]==0x54 && written_op[4]==0x0c && written_op[5]==0x0b);
    assert(c.MuicType==B2L_MAX_MUIC_USB && c.MuicControl1==9 && c.MuicPathVerified);
    assert(c.DataRole==0 && c.DataRoleStatus==STATUS_SUCCESS); // analog completion, OS status is separate
    // UIC handlers precede CC/PD; their BC status never manufactures CC/DR events.
    reset(&c);reg_values[0x25][8]=0x82;raw[0]=0x22;
    assert(deliver() && strcmp(trace,"IMM")==0 && c.MuicSamples==2);
    assert(c.MuicCause==2 && c.MuicType==B2L_MAX_MUIC_CDP && !c.DataRoleValid && !c.PowerRoleValid);
    assert(c.IrqFrames[0].Raw[0]==0x22 && c.IrqFrames[0].Active[1]==0);
    // VBADC working-value adjustment retains raw bytes. An unrelated voltage
    // notification has a valid sample but must not claim a new classification.
    reset(&c);reg_values[0x25][6]=0x17;reg_values[0x25][8]=0x83;raw[0]=0x10;
    assert(deliver() && c.MuicSample[2]==0x83 && c.MuicEffectiveBc==3 && c.MuicType==0);
    reg_values[0x25][6]=0x27;raw[0]=0x10;assert(deliver());
    assert(c.MuicSampleValid && !c.MuicClassified && commands==0);
    // Failed five-byte reads do not turn zeros/stale status into a new cable.
    reset(&c);reg_values[0x25][8]=0x81;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxDetectMuicLocked(&c,2)==STATUS_PENDING);
    read_error=STATUS_IO_TIMEOUT;
    assert(B2lMaxDetectMuicLocked(&c,0x20)==STATUS_IO_TIMEOUT);
    assert(!c.MuicSampleValid && !c.MuicClassified && c.MuicSample[2]==0x81);
    assert(c.MuicType==B2L_MAX_MUIC_USB && commands==1);
    WdfWaitLockRelease(c.MfdLock);
    // Current RID is read only at probe/FCTID. Undefined FCT does not erase
    // MUIC's last accepted RID, and the 1K notification alone schedules no path.
    reset(&c);reg_values[0x25][0x0d]=3;reg_values[0x25][8]=0x80;raw[2]=1;
    assert(deliver() && c.RidRaw==3 && c.MuicRid==3 && c.RidChanges==1);
    assert(c.MuicType==B2L_MAX_MUIC_JIG_USB_OFF && !c.DataRoleValid);
    reg_values[0x25][0x0d]=0x0f;raw[2]=1;assert(deliver());
    assert(c.RidRaw==0x0f && c.MuicRid==3 && c.RidChanges==1);
    reset(&c);reg_values[0x25][0x0d]=2;raw[2]=1;assert(deliver());
    assert(c.MuicRid==2 && c.MuicSamples==0 && commands==0);
    // USB -> OTG logical role swap writes USB_AP without an intervening OPEN.
    reset(&c);c.MuicType=B2L_MAX_MUIC_USB;c.MuicPathVerified=TRUE;
    c.MuicPathRequested=c.MuicControl1=9;c.MuicRprd=TRUE;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxDetectMuicLocked(&c,0x200)==STATUS_PENDING && c.MuicType==B2L_MAX_MUIC_OTG && commands==1);
    assert(c.OpcodeHead->Opcode==6 && c.OpcodeHead->WriteData==9);
    assert(B2lMaxDetectMuicLocked(&c,0x20)==STATUS_PENDING);
    assert(c.MuicOutstanding==2 && c.MuicGeneration==2 && commands==1);
    assert(!c.MuicPathVerified); // duplicate OTG also requires a fresh response
    // A distinct charger class opens the previous data path via real opcodes.
    c.MuicRprd=FALSE;reg_values[0x25][8]=0x83;
    assert(B2lMaxDetectMuicLocked(&c,2)==0);
    assert(c.MuicType==B2L_MAX_MUIC_TA && c.MuicPathRequested==0x3f && commands==1);
    reg_values[0x25][8]=0x80;
    assert(B2lMaxDetectMuicLocked(&c,2)==0 && c.MuicType==B2L_MAX_MUIC_TA && commands==1);
    WdfWaitLockRelease(c.MfdLock);
    // PDMSG -> PS_RDY -> DATAROLE: PS_RDY queues 0x30, not a fabricated PDO.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;
    reg_values[0x25][0x0c]=0x17;reg_values[0x25][0x0d]=0x90;raw[2]=0xe0;
    assert(deliver() && strcmp(trace,"SPPM")==0 && c.DataRole==1);
    assert(c.PdSupported && !c.PdCapsValid && c.PdReadsPending==1 && commands==1);
    assert(written_op[0]==0x30 && c.OpcodeHead->ReadLength==31 && !otg_on && !otg_off);
    pd_response[1]=(UCHAR)((7<<3)|7);
    for(i=0;i<7;++i) {pd_response[2+4*i]=(UCHAR)(i+1);pd_response[5+4*i]=0xc0;}
    pd_response[2]=0x2c;pd_response[3]=0x91;pd_response[4]=1;pd_response[5]=0; // fixed 5V/3A first PDO
    assert(deliver() && c.PdCapsValid && c.PdCurrentPdo==7 && c.PdPdoCount==7);
    assert(c.PdPdos[0]==0x0001912c && c.PdPdos[6]==0xc0000007 && c.PdReadsPending==0);
    assert(!otg_on && !otg_off); // parsing APDO does not request its voltage
    assert(deliver() && deliver()); // independent MUIC WRITE/READ complete
    // A malformed selection is not a verified contract; transport still succeeded.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;raw[2]=0x40;
    assert(deliver());pd_response[1]=(3<<3)|2;assert(deliver());
    assert(!c.PdCapsValid && c.PdCapsStatus==STATUS_DEVICE_DATA_ERROR && !c.MfdFault);
    assert(c.PdCapsHeader==((3<<3)|2) && !c.PdPdoCount && !c.PdCurrentPdo);
    // Wrong mailbox opcode retires the PDO slot as a protocol failure.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;raw[2]=0x40;
    assert(deliver());wrong_reply=1;reply_opcode=0x05;assert(deliver());
    assert(c.MfdFault==STATUS_DEVICE_PROTOCOL_ERROR && c.PdCapsStatus==STATUS_DEVICE_PROTOCOL_ERROR);
    assert(!c.PdReadsPending && !c.PdRequests[0].InUse && !c.PdCapsValid && c.PdReadCompletions==1);
    // A send failure can retire synchronously: no slot/counter double release.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;write_error=STATUS_IO_TIMEOUT;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxHandlePsRdyLocked(&c)==STATUS_IO_TIMEOUT);
    assert(!c.PdReadsPending && !c.PdRequests[0].InUse && c.PdReadCompletions==1 && !c.PdReadsQueued);
    assert(c.PdCapsStatus==STATUS_IO_TIMEOUT && !c.PdCapsValid);
    WdfWaitLockRelease(c.MfdLock);
    // A PDO reply from a detached cable must be consumed but never published.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;raw[2]=0x40;
    assert(deliver());pd_response[1]=9;
    WdfWaitLockAcquire(c.MfdLock,NULL);B2lMaxResetPdConnectionLocked(&c);WdfWaitLockRelease(c.MfdLock);
    assert(deliver() && c.PdStaleResponses==1 && !c.PdCapsValid && !c.PdReadsPending);
    // Two PS_RDY events before the first reply: the newest generation wins.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxHandlePsRdyLocked(&c)==STATUS_PENDING && B2lMaxHandlePsRdyLocked(&c)==STATUS_PENDING);
    WdfWaitLockRelease(c.MfdLock);pd_response[1]=9;pd_response[2]=0x64;
    assert(deliver() && !c.PdCapsValid && c.PdStaleResponses==1 && c.PdReadsPending==1);
    assert(deliver() && c.PdCapsValid && c.PdPdos[0]==0x64 && !c.PdReadsPending);
    // Slot pressure reports an error and cancellation retires every request.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    for(i=0;i<B2L_MAX_PD_SLOTS;++i)assert(B2lMaxHandlePsRdyLocked(&c)==STATUS_PENDING);
    assert(B2lMaxHandlePsRdyLocked(&c)==STATUS_INSUFFICIENT_RESOURCES && c.PdReadsPending==B2L_MAX_PD_SLOTS);
    B2lMaxCancelOpcodesLocked(&c,STATUS_IO_TIMEOUT);
    assert(!c.PdReadsPending && !c.PdCapsValid && !c.OpcodeHead);
    for(i=0;i<B2L_MAX_PD_SLOTS;++i)assert(!c.PdRequests[i].InUse);
    WdfWaitLockRelease(c.MfdLock);
    // Source PS_RDY and PDMSG=Sink_PSRdy do not substitute for sink PS_RDY IRQ.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;reg_values[0x25][0x0d]=0x90;raw[2]=0x40;
    assert(deliver() && c.PdSupported && !commands && !c.PdCapsValid);
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0c]=1;raw[2]=0x80;
    assert(deliver() && !c.PdSupported && !commands);
    // Hard-reset recovery is an actual Linux delayed VBUS action, not IRQ polling.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;c.OtgEnabled=TRUE;
    reg_values[0x25][0x0c]=0x16;raw[2]=0x80;assert(deliver());
    assert(otg_off==1 && !otg_on && c.PdRecoveryPending && pd_due==ticks+7600000ULL);
    { ULONGLONG first=pd_due;
      ticks+=100000;raw[2]=0x80;assert(deliver() && pd_due==first);
      ticks=first-1;B2lMaxEvtPdRecoveryTimer(c.PdRecoveryTimer);assert(!otg_on && pd_due==first);
      ticks=first;B2lMaxEvtPdRecoveryTimer(c.PdRecoveryTimer);
      assert(otg_on==1 && !c.PdRecoveryPending && c.PdRecoveryAttempts==1 && !c.PdRecoveryStatus); }
    // Sender timeout uses 800 ms. Sink/cancel/PnP must prevent deferred power-on.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;reg_values[0x25][0x0c]=6;raw[2]=0x80;
    assert(deliver() && pd_due==ticks+8000000ULL);c.PowerRole=1;ticks=pd_due;
    B2lMaxEvtPdRecoveryTimer(c.PdRecoveryTimer);assert(!otg_on && !c.PdRecoveryPending);
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;reg_values[0x25][0x0c]=0x19;raw[2]=0x80;
    assert(deliver());c.PolicyEnabled=FALSE;ticks=pd_due;B2lMaxEvtPdRecoveryTimer(c.PdRecoveryTimer);
    assert(!otg_on && c.PdRecoveryStatus==STATUS_DEVICE_NOT_READY);
    // Off failure never schedules/reuses a recovery; real data IRQ still runs.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;otg_error=-90;
    reg_values[0x25][0x0c]=6;reg_values[0x25][0x0d]=0x80;raw[2]=0xa0;
    assert(deliver() && c.PdMessageStatus==-90 && !c.PdRecoveryPending && c.DataRoleValid);
    // PR transition messages operate VBUS without relabeling cached CC/DR.
    reset(&c);c.PowerRoleValid=c.DataRoleValid=TRUE;c.PowerRole=1;c.DataRole=0;
    reg_values[0x25][0x0c]=0x1d;raw[2]=0x80;assert(deliver() && otg_on==1 && c.PowerRole==1 && c.DataRole==0);
    reg_values[0x25][0x0c]=0x1a;raw[2]=0x80;assert(deliver() && otg_off==1 && !c.PdRecoveryPending);
    reg_values[0x25][0x0c]=0x17;raw[2]=0x80;assert(deliver() && otg_on==1); // documented no-op
    c.PdCapsValid=TRUE;reg_values[0x25][0x0c]=0x1c;raw[2]=0x80;
    assert(deliver() && !c.PdCapsValid && c.PdMessageStatus==STATUS_NOT_SUPPORTED);
    // Water/BYP cancel a pending recovery even while its VBUS is already off.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;reg_values[0x25][0x0c]=6;raw[2]=0x80;assert(deliver());
    reg_values[0x25][0x0b]=2;raw[1]=0x10;assert(deliver() && !c.PdRecoveryPending && !pd_due);
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=2;reg_values[0x25][0x0c]=6;raw[2]=0x80;assert(deliver());
    top=9;reg_values[0x69][0xb5]=1;assert(deliver() && !c.PdRecoveryPending && !pd_due);
    puts("MAX77705 IRQ/queue/MUIC/PD power, PDO lifetime and probe order: PASS (hardware delivery unverified)");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-max-irq-test-') as directory:
    temp=Path(directory);(temp/'test.c').write_text(h)
    compiler=shlex.split(os.environ.get('CC','cc'))
    subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror','-I',str(DIR),str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
