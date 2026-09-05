#!/usr/bin/env python3
"""Run actual PnP/power callbacks against resource and interrupt-state fixtures.
No device/WDK build. Checks the IRQ boundary around MAX client transactions.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'windows/b2lusb/b2lmax77705.c').read_text()
header=(ROOT/'windows/b2lusb/b2lmax77705.h').read_text()
def function(name):
    pos=source.index(name+'('); start=source.rfind('\n\n',0,pos)+2
    brace=source.index('{',pos); end=brace+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
a=header.index('typedef struct _B2L_MAX_OPCODE_REQUEST');b=header.index('} B2L_MAX_CONTEXT, *PB2L_MAX_CONTEXT;',a)+len('} B2L_MAX_CONTEXT, *PB2L_MAX_CONTEXT;')
h=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UCHAR, *PUCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG, *PULONG;
typedef uint64_t ULONGLONG;
typedef int32_t NTSTATUS;
typedef int BOOLEAN;
typedef void VOID;
typedef void *WDFDEVICE, *WDFWORKITEM, *WDFWAITLOCK, *WDFINTERRUPT;
typedef int KEVENT, WDF_POWER_DEVICE_STATE;
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_CONFIGURATION_ERROR (-1)
#define STATUS_INSUFFICIENT_RESOURCES (-2)
#define STATUS_NO_SUCH_DEVICE (-3)
#define STATUS_UNSUCCESSFUL (-4)
#define STATUS_DEVICE_NOT_READY (-5)
#define PAGE_READWRITE 1
#define PAGE_NOCACHE 2
#define CM_RESOURCE_INTERRUPT_LATCHED 1
#define CM_RESOURCE_INTERRUPT_MESSAGE 2
#define CmResourceTypeMemory 1
#define CmResourceTypeInterrupt 2
#define CmResourceTypeDevicePrivate 3
#define RtlZeroMemory(p,n) memset(p,0,n)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define WDF_REL_TIMEOUT_IN_MS(x) (x)
#define C_ASSERT(x) _Static_assert(x,#x)
#define MAX77705_MUIC_COM_OPEN 0x3f
#include "b2lmax77705_events.h"
'''
h+='\n'.join(re.findall(r'^#define B2L_MAX_.*',header,re.M))+'\n'+header[a:b]+'\n'
h+=r'''
typedef struct { int64_t QuadPart; } PHYSICAL_ADDRESS;
typedef struct {
    int Type; ULONG Flags;
    union {
        struct { PHYSICAL_ADDRESS Start; ULONG Length; } Memory;
        struct { ULONG Vector; } Interrupt;
    } u;
} CM_PARTIAL_RESOURCE_DESCRIPTOR, *PCM_PARTIAL_RESOURCE_DESCRIPTOR;
typedef struct { ULONG Count; CM_PARTIAL_RESOURCE_DESCRIPTOR Entries[10]; } RESOURCES, *WDFCMRESLIST;
static PB2L_MAX_CONTEXT current;
static int held, starts, stops, off_calls, muic_calls, quiesces, unmaps, maps;
static NTSTATUS init_status, off_status, muic_status, diagnostic;
static ULONG diagnostic_stage;
static ULONG recorded, cc_dispatches, pd_dispatches, byp_dispatches, water_dispatches;
static char dispatch_order[32];static int dispatch_count;
static NTSTATUS service_status, cc_status;
static WDFDEVICE WdfWorkItemGetParentObject(WDFWORKITEM w) { assert(w==current);return current; }
static void B2lMaxRecordIrqFrame(PB2L_MAX_CONTEXT c,const B2L_MAX_IRQ_FRAME *f) {
    assert(c==current && f && held==1); ++recorded;
}
static NTSTATUS B2lMaxServiceConnection(PB2L_MAX_CONTEXT c) {
    assert(held==1);c->DiagnosticStage=B2L_MAX_STAGE_IDENTITY;
    if(NT_SUCCESS(service_status)) c->UsbcInitialized=TRUE;
    return service_status;
}
static NTSTATUS B2lMaxEvaluateConnection(PB2L_MAX_CONTEXT c,BOOLEAN initial) {
    assert(c==current && held==1 && !initial);++cc_dispatches;dispatch_order[dispatch_count++]='C';return cc_status;
}
static NTSTATUS B2lMaxHandleBypass(PB2L_MAX_CONTEXT c) {
    assert(c==current && held==1);++byp_dispatches;dispatch_order[dispatch_count++]='B';return 0;
}
static NTSTATUS B2lMaxReadWaterState(PB2L_MAX_CONTEXT c) {
    assert(c==current && held==1);++water_dispatches;dispatch_order[dispatch_count++]='W';return 0;
}
static NTSTATUS B2lMaxHandleDataRole(PB2L_MAX_CONTEXT c,BOOLEAN initial) {
    assert(c==current && held==1 && !initial);++pd_dispatches;dispatch_order[dispatch_count++]='P';return 0;
}
#define MAX77705_CC_INT_STATE 1U
#define MAX77705_CC_INT_WATER 0x10U
#define MAX77705_PD_INT_DATAROLE 0x20U
static PB2L_MAX_CONTEXT B2lMaxGetContext(WDFDEVICE d) { assert(d==current); return current; }
static void WdfWaitLockAcquire(WDFWAITLOCK l, void *t) {
    int bit=(int)(uintptr_t)l; assert(bit && !(held&bit) && t==NULL); held|=bit;
}
static void WdfWaitLockRelease(WDFWAITLOCK l) {
    int bit=(int)(uintptr_t)l; assert(held&bit); held&=~bit;
}
static void WdfWorkItemEnqueue(WDFWORKITEM w) {
    (void)w; assert(current->HsI2cIrqEnabled && current->MaxIrqEnabled && current->PolicyEnabled);
    ++starts;
}
static void WdfWorkItemFlush(WDFWORKITEM w) {
    (void)w; assert(!held && !current->PolicyEnabled); ++stops;
}
static NTSTATUS B2lMaxPrepareGpio(PB2L_MAX_CONTEXT c) {
    assert(c==current && held==1 && c->PlatformPrepared && !c->MaxIrqEnabled); return 0;
}
static void B2lMaxGpioMask(PB2L_MAX_CONTEXT c) { assert(c==current && (held&4)); }
static void B2lMaxCancelOpcodesLocked(PB2L_MAX_CONTEXT c,NTSTATUS s) {
    assert(c==current && (held&4) && !NT_SUCCESS(s)); c->OpcodeHead=c->OpcodeTail=NULL;c->MfdFault=s;
}
static ULONGLONG KeQueryInterruptTime(void) { return 123456; }
static NTSTATUS B2lMaxHsI2cInitialize(PB2L_MAX_CONTEXT c) {
    assert(c==current && held && !c->HsI2cIrqEnabled);
    c->PlatformPrepared=NT_SUCCESS(init_status); return init_status;
}
static void B2lMaxPublishDiagnostic(PB2L_MAX_CONTEXT c, ULONG stage, NTSTATUS status) {
    assert(c==current); diagnostic=status; diagnostic_stage=stage;c->DiagnosticStage=stage;
}
static NTSTATUS B2lMaxDisableOtg(PB2L_MAX_CONTEXT c) {
    assert(held && c->HsI2cIrqEnabled && stops>0); ++off_calls;
    if (NT_SUCCESS(off_status)) c->OtgEnabled=c->OtgHardwareActive=FALSE;
    return off_status;
}
static NTSTATUS B2lMaxSetMuicPath(PB2L_MAX_CONTEXT c, UCHAR value) {
    assert(held && c->HsI2cIrqEnabled && off_calls>0 && value==0x3f); ++muic_calls;
    return muic_status;
}
static void B2lMaxHsI2cQuiesce(PB2L_MAX_CONTEXT c) { assert(c==current && held==7); ++quiesces; }
static void B2lMaxUnmapResources(PB2L_MAX_CONTEXT c) {
    ++unmaps; c->HardwareReady=c->PlatformPrepared=FALSE;
    c->HsI2cRegisters=c->UsiRegisters=c->CmgpCmuRegisters=c->CmgpPinctrlRegisters=c->AliveRegisters=NULL;
}
static ULONG WdfCmResourceListGetCount(WDFCMRESLIST r) { return r->Count; }
static PCM_PARTIAL_RESOURCE_DESCRIPTOR WdfCmResourceListGetDescriptor(WDFCMRESLIST r, ULONG i) {
    return i < r->Count ? &r->Entries[i] : NULL;
}
static void *MmMapIoSpaceEx(PHYSICAL_ADDRESS a, ULONG n, ULONG flags) {
    assert(n>0 && flags==(PAGE_READWRITE|PAGE_NOCACHE)); ++maps;
    return (void *)(uintptr_t)a.QuadPart;
}
'''
for name in ('B2lMaxEvtPrepareHardware','B2lMaxEvtReleaseHardware','B2lMaxEvtD0Entry',
             'B2lMaxEvtD0EntryPostInterruptsEnabled','B2lMaxEvtD0ExitPreInterruptsDisabled','B2lMaxEvtPolicyWork'):
    h+='\n'+function(name)+'\n'
h+=r'''
static void reset(PB2L_MAX_CONTEXT c) {
    memset(c,0,sizeof(*c)); current=c; held=starts=stops=off_calls=muic_calls=quiesces=unmaps=maps=0;
    init_status=off_status=muic_status=diagnostic=service_status=cc_status=0;
    recorded=cc_dispatches=pd_dispatches=byp_dispatches=water_dispatches=0;
    memset(dispatch_order,0,sizeof(dispatch_order));dispatch_count=0; diagnostic_stage=0;
    c->BusLock=(void *)1; c->TransportLock=(void *)2; c->MfdLock=(void *)4;
    c->HardwareReady=c->PlatformPrepared=c->IdentityReady=TRUE;
    c->OtgEnabled=c->OtgHardwareActive=TRUE;
}
static void resources(RESOURCES *r) {
    const ULONG bases[]={0x15c00000,0x15d60000,0x15c22000,0x15c30000,0x15850000}; ULONG i;
    memset(r,0,sizeof(*r)); r->Count=7;
    for(i=0;i<5;++i) {
        r->Entries[i].Type=CmResourceTypeMemory;
        r->Entries[i].u.Memory.Start.QuadPart=bases[i];
        r->Entries[i].u.Memory.Length=i==0 ? 0x8000 : 0x1000;
    }
    r->Entries[5].Type=CmResourceTypeInterrupt; r->Entries[5].u.Interrupt.Vector=114;
    r->Entries[6].Type=CmResourceTypeInterrupt; r->Entries[6].u.Interrupt.Vector=51;
}
int main(void) {
    B2L_MAX_CONTEXT c; RESOURCES raw,translated;
    reset(&c); resources(&raw); translated=raw; translated.Entries[5].u.Interrupt.Vector=0xa01;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)==0);
    assert(c.HardwareReady && maps==5 && c.HsI2cRawIrq==114 && c.HsI2cTranslatedIrq==0xa01 && c.MaxRawIrq==51 && c.AliveRegisters);
    raw.Entries[5].u.Interrupt.Vector=51; // MAX EINT cannot substitute for HSI2C GSIV
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)==STATUS_DEVICE_CONFIGURATION_ERROR);
    assert(!c.HardwareReady && !c.HsI2cRegisters && c.HsI2cRawIrq==0);
    resources(&raw); translated=raw; translated.Count=4;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)<0 && !c.HardwareReady);
    resources(&raw); translated=raw; raw.Count=4;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)<0 && !c.HardwareReady);
    resources(&raw); translated=raw; translated.Entries[5].Flags=CM_RESOURCE_INTERRUPT_LATCHED;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)<0 && !c.HardwareReady);
    resources(&raw); translated=raw; raw.Entries[7]=raw.Entries[5]; translated.Entries[7]=translated.Entries[5];
    raw.Count=translated.Count=8;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)<0 && !c.HardwareReady);
    resources(&raw); translated=raw; raw.Entries[5].u.Interrupt.Vector=51;raw.Entries[6].u.Interrupt.Vector=114;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)<0 && !c.HardwareReady); // reversed IRQ ownership
    resources(&raw); translated=raw; translated.Entries[4].Type=CmResourceTypeDevicePrivate;
    assert(B2lMaxEvtPrepareHardware(&c,&raw,&translated)<0 && !c.HardwareReady); // missing ALIVE
    reset(&c); c.HsI2cInterruptCount=100; c.HsI2cCompletionCount=99;
    assert(B2lMaxEvtD0Entry(&c,0)==0 && starts==0 && !c.PolicyEnabled);
    assert(c.HsI2cInterruptCount==0 && c.HsI2cCompletionCount==0 && c.PlatformPrepared);
    c.HsI2cIrqEnabled=c.MaxIrqEnabled=TRUE; // framework enable occurs between these callbacks
    assert(B2lMaxEvtD0EntryPostInterruptsEnabled(&c,0)==0 && starts==1);
    assert(B2lMaxEvtD0ExitPreInterruptsDisabled(&c,0)==0);
    assert(stops==1 && off_calls==1 && muic_calls==1 && quiesces==1 && !c.OtgHardwareActive);
    c.HsI2cIrqEnabled=c.MaxIrqEnabled=FALSE; // framework disconnect, then ReleaseHardware
    assert(B2lMaxEvtReleaseHardware(&c,NULL)==0 && off_calls==1 && muic_calls==1 && unmaps==1);
    reset(&c); init_status=STATUS_UNSUCCESSFUL;
    assert(B2lMaxEvtD0Entry(&c,0)==STATUS_UNSUCCESSFUL && !c.PlatformPrepared && starts==0);
    assert(diagnostic==STATUS_UNSUCCESSFUL && diagnostic_stage==B2L_MAX_STAGE_BUS);
    reset(&c); c.HsI2cIrqEnabled=c.MaxIrqEnabled=TRUE; off_status=STATUS_NO_SUCH_DEVICE;
    assert(B2lMaxEvtD0ExitPreInterruptsDisabled(&c,0)==STATUS_NO_SUCH_DEVICE);
    assert(c.OtgHardwareActive && diagnostic==STATUS_NO_SUCH_DEVICE && diagnostic_stage==B2L_MAX_STAGE_OTG_OFF);
    reset(&c); c.HsI2cIrqEnabled=c.MaxIrqEnabled=TRUE; muic_status=STATUS_UNSUCCESSFUL;
    assert(B2lMaxEvtD0ExitPreInterruptsDisabled(&c,0)==STATUS_UNSUCCESSFUL);
    assert(diagnostic_stage==B2L_MAX_STAGE_MUIC_OPEN && !c.OtgHardwareActive);
    // A fault must still drain every read-to-clear frame into diagnostics.
    reset(&c); c.PolicyEnabled=c.UsbcInitialized=TRUE; c.MfdFault=STATUS_UNSUCCESSFUL;
    c.IrqFrameCount=2; c.IrqFrames[0].Active[1]=1;
    B2lMaxEvtPolicyWork(&c); assert(recorded==2 && cc_dispatches==0 && c.IrqFrameCount==0 && diagnostic<0);
    reset(&c);c.PolicyEnabled=TRUE;service_status=STATUS_NO_SUCH_DEVICE;c.IrqFrameCount=1;
    B2lMaxEvtPolicyWork(&c);assert(recorded==1 && cc_dispatches==0 && diagnostic_stage==B2L_MAX_STAGE_IDENTITY);
    reset(&c);c.PolicyEnabled=c.UsbcInitialized=TRUE;c.IrqFrameCount=3;
    c.IrqFrames[0].Active[1]=8;c.IrqFrames[1].Active[1]=4;c.IrqFrames[2].Active[1]=1;
    B2lMaxEvtPolicyWork(&c);assert(recorded==3 && cc_dispatches==1 && !held);
    reset(&c);c.PolicyEnabled=c.UsbcInitialized=TRUE;c.IrqFrameCount=3;
    c.IrqFrames[0].Active[1]=1;cc_status=STATUS_NO_SUCH_DEVICE;
    B2lMaxEvtPolicyWork(&c);assert(recorded==3 && cc_dispatches==1 && diagnostic==STATUS_NO_SUCH_DEVICE);
    reset(&c);c.PolicyEnabled=c.UsbcInitialized=TRUE;c.IrqFrameCount=1;
    c.IrqFrames[0].Charger=1;c.IrqFrames[0].Active[1]=0x11;c.IrqFrames[0].Active[2]=0x20;
    B2lMaxEvtPolicyWork(&c);assert(strcmp(dispatch_order,"BWCP")==0);
    reset(&c);c.PolicyEnabled=c.UsbcInitialized=TRUE;c.MfdFault=STATUS_UNSUCCESSFUL;c.IrqFrameCount=1;
    c.IrqFrames[0].Charger=1;c.IrqFrames[0].Active[1]=1;c.IrqFrames[0].Active[2]=0x20;
    B2lMaxEvtPolicyWork(&c);assert(strcmp(dispatch_order,"B")==0 && recorded==1 && diagnostic<0);
    reset(&c);c.PolicyEnabled=c.UsbcInitialized=TRUE;c.IrqFrameCount=1;c.IrqFrames[0].DetachRecheck=TRUE;
    B2lMaxEvtPolicyWork(&c);assert(cc_dispatches==1 && c.CcInterruptCount==0 && c.DetachRechecks==1);
    puts("MAX77705 resource and power/IRQ lifecycle tests: PASS (KMDF/device unverified)");
}
'''
h=re.sub(r'\b(0[xX][0-9A-Fa-f]+|[0-9]+)UL\b',r'\1U',h)
with tempfile.TemporaryDirectory(prefix='b2l-power-test-') as temp:
    c=Path(temp)/'power.c'; exe=Path(temp)/'power'; c.write_text(h)
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-O0','-I',str(ROOT/'windows/b2lusb'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
