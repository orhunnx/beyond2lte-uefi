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
sysmsg=(DIR/'b2lmax77705_sysmsg.c').read_text().replace('#include "b2lmax77705.h"','')
control=(DIR/'b2lmax77705_control.c').read_text().replace('#include "b2lmax77705.h"','')
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
static NTSTATUS read_error, write_error, opcode_write_error, snap_error;
static unsigned reset_writes, trim_writes, delay_100ms;
static UCHAR reset_irq;
static int reset_fail_stage, control3_bad, cc_bad;
static int drop_irq, wrong_reply, stale_wakes, probe_test;
static UCHAR sys_reads[8];
static int sys_read_count, sys_read_length, sys_read_fail, afc_step, afc_fail, trace_sysmsg;
static int retry_while_waiting;
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
VOID B2lMaxClearOpcodesLocked(PB2L_MAX_CONTEXT);
VOID B2lMaxRetirePdRequestLocked(PB2L_MAX_CONTEXT,PB2L_MAX_PD_REQUEST,NTSTATUS);
VOID B2lMaxHandleSysmsgLocked(PB2L_MAX_CONTEXT);
NTSTATUS B2lMaxQueueControlLocked(PB2L_MAX_CONTEXT,UCHAR);
VOID B2lMaxRetireControlLocked(PB2L_MAX_CONTEXT,PB2L_MAX_CONTROL_REQUEST,NTSTATUS);
NTSTATUS B2lMaxRecoverControlsLocked(PB2L_MAX_CONTEXT);
NTSTATUS B2lMaxResetUsbcLocked(PB2L_MAX_CONTEXT,BOOLEAN);
NTSTATUS B2lMaxSetGrlLocked(PB2L_MAX_CONTEXT);
NTSTATUS B2lMaxWriteUsbcMasks(PB2L_MAX_CONTEXT,UCHAR,UCHAR,UCHAR,UCHAR);
static VOID B2lMaxDelayMicroseconds(ULONG us) { assert(held && us==100000);++delay_100ms;ticks+=us*10ULL; }
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
    else if(a==0x25 && r==0x02) {
        if(reset_fail_stage==5)return STATUS_IO_TIMEOUT;
        *v=reset_irq;reset_irq=0;
    }
    else if(a==0x25 && r==0x07) {
        if(trace_sysmsg)trace[trace_count++]='Y';
        ++sys_read_count;
        if(sys_read_count==sys_read_fail)return STATUS_IO_TIMEOUT;
        *v=sys_read_count<=sys_read_length ? sys_reads[sys_read_count-1] : reg_values[a][r];
    }
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
    if(last_opcode==0x70) {assert(n==3);v[0]=wrong_reply ? reply_opcode : last_opcode;v[1]=0x12;v[2]=0x34;trace[trace_count++]='A';return 0;}
    assert(n<=2);
    trace[trace_count++]='A';
    v[0]=wrong_reply ? reply_opcode : last_opcode;
    if(n==2) v[1]=(last_opcode==0x0b || last_opcode==0x0c) ? 0x1f : 0x64;
    if(last_opcode==0x0b && cc_bad) v[1]=0;
    if(last_opcode==0x1c && n==2) v[1]=control3_bad ? 0x66 : 0x64;
    if(last_opcode==0x05 && n==2) v[1]=muic_bad_read ? 0x11 : muic_actual;
    return 0;
}
static NTSTATUS B2lMaxWriteBlock(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,const UCHAR *v,ULONG n) {
    assert(c==current && held && a==0x25 && r==0x21 && n>=1 && n<=2);
    if(write_error) return write_error;
    if(opcode_write_error) return opcode_write_error;
    assert(commands<64); written_op[commands]=last_opcode=v[0];
    written_data[commands]=last_payload=(n==2 ? v[1] : 0); ++commands;
    if(v[0]==0x06) muic_actual=v[1];
    return 0;
}
static NTSTATUS B2lMaxWriteByte(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,UCHAR v) {
    assert(c==current && held);
    if((a==0x66 && r==0xfe) || (a==0x62 && r==0x0f)) {
        ++afc_step;
        if(a==0x66) { assert(v==0xc5 || v==0);trace[trace_count++]=v ? 'T' : 'L'; }
        else { assert(v==4 || v==0);trace[trace_count++]=v ? '+' : '-'; }
        if(afc_step==afc_fail)return STATUS_IO_TIMEOUT;
    }
    if(a==0x25 && r==0x80) { assert(v==0xf);++reset_writes; if(reset_fail_stage==3)return STATUS_IO_TIMEOUT;raw[0]=0; }
    if(a==0x62 && r==0x1f) { assert(v==4);++trim_writes; if(reset_fail_stage==9)return STATUS_IO_TIMEOUT; }
    if(write_error) return write_error;
    reg_values[a][r]=v;
    if(a==0x25 && r==0x41) { assert(v==0); raw[0]|=0x80; top|=8; }
    return 0;
}
static NTSTATUS B2lMaxUpdateByte(PB2L_MAX_CONTEXT c,UCHAR a,UCHAR r,UCHAR value,UCHAR mask,UCHAR *back) {
    if(reset_fail_stage==2 && a==0x66 && r==0x23 && value==8)return STATUS_IO_TIMEOUT;
    if(reset_fail_stage==4 && a==0x25 && r==0x0f)return STATUS_IO_TIMEOUT;
    if(reset_fail_stage==7 && a==0x66 && r==0x23 && value==0)return STATUS_IO_TIMEOUT;
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
static NTSTATUS KeWaitForSingleObject(KEVENT *event,int r,int m,int alert,LARGE_INTEGER *t) {
    assert(!held && r==0 && m==0 && !alert && t->QuadPart<0);
    if(event->signaled) { event->signaled=0;return 0; }
    if(retry_while_waiting) {
        ULONGLONG old_end=ticks+(ULONGLONG)-t->QuadPart;
        retry_while_waiting=0;ticks+=1000000;
        reg_values[0x25][7]=0x33;raw[0]=0x40;regs[0xa04/4]|=1U<<6;
        WdfWaitLockAcquire(current->MfdLock,NULL);
        assert(B2lMaxEvtMaxInterruptIsr(current,0));
        WdfWaitLockRelease(current->MfdLock);
        ticks=old_end;return STATUS_TIMEOUT; // old wait expired, new send has not
    }
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
h+='\n'+sysmsg+'\n'+control
h+='\n'+function('B2lMaxWriteUsbcMasks')+'\n'+function('B2lMaxInitializeUsbc')
h+='\n'+function('B2lMaxRecordIrqFrame')
h+=r'''
static void reset(PB2L_MAX_CONTEXT c) {
    memset(c,0,sizeof(*c)); memset(regs,0xa5,sizeof(regs)); memset(reg_values,0,sizeof(reg_values));
    memset(raw,0,4); current=c; ticks=1234; held=queued=r2c_reads=responses=commands=cancels=0;
    mmio_ack=0; read_error=write_error=opcode_write_error=snap_error=0;
    reset_writes=trim_writes=delay_100ms=0;reset_irq=0;reset_fail_stage=control3_bad=cc_bad=0;
    drop_irq=wrong_reply=stale_wakes=probe_test=0; last_opcode=last_payload=reply_opcode=0;
    sys_read_count=sys_read_length=sys_read_fail=afc_step=afc_fail=trace_sysmsg=retry_while_waiting=0;
    memset(sys_reads,0,sizeof(sys_reads));
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
static void finish_controls(void) {
    for(unsigned n=0; current->OpcodeHead && n<64; ++n) assert(deliver());
    assert(!current->OpcodeHead);
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
    assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);
    assert(commands==1 && responses==0 && c.ProbeStarted && !c.UsbcInitialized);
    finish_controls();
    assert(commands==6 && responses==6 && c.UsbcInitialized && c.CcControlConfigured);
    assert(written_op[0]==0x57 && written_data[0]==1 && written_op[1]==0x54 && written_data[1]==0);
    assert(written_op[2]==0x0c && written_data[2]==0x1f && written_op[3]==0x0b);
    assert(reg_values[0x66][0x23]==6 && reg_values[0x25][0x10]==0x1c && reg_values[0x25][0x11]==0xff);
    assert(!c.UsbcRuntimeMasksConfigured && c.OpcodeHead==NULL);
    reset(&c);probe_test=1;assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);
    ticks=timer_due;B2lMaxEvtOpcodeDeadline(&c);assert(c.ControlStatus==STATUS_IO_TIMEOUT);
    assert(c.OpcodeHead==NULL && c.OpcodeTail==NULL && !c.AutoIbusConfigured && !c.UsbcInitialized);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0d]=0x80;
    assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);finish_controls();assert(c.UsbcInitialized);
    assert(commands==8 && responses==8 && c.MuicPathVerified && c.MuicControl1==9);
    assert(written_op[0]==0x57 && written_op[1]==6 && written_op[2]==5 && written_op[3]==0x54);
    assert(c.PowerRole==1 && c.DataRole==1 && c.CcSourceEventCount==0);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0d]=0;
    assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);finish_controls();assert(c.UsbcInitialized && c.MfdFault==0);
    assert(c.DataRoleStatus==STATUS_SUCCESS && commands==6 && c.MuicType==B2L_MAX_MUIC_NONE);
    reset(&c);probe_test=1;c.MfdReady=FALSE;B2lMaxGpioMask(&c);
    reg_values[0x25][0x0a]=1;reg_values[0x25][0x0b]=2;reg_values[0x25][0x0d]=0x80;
    assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);finish_controls();assert(c.UsbcInitialized && c.MfdFault==0);
    assert(commands==6 && c.WaterDetected && c.DataRoleStatus==STATUS_DEVICE_NOT_READY);
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
    // Actual CC detach discards the old physical command sequence before
    // queuing OPEN. A generation check alone would still send the old writes.
    reset(&c);c.PowerRoleValid=c.DataRoleValid=TRUE;c.PowerRole=2;c.DataRole=1;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING);
    assert(B2lMaxReapplyMuicLocked(&c,9)==STATUS_PENDING);
    WdfWaitLockRelease(c.MfdLock);
    raw[0]=0;raw[1]=1;reg_values[0x25][0x0a]=0;
    assert(deliver() && c.DetachQueueClears==1 && c.QueueClears==1 && c.QueueDiscarded==4);
    assert(!c.PowerRoleValid && !c.DataRoleValid && !c.MfdFault);
    assert(commands==2 && responses==0 && written_op[1]==6 && written_data[1]==0x3f);
    assert(c.MuicOutstanding==1 && !c.MuicPathVerified && c.MuicStatus==STATUS_PENDING);
    assert(deliver() && deliver() && c.MuicPathVerified && c.MuicControl1==0x3f);
    assert(commands==3 && responses==2 && !c.MuicOutstanding);
    // The Linux PASS2 VBUSDet recheck uses the same real-detach cleanup,
    // including PDO requests and a failed VBUS-off. No OPEN follows that error.
    reset(&c);c.PowerRoleValid=TRUE;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;
    WdfWaitLockAcquire(c.MfdLock,NULL);
    assert(B2lMaxHandlePsRdyLocked(&c)==STATUS_PENDING);
    WdfWaitLockRelease(c.MfdLock);
    c.OtgEnabled=TRUE;otg_error=STATUS_IO_TIMEOUT;raw[0]=0x20;
    assert(deliver() && c.DetachQueueClears==1 && c.QueueDiscarded==1);
    assert(!c.OpcodeHead && !c.PdReadsPending && !c.PdCapsValid && !c.DataRoleValid);
    assert(commands==1 && responses==0 && otg_off==1 && c.OtgEnabled);
    // Clearing a probe command group does not retroactively complete it.
    // Recover only unsent 0c/54 after a physical APCMD, not from a timer.
    reset(&c);assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);
    raw[0]=0;raw[1]=1;assert(deliver());
    assert(c.DetachQueueClears==1 && c.RecoverMask==3 && !c.ControlPending);
    assert(c.ControlStatus==STATUS_CANCELLED && !c.UsbcInitialized && !c.MfdFault);
    assert(commands==2 && written_op[1]==6);
    ticks+=100;B2lMaxEvtOpcodeDeadline(&c);
    assert(c.RecoverMask==3 && commands==2 && responses==0);
    finish_controls();
    assert(commands==5 && written_op[2]==5 && written_op[3]==0x0c && written_op[4]==0x54);
    assert(c.ControlStatus==STATUS_CANCELLED && !c.UsbcInitialized && !c.ControlPending && !c.RecoverMask);
    assert(!c.AutoIbusConfigured && !c.CcControlConfigured && !c.CcOpenNormalized);
    // An APCMD in the same frame precedes CCSTAT (source nested IRQ order).
    reset(&c);assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);
    raw[1]=1;assert(deliver());
    assert(c.AutoIbusConfigured && c.DetachQueueClears==1 && commands==3 && responses==1);
    assert(written_op[1]==0x54 && written_op[2]==6 && c.RecoverMask==1);
    finish_controls();assert(!c.MfdFault && !c.ControlPending && !c.MuicOutstanding);
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
    assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);finish_controls();assert(commands==8 && responses==8);
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
    // SYSMSG uses three reads on its own IRQ. Retry does not complete APCMD.
    reset(&c);B2lMaxInitOpcodeRequest(&a,0x0c,1,0x1f,1);
    WdfWaitLockAcquire(c.MfdLock,NULL);assert(B2lMaxQueueOpcodeLocked(&c,&a)==0);WdfWaitLockRelease(c.MfdLock);
    queue(&c,&b,0x05);
    raw[0]=0x40;reg_values[0x25][7]=0x33;trace_sysmsg=1;
    ticks+=1000000;assert(deliver());
    assert(strcmp(trace,"YYYT+-L")==0 && sys_read_count==3 && commands==2 && responses==0);
    assert(written_op[0]==0x0c && written_op[1]==0x0c && !a.Complete && !b.Complete);
    assert(written_data[0]==0x1f && written_data[1]==0x1f);
    assert(c.OpcodeHead==&a && a.Next==&b && c.SysmsgValid && c.SysmsgValue==0x33);
    assert(c.SysmsgRetries==1 && c.SysmsgRetryOpcode==0x0c && !c.SysmsgRetryStatus && !c.SysmsgStatus);
    assert(c.AfcClockAttempts==1 && !c.AfcClockStatus && !c.AfcRelockStatus);
    assert(a.SentAt==ticks && timer_due==ticks+B2L_MAX_OPCODE_TIMEOUT_100NS);
    assert(deliver() && a.Complete && !b.Complete && commands==3);
    assert(deliver() && b.Complete && !c.OpcodeHead);
    // APCMD's status read alone does not generate a SYSMSG retry/workaround.
    reset(&c);queue(&c,&a,0x1c);reg_values[0x25][7]=0x33;
    assert(deliver() && a.Complete && !c.SysmsgEvents && !c.SysmsgRetries && !afc_step);
    // If both bits are real, APCMD retires A before SYSMSG retries current B.
    reset(&c);queue(&c,&a,0x1c);queue(&c,&b,0x05);raw[0]=0xc0;reg_values[0x25][7]=0x33;
    assert(deliver() && a.Complete && !b.Complete && commands==3 && responses==1);
    assert(written_op[1]==0x05 && written_op[2]==0x05 && c.SysmsgRetryOpcode==0x05 && sys_read_count==4);
    // Unstable reads are invalid evidence, not a manufactured 0x6d message.
    reset(&c);queue(&c,&a,0x1c);raw[0]=0x40;
    sys_read_length=3;sys_reads[0]=sys_reads[2]=0x33;sys_reads[1]=5;
    assert(deliver() && !c.SysmsgValid && c.SysmsgValue==0xff && c.SysmsgSampleCount==3);
    assert(c.SysmsgSamples[1]==5 && c.SysmsgMismatches==1 && c.SysmsgStatus==STATUS_DEVICE_DATA_ERROR);
    assert(commands==1 && !afc_step && !a.Complete && !c.MfdFault);
    // Partial transport failure cancels the outstanding request and keeps evidence.
    reset(&c);queue(&c,&a,0x1c);raw[0]=0x40;reg_values[0x25][7]=0x33;sys_read_fail=2;
    assert(deliver() && !c.SysmsgValid && c.SysmsgSampleCount==1 && c.SysmsgSamples[0]==0x33);
    assert(c.MfdFault==STATUS_IO_TIMEOUT && a.Complete && a.Status==STATUS_IO_TIMEOUT && !afc_step);
    // The AFC workaround is required even with an empty opcode queue.
    reset(&c);raw[0]=0x40;reg_values[0x25][7]=0x33;
    assert(deliver() && afc_step==4 && !commands && !c.SysmsgStatus);
    assert(c.SysmsgRetryStatus==STATUS_DEVICE_NOT_READY && c.SysmsgRetryOpcode==0xff && !c.SysmsgRetries);
    // Every write failure attempts TKEY relock; ON failure still attempts OFF.
    for(i=1;i<=4;++i) {
        reset(&c);raw[0]=0x40;reg_values[0x25][7]=0x33;afc_fail=(int)i;
        assert(deliver() && c.SysmsgStatus==STATUS_IO_TIMEOUT && c.MfdFault==STATUS_IO_TIMEOUT);
        assert(afc_step==(i==1 ? 2 : 4) && trace[trace_count-1]=='L');
        assert(c.AfcClockStatus==STATUS_IO_TIMEOUT);
        assert(c.AfcRelockStatus==(i==4 ? STATUS_IO_TIMEOUT : 0));
    }
    // A retry send failure is not disguised by successful AFC cleanup.
    reset(&c);queue(&c,&a,0x1c);raw[0]=0x40;reg_values[0x25][7]=0x33;opcode_write_error=STATUS_NO_SUCH_DEVICE;
    assert(deliver() && a.Complete && a.Status==STATUS_NO_SUCH_DEVICE && !c.OpcodeHead);
    assert(c.SysmsgRetryStatus==STATUS_NO_SUCH_DEVICE && c.SysmsgStatus==STATUS_NO_SUCH_DEVICE);
    assert(afc_step==4 && !c.AfcClockStatus && !c.AfcRelockStatus);
    // Shutdown ignores SYSMSG, while the independent APCMD path remains usable.
    reset(&c);queue(&c,&a,0x1c);c.PolicyEnabled=FALSE;raw[0]=0x40;reg_values[0x25][7]=0x33;
    assert(deliver() && !c.SysmsgEvents && !afc_step && commands==1 && !a.Complete);
    raw[0]=0x80;assert(deliver() && a.Complete && !c.SysmsgEvents && !afc_step);
    // Original GRL/VDM exceptions must never be blindly retransmitted.
    for(i=0;i<2;++i) {
        reset(&c);queue(&c,&a,i ? 0x70 : 0x48);raw[0]=0x40;reg_values[0x25][7]=0x33;
        assert(deliver() && commands==1 && a.Complete && a.Status==STATUS_CANCELLED);
        assert(c.SysmsgStatus==STATUS_CANCELLED && !c.SysmsgUnhandled && !c.SysmsgRetries && afc_step==4 && !c.MfdFault);
    }
    // A real SYSMSG retry during a sleep moves the deadline, not its completion.
    reset(&c);queue(&c,&a,0x1c);retry_while_waiting=1;
    assert(B2lMaxWaitOpcode(&c,&a)==0 && c.SysmsgRetries==1 && c.OpcodeCompletions==1);
    assert(commands==2 && responses==1 && !c.MfdFault);
    // Reset at each probe boundary retires all old durable requests. No
    // timeout/probe retry reinitializes the bus or fabricates the lost replies.
    for(i=0;i<=6;++i) {
        reset(&c);assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);
        for(unsigned n=0;n<i;++n) assert(deliver());
        unsigned before=commands;
        raw[0]=0x40;reg_values[0x25][7]=5;
        assert(deliver() && c.PorResets==1 && c.ResetStatus==0 && c.ResetStage==8);
        assert(c.ProbeStarted && !c.UsbcInitialized && !c.MfdFault && reset_writes==1 && delay_100ms==1);
        assert(c.ControlStatus==STATUS_PENDING && c.ControlPending==2);
        assert(c.RecoverMask==(i==0 ? 3U : i==1 ? 1U : 0U));
        assert(written_op[before]==0x54 && written_data[before]==0 && !otg_off);
        assert(reg_values[0x25][0xe]==4 && reg_values[0x25][0xf]==0x20);
        assert(reg_values[0x25][0x10]==0 && reg_values[0x25][0x11]==0xf0);
        assert(!(reg_values[0x66][0x23]&8));
        finish_controls();
        assert(c.UsbcInitialized && !c.ControlPending && c.ControlStatus==0 && !c.RecoverMask);
        assert(written_op[before+1]==0x0c && written_data[before+1]==0x1f);
        assert(commands==before+(i==0 ? 4U : i==1 ? 3U : 2U));
        assert(!c.AutoIbusConfigured && !c.CcControlConfigured && !c.CcOpenNormalized);
        for(unsigned n=0;n<B2L_MAX_CONTROL_SLOTS;++n) assert(!c.ControlRequests[n].InUse);
        // No initial-only 57/0b/1c command is synthesized by reset recovery.
        for(unsigned n=before;n<commands;++n) assert(written_op[n]==0x54 || written_op[n]==0x0c);
    }
    // WDT differs from POR: only a known real SOURCE powers VBUS off.
    for(i=0;i<3;++i) {
        reset(&c);c.PowerRoleValid=(i!=0);c.PowerRole=i==2 ? 2 : 1;c.OtgEnabled=1;
        reg_values[0x25][7]=3;raw[0]=0x40;assert(deliver());
        assert(otg_off==(i==2) && c.WatchdogResets==1 && !c.PorResets);
        assert(!c.DataRoleValid && !connector_notifications);finish_controls();
    }
    // Each failed stage preserves its real failure and stops further init.
    for(i=2;i<=7;++i) {
        if(i==6)continue;
        reset(&c);queue(&c,&a,0x1c);reset_fail_stage=(int)i;
        reg_values[0x25][7]=5;raw[0]=0x40;assert(deliver());
        assert(c.ResetStatus==STATUS_IO_TIMEOUT && c.ResetStage==i && c.MfdFault==STATUS_IO_TIMEOUT);
        assert(!c.OpcodeHead && !c.ControlPending && a.Complete && a.Status<0);
        assert(!c.UsbcInitialized && (regs[0x904/4]&(1U<<6)));
        assert(!trim_writes && reset_writes==(i>=3));
    }
    reset(&c);c.PowerRoleValid=1;c.PowerRole=2;otg_error=STATUS_IO_TIMEOUT;
    reg_values[0x25][7]=3;raw[0]=0x40;assert(deliver());
    assert(c.ResetStage==1 && c.ResetStatus==STATUS_IO_TIMEOUT && !reset_writes);
    reset(&c);reg_values[0x25][7]=5;raw[0]=0x40;opcode_write_error=STATUS_IO_TIMEOUT;assert(deliver());
    assert(c.ResetStage==6 && c.ResetStatus==STATUS_IO_TIMEOUT && !c.ControlPending && !c.OpcodeHead);
    // Saved reset UIC is actual read-to-clear data. CHG-only IRQ must not
    // consume it; the next real USBC IRQ merges it without changing Raw[].
    reset(&c);reset_irq=0x62;reg_values[0x25][7]=5;raw[0]=0x40;assert(deliver());
    assert(c.ResetUicRaw==0x62 && c.SavedUic==0x22 && !c.LastUicReplay && c.UicReplayCount==0);
    top=1;assert(deliver() && c.SavedUic==0x22);
    top=8;raw[0]=0x80;assert(deliver());
    assert(!c.SavedUic && c.LastUicReplay==0x22 && c.UicReplayCount==1);
    assert(c.IrqFrames[2].Raw[0]==0x80 && c.IrqFrames[2].Active[0]==0xa2 && c.IrqFrames[2].UicReplay==0x22);
    assert(c.SysmsgEvents==1 && c.PorResets==1);finish_controls();
    // GRL clears software requests, preserves CC and MUIC/PD slot lifetimes,
    // sends 70 01 / reads two bytes; trim requires the matching actual reply.
    reset(&c);queue(&c,&a,0x1c);queue(&c,&b,0x0c);queue(&c,&d,0x54);
    WdfWaitLockAcquire(c.MfdLock,NULL);assert(B2lMaxRequestMuicLocked(&c,9)==STATUS_PENDING);WdfWaitLockRelease(c.MfdLock);
    c.PowerRoleValid=1;c.PowerRole=2;raw[0]=0x40;reg_values[0x25][7]=0x64;
    assert(deliver() && c.GrlRequests==1 && c.GrlStatus==STATUS_PENDING && !trim_writes && !reset_writes);
    assert(a.Complete && b.Complete && d.Complete && a.Status==STATUS_CANCELLED && b.Status==STATUS_CANCELLED);
    assert(!c.MuicOutstanding && !c.MuicPathVerified && c.MuicStatus==STATUS_CANCELLED);
    assert(!connector_notifications && c.PowerRole==2 && !c.MfdFault && c.RecoverMask==3);
    assert(commands==2 && written_op[1]==0x70 && written_data[1]==1 && !responses);
    assert(deliver() && trim_writes==1 && delay_100ms==1 && c.GrlStatus==0);
    assert(written_op[2]==0x0c && !c.RecoverMask && c.ControlPending==2);
    finish_controls();assert(written_op[3]==0x54 && c.UsbcInitialized && !c.ControlPending);
    // Lost/mismatched response or failed trim cannot turn into GRL success.
    for(i=0;i<3;++i) {
        reset(&c);raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());
        if(i==0) {ticks=timer_due;B2lMaxEvtOpcodeDeadline(&c);}
        if(i==1) {wrong_reply=1;reply_opcode=0x54;assert(deliver());}
        if(i==2) {reset_fail_stage=9;assert(deliver());}
        assert(c.GrlStatus<0 && c.ControlStatus<0 && c.MfdFault<0);
        assert(trim_writes==(i==2) && !c.ControlPending && !c.OpcodeHead && !c.UsbcInitialized);
    }
    // GRL APCMD_FAIL dequeues only its response. Successor stays unsent;
    // its next APCMD sends it, a later APCMD reads its actual reply.
    reset(&c);raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());queue(&c,&a,0x1c);
    raw[0]=0x40;reg_values[0x25][7]=0x33;assert(deliver());
    assert(c.GrlStatus==STATUS_CANCELLED && !trim_writes && commands==1 && !responses);
    assert(c.OpcodeHead==&a && !a.SentAt && !c.MfdFault && !timer_due);
    B2lMaxEvtOpcodeDeadline(&c);assert(commands==1 && !responses);
    raw[0]=0x80;assert(deliver() && commands==2 && !responses && !a.Complete);
    assert(deliver() && a.Complete && responses==1 && !trim_writes);
    // Repeated reset/GRL cancels durable recovery slots too, with no old
    // continuation firing, double retirement, or leaked queue links.
    reset(&c);raw[0]=0x40;reg_values[0x25][7]=5;assert(deliver());
    raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());
    raw[0]=0x40;reg_values[0x25][7]=3;assert(deliver());finish_controls();
    assert(c.PorResets==1 && c.WatchdogResets==1 && c.GrlRequests==1 && c.GrlStatus==STATUS_CANCELLED);
    assert(c.UsbcInitialized && !c.ControlPending && !trim_writes && !c.MfdFault);
    // Actual CONTROL3 read drives the optional LockErrorEn write. An
    // unchanged sticky bit fails verification, never a fabricated readback.
    reset(&c);control3_bad=1;assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);finish_controls();
    assert(written_op[5]==0x1d && written_data[5]==0x64 && written_op[6]==0x1c);
    assert(!c.CcOpenNormalized && c.ControlStatus==STATUS_DEVICE_DATA_ERROR && c.MfdFault<0);
    reset(&c);cc_bad=1;assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);finish_controls();
    assert(!c.CcControlConfigured && !c.UsbcInitialized && c.ControlStatus<0 && !c.ControlPending);
    // Pending PD slots are retired too, without a synthetic PD detach or
    // a successful PDO publication when GRL cancels their mailbox response.
    reset(&c);c.PowerRoleValid=1;c.PowerRole=1;reg_values[0x25][0x0d]=0x10;
    raw[2]=0x40;assert(deliver() && c.PdReadsPending==1);
    raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());
    assert(!c.PdReadsPending && !c.PdCapsValid && c.PdCapsStatus==STATUS_CANCELLED);
    for(i=0;i<B2L_MAX_PD_SLOTS;++i) assert(!c.PdRequests[i].InUse);
    finish_controls();
    // Software START retains NOT_READY until a real alt-mode consumer exists.
    reset(&c);queue(&c,&a,0x1c);queue(&c,&b,0x55);
    raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());
    assert(c.RecoverMask==4);finish_controls();
    assert(!c.AltModeStartRequested && c.AltModeStatus==STATUS_DEVICE_NOT_READY && c.UsbcInitialized);
    assert(commands==2 && written_op[1]==0x70);
    // Shutdown can drain the actual sent response, but it must not start
    // CONTROL3/recover continuations or execute delayed GRL policy writes.
    reset(&c);raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());c.PolicyEnabled=FALSE;
    assert(deliver() && c.GrlStatus==STATUS_CANCELLED && !trim_writes && !c.ControlPending);
    reset(&c);control3_bad=1;assert(B2lMaxInitializeUsbc(&c)==STATUS_PENDING);c.PolicyEnabled=FALSE;
    finish_controls();assert(commands==5 && c.ControlStatus==STATUS_CANCELLED && !c.CcOpenNormalized);
    // A bus error in AFC is not hidden by the earlier GRL response discard.
    reset(&c);raw[0]=0x40;reg_values[0x25][7]=0x64;assert(deliver());
    raw[0]=0x40;reg_values[0x25][7]=0x33;afc_fail=3;assert(deliver());
    assert(c.GrlStatus==STATUS_CANCELLED && c.SysmsgStatus==STATUS_IO_TIMEOUT && c.MfdFault==STATUS_IO_TIMEOUT);
    // A failed APCMD must not hide a genuine POR in the same raw frame.
    // Fault history survives; only the actual reset sequence admits new work.
    reset(&c);queue(&c,&a,0x1c);queue(&c,&b,0x05);
    raw[0]=0xc0;reg_values[0x25][7]=5;wrong_reply=1;reply_opcode=0x54;assert(deliver());
    assert(a.Complete && a.Status==STATUS_DEVICE_PROTOCOL_ERROR && b.Complete);
    assert(c.ResetPriorFault==STATUS_DEVICE_PROTOCOL_ERROR && c.ResetStatus==0 && c.ResetStage==8);
    assert(!c.MfdFault && c.ControlStatus==STATUS_PENDING && commands==2 && written_op[1]==0x54);
    wrong_reply=0;finish_controls();assert(c.UsbcInitialized && c.ControlStatus==0);
    // A failed reset never clears that earlier error as if recovery succeeded.
    reset(&c);queue(&c,&a,0x1c);raw[0]=0xc0;reg_values[0x25][7]=5;
    wrong_reply=1;reply_opcode=0x54;reset_fail_stage=4;assert(deliver());
    assert(c.ResetPriorFault==STATUS_DEVICE_PROTOCOL_ERROR && c.ResetStatus==STATUS_IO_TIMEOUT);
    assert(c.MfdFault==STATUS_IO_TIMEOUT && !c.ControlPending && !c.OpcodeHead && !c.UsbcInitialized);
    puts("MAX77705 IRQ/queue/MUIC/PD/SYSMSG reset/GRL/recovery: PASS (hardware delivery unverified)");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-max-irq-test-') as directory:
    temp=Path(directory);(temp/'test.c').write_text(h)
    compiler=shlex.split(os.environ.get('CC','cc'))
    subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror','-I',str(DIR),str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
