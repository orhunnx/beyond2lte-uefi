#!/usr/bin/env python3
"""Execute the actual DeviceAdd with object-type-aware WDF contract doubles.
This is a host regression, not a Windows KMDF/device test.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'windows/b2lusb/b2lmax77705.c').read_text()
start = source.index('NTSTATUS\nB2lMaxEvtDeviceAdd(')
end = source.index('\nNTSTATUS\nB2lMaxEvtPrepareHardware', start)
function = source[start:end]
# The negative control must fail with the observed status when the timer's
# attributes leak into WdfWorkItemCreate as they did in .42.
reset = '''    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    WDF_WORKITEM_CONFIG_INIT(&WorkConfig, B2lMaxEvtPolicyWork);'''
assert reset in function
legacy = function.replace('B2lMaxEvtDeviceAdd(', 'LegacyDeviceAdd(', 1).replace(
    reset, '    WDF_WORKITEM_CONFIG_INIT(&WorkConfig, B2lMaxEvtPolicyWork);')
h = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef int32_t NTSTATUS;
typedef void *WDFDRIVER, *PWDFDEVICE_INIT, *WDFDEVICE;
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define RtlZeroMemory(p,n) memset(p,0,n)
#define FALSE 0
#define TRUE 1
#define WdfFalse 0
#define SynchronizationEvent 0
#define WdfExecutionLevelInheritFromParent 1
#define WdfExecutionLevelPassive 2
#define WdfSynchronizationScopeInheritFromParent 1
#define INVALID_LEVEL ((NTSTATUS)0xC0200211U)
#define INJECTED ((NTSTATUS)0xC000009AU)
typedef struct {
    void *Device, *HsI2cInterrupt, *BusLock, *TransportLock, *MfdLock;
    void *MaxInterrupt, *OpcodeDeadline, *PdRecoveryTimer, *PolicyWork, *ConnectorWork;
    int HsI2cCompletion;
    unsigned char CcStatus0, OpcodeResponse, AutoIbusMode, CcControlResponse;
    unsigned char CcControlReadBack, Control3Before, Control3After;
    unsigned char UicInterruptMask, CcInterruptMask, PdInterruptMask, VdmInterruptMask;
    unsigned char MuicPathRequested, MuicControl1;
} B2L_MAX_CONTEXT, *PB2L_MAX_CONTEXT;
static B2L_MAX_CONTEXT context;
typedef struct { int ExecutionLevel, SynchronizationScope; void *ParentObject; } WDF_OBJECT_ATTRIBUTES;
typedef struct { int callback, AutomaticSerialization; } WDF_WORKITEM_CONFIG, WDF_TIMER_CONFIG;
typedef struct {
    int isr, dpc, AutomaticSerialization, ShareVector, ReportInactiveOnPowerDown;
    int EvtInterruptEnable, EvtInterruptDisable, PassiveHandling; void *WaitLock;
} WDF_INTERRUPT_CONFIG;
typedef struct {
    int EvtDevicePrepareHardware, EvtDeviceReleaseHardware, EvtDeviceD0Entry;
    int EvtDeviceD0EntryPostInterruptsEnabled, EvtDeviceD0ExitPreInterruptsDisabled;
} WDF_PNPPOWER_EVENT_CALLBACKS;
typedef int UCM_MANAGER_CONFIG;
enum {
    B2lMaxEvtPrepareHardware=1, B2lMaxEvtReleaseHardware, B2lMaxEvtD0Entry,
    B2lMaxEvtD0EntryPostInterruptsEnabled, B2lMaxEvtD0ExitPreInterruptsDisabled,
    B2lMaxHsI2cEvtInterruptIsr, B2lMaxHsI2cEvtInterruptDpc,
    B2lMaxHsI2cEvtInterruptEnable, B2lMaxHsI2cEvtInterruptDisable,
    B2lMaxEvtMaxInterruptIsr, B2lMaxEvtMaxInterruptEnable, B2lMaxEvtMaxInterruptDisable,
    B2lMaxEvtOpcodeDeadline, B2lMaxEvtPdRecoveryTimer, B2lMaxEvtPolicyWork, B2lMaxEvtConnectorWork
};
#define WDF_OBJECT_ATTRIBUTES_INIT(a) (*(a)=(WDF_OBJECT_ATTRIBUTES){1,1,NULL})
#define WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(a,t) WDF_OBJECT_ATTRIBUTES_INIT(a)
#define WDF_NO_OBJECT_ATTRIBUTES NULL
#define WDF_PNPPOWER_EVENT_CALLBACKS_INIT(c) memset(c,0,sizeof(*(c)))
#define WdfDeviceInitSetPnpPowerEventCallbacks(i,c) ((void)(i),(void)(c))
#define UCM_MANAGER_CONFIG_INIT(c) (*(c)=1)
#define WDF_INTERRUPT_CONFIG_INIT(c,i,d) do { memset(c,0,sizeof(*(c)));(c)->isr=(i);(c)->dpc=(int)(intptr_t)(d); } while(0)
#define WDF_TIMER_CONFIG_INIT(c,f) (*(c)=(WDF_TIMER_CONFIG){f,TRUE})
#define WDF_WORKITEM_CONFIG_INIT(c,f) (*(c)=(WDF_WORKITEM_CONFIG){f,TRUE})
static int calls, fail_at, timers, workers, handles[16];
static NTSTATUS create(void **out) {
    ++calls;
    if(calls==fail_at) return INJECTED;
    *out=&handles[calls];return 0;
}
static NTSTATUS WdfDeviceCreate(PWDFDEVICE_INIT *i,WDF_OBJECT_ATTRIBUTES *a,WDFDEVICE *d) {
    (void)i;assert(a->ExecutionLevel==WdfExecutionLevelPassive);return create(d);
}
static PB2L_MAX_CONTEXT B2lMaxGetContext(WDFDEVICE d) { (void)d;return &context; }
static NTSTATUS UcmInitializeDevice(WDFDEVICE d,UCM_MANAGER_CONFIG *c) {
    void *out;assert(d==context.Device && *c==1);return create(&out);
}
static void KeInitializeEvent(int *e,int t,int v) { (void)t;*e=v; }
static NTSTATUS WdfInterruptCreate(WDFDEVICE d,WDF_INTERRUPT_CONFIG *c,WDF_OBJECT_ATTRIBUTES *a,void **out) {
    assert(d==context.Device && !a && !c->AutomaticSerialization);
    if(c->PassiveHandling) assert(c->WaitLock==context.MfdLock && !c->dpc);
    return create(out);
}
static NTSTATUS WdfWaitLockCreate(WDF_OBJECT_ATTRIBUTES *a,void **out) {
    assert(a->ParentObject==context.Device && a->ExecutionLevel==1);return create(out);
}
static NTSTATUS WdfTimerCreate(WDF_TIMER_CONFIG *c,WDF_OBJECT_ATTRIBUTES *a,void **out) {
    assert(!c->AutomaticSerialization && a->ParentObject==context.Device);
    assert(a->ExecutionLevel==WdfExecutionLevelPassive);++timers;return create(out);
}
static NTSTATUS WdfWorkItemCreate(WDF_WORKITEM_CONFIG *c,WDF_OBJECT_ATTRIBUTES *a,void **out) {
    assert(!c->AutomaticSerialization && a->ParentObject==context.Device);
    assert(c->callback==(workers ? B2lMaxEvtConnectorWork : B2lMaxEvtPolicyWork));
    ++workers;
    if(a->ExecutionLevel!=WdfExecutionLevelInheritFromParent) return INVALID_LEVEL;
    assert(a->SynchronizationScope==WdfSynchronizationScopeInheritFromParent);
    return create(out);
}
static void reset(void) { memset(&context,0,sizeof(context));calls=fail_at=timers=workers=0; }
'''
h += function + '\n' + legacy + r'''
int main(void) {
    reset();assert(LegacyDeviceAdd(NULL,NULL)==INVALID_LEVEL);
    assert(timers==2 && workers==1 && !context.PolicyWork && !context.ConnectorWork);
    reset();assert(B2lMaxEvtDeviceAdd(NULL,NULL)==0);
    assert(timers==2 && workers==2 && context.PolicyWork && context.ConnectorWork);
    int total=calls;
    for(int i=1;i<=total;++i) {
        reset();fail_at=i;assert(B2lMaxEvtDeviceAdd(NULL,NULL)==INJECTED);
        assert(calls==i); // creation must stop at the original failure
    }
    puts("MAX DeviceAdd: .42 invalid-level reproduction, separate timer/work-item attributes, all failure exits: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-device-add-') as temp:
    c, exe = Path(temp)/'add.c', Path(temp)/'add'
    c.write_text(h)
    subprocess.run(shlex.split(os.environ.get('CC','cc')) +
                   ['-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
