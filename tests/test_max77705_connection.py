#!/usr/bin/env python3
"""Run actual CC decode and connection policy with scripted transport/actions."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'windows/b2lusb/b2lmax77705.c').read_text()
header = (ROOT/'windows/b2lusb/b2lmax77705.h').read_text()

def function(name):
    start = source.rfind('\nstatic ', 0, source.index(name+'(')) + 1
    brace = source.index('{', source.index(name+'(', start))
    depth, end = 1, brace+1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}'); end += 1
    return source[start:end]

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t UCHAR;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef int32_t NTSTATUS;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY (-100)
#define STATUS_NOT_SUPPORTED (-101)
#define NT_SUCCESS(x) ((x)>=0)
#define RTL_NUMBER_OF(x) (sizeof(x)/sizeof(x[0]))
typedef struct {
    UCHAR CcStatus0, CcStatus1, CcState, PreviousCcState, CcInterrupt, MuicControl1;
    ULONGLONG CcLastSampleTime, CcLastTransitionTime;
    ULONG CcSampleCount, CcSourceEventCount, CcTransitionCount;
    int CcStatusValid, CcStateChanged, DiagnosticDirty;
    int OtgEnabled, OtgHardwareActive, MuicPathVerified;
    int PowerRoleValid, DataRoleValid, WaterDetected, MuicRprd;
    ULONG MuicType;
    UCHAR PowerRole, DataRole, PdStatus1, ChargerDetails[3];
    ULONG DataRoleEvents, DataRoleChanges, OvercurrentEvents;
    NTSTATUS DataRoleStatus;
} CONTEXT, *PB2L_MAX_CONTEXT;
static UCHAR raw[2], pd_raw, byp_raw;
static NTSTATUS read_error, power_error, muic_error;
static char actions[32]; static size_t action_count;
static ULONGLONG KeQueryInterruptTime(void) { return 123; }
static void action(char c) { assert(action_count+1<sizeof(actions)); actions[action_count++]=c; }
static NTSTATUS B2lMaxReadBlock(PB2L_MAX_CONTEXT c,UCHAR addr,UCHAR reg,UCHAR *data,ULONG len) {
    (void)c; assert(addr==0x25 && reg==0x0a && len==2); action('R');
    if (read_error) return read_error;
    memcpy(data,raw,2); return 0;
}
static NTSTATUS B2lMaxReadByte(PB2L_MAX_CONTEXT c,UCHAR addr,UCHAR reg,UCHAR *v) {
    (void)c;
    if(addr==0x25 && reg==0x0d) {action('P');*v=pd_raw;}
    else if(addr==0x25 && reg==0x0b) {action('W');*v=raw[1];}
    else {assert(addr==0x69 && reg==0xb5);action('B');*v=byp_raw;}
    return read_error;
}
static NTSTATUS B2lMaxUnlockCharger(PB2L_MAX_CONTEXT c) { (void)c;action('U');return 0; }
static void B2lMaxSetDiagnosticLocked(PB2L_MAX_CONTEXT c, ULONG stage, NTSTATUS s) {
    (void)c; (void)stage; (void)s;
}
static NTSTATUS B2lMaxEnableOtg(PB2L_MAX_CONTEXT c) {
    action('V'); if(power_error) return power_error;
    c->OtgEnabled=c->OtgHardwareActive=1; return 0;
}
static NTSTATUS B2lMaxDisableOtg(PB2L_MAX_CONTEXT c) {
    action('D'); if(power_error) return power_error;
    c->OtgEnabled=c->OtgHardwareActive=0; return 0;
}
static NTSTATUS B2lMaxRequestMuicLocked(PB2L_MAX_CONTEXT c,UCHAR path) {
    assert(path==9 || path==0x3f); action(path==9?'M':'O');
    if(muic_error) return muic_error;
    c->MuicControl1=path; c->MuicPathVerified=1; return 0;
}
'''
harness += '\n'.join(re.findall(r'^#define MAX77705_.*', source.replace('\\\n', ' '), re.M))+'\n'
harness += '\n'.join(re.findall(r'^#define B2L_MAX_.*', header, re.M))+'\n'
harness += r'''
// MUIC classification/real opcode queue runs in test_max77705_irq.py.
// This fixture isolates CC/PD notifications and failure propagation.
static NTSTATUS B2lMaxDetectMuicLocked(PB2L_MAX_CONTEXT c, ULONG cause) {
    assert(cause==B2L_MAX_MUIC_CC_NOTIFY);
    return B2lMaxRequestMuicLocked(c,9);
}
'''
harness += function('B2lMaxReadCcStatus')+'\n'
for name in ('B2lMaxEvaluateConnection','B2lMaxReadWaterState','B2lMaxHandleDataRole','B2lMaxHandleBypass'):
    harness += function(name)+'\n'
harness += r'''
static void reset(void) {
    memset(actions,0,sizeof(actions)); action_count=0;
    read_error=power_error=muic_error=0;
}
int main(void) {
    CONTEXT c; UCHAR state;
    for(state=0;state<8;++state) {
        if(state==2) continue;
        reset(); memset(&c,0,sizeof(c)); raw[0]=(UCHAR)(0x80|state); raw[1]=0;
        assert(B2lMaxEvaluateConnection(&c,FALSE)==0);
        assert(strcmp(actions,state==1 ? "R" : "RO")==0 && c.CcState==state);
        assert(c.CcSourceEventCount==0 && !c.OtgEnabled);
    }
    reset(); memset(&c,0,sizeof(c)); raw[0]=0x42; pd_raw=0x80;
    assert(B2lMaxEvaluateConnection(&c,TRUE)==0 && strcmp(actions,"R")==0);
    assert(!c.PowerRoleValid && c.CcSourceEventCount==0);
    reset(); assert(B2lMaxHandleDataRole(&c,TRUE)==0 && strcmp(actions,"P")==0);
    assert(!c.DataRoleValid); // initial SOURCE is not promoted to DFP
    reset(); assert(B2lMaxHandleDataRole(&c,FALSE)==0 && strcmp(actions,"P")==0);
    assert(!c.DataRoleValid); // no CC power-role event yet
    reset(); assert(B2lMaxEvaluateConnection(&c,FALSE)==0 && strcmp(actions,"RV")==0);
    assert(c.CcSourceEventCount==1 && c.OtgEnabled && !c.MuicPathVerified);
    reset(); assert(B2lMaxHandleDataRole(&c,FALSE)==0 && strcmp(actions,"PM")==0);
    assert(c.DataRole==1 && c.DataRoleValid && c.DataRoleEvents==1 && c.DataRoleChanges==1);
    reset(); assert(B2lMaxHandleDataRole(&c,FALSE)==0 && strcmp(actions,"P")==0);
    assert(c.DataRoleChanges==1); // stable DFP
    reset(); raw[0]=0x41;assert(B2lMaxEvaluateConnection(&c,FALSE)==0);
    assert(strcmp(actions,"RD")==0 && c.PowerRole==1 && c.DataRole==1 && c.MuicControl1==9);
    reset(); assert(B2lMaxHandleDataRole(&c,FALSE)==0 && strcmp(actions,"P")==0); // SINK+DFP valid
    reset(); raw[0]=0x80;assert(B2lMaxEvaluateConnection(&c,FALSE)==0);
    assert(strcmp(actions,"RO")==0 && !c.PowerRoleValid && !c.DataRoleValid);
    reset();raw[0]=0x42;assert(B2lMaxEvaluateConnection(&c,FALSE)==0);
    reset();raw[0]=0;assert(B2lMaxEvaluateConnection(&c,FALSE)==0 && strcmp(actions,"RD")==0);
    // OPEN is already verified here; power goes off without redundant opcode.
    reset();memset(&c,0,sizeof(c));raw[0]=0x42;power_error=-10;
    assert(B2lMaxEvaluateConnection(&c,FALSE)==-10 && strcmp(actions,"RV")==0 && !c.PowerRoleValid);
    reset();assert(B2lMaxEvaluateConnection(&c,FALSE)==0);
    reset();c.DataRoleValid=0;muic_error=-20;assert(B2lMaxHandleDataRole(&c,FALSE)==-20 && strcmp(actions,"PM")==0);
    reset();assert(B2lMaxHandleDataRole(&c,FALSE)==-20 && strcmp(actions,"P")==0);
    c.DataRoleValid=0;
    assert(B2lMaxHandleDataRole(&c,FALSE)==0);
    reset();read_error=-30;assert(B2lMaxHandleDataRole(&c,FALSE)==-30 && strcmp(actions,"P")==0);
    assert(c.DataRoleStatus==-30);
    reset();raw[1]=2;assert(B2lMaxReadWaterState(&c)==0 && c.WaterDetected);
    reset();c.DataRoleValid=0;assert(B2lMaxHandleDataRole(&c,FALSE)==STATUS_DEVICE_NOT_READY && strcmp(actions,"P")==0);
    reset();raw[1]=0;assert(B2lMaxReadWaterState(&c)==0 && !c.WaterDetected);
    // A UFP observation cannot masquerade as a supported host/function path.
    reset();pd_raw=0;assert(B2lMaxHandleDataRole(&c,FALSE)==STATUS_NOT_SUPPORTED);
    assert(strcmp(actions,"PM")==0 && c.DataRole==0 && c.DataRoleValid);
    assert(c.OtgEnabled); // data-role swap does not silently change source power
    reset();byp_raw=1;assert(B2lMaxHandleBypass(&c)==0 && strcmp(actions,"UBD")==0);
    assert(!c.OtgEnabled && c.OvercurrentEvents==1 && c.PowerRole==2);
    reset();assert(B2lMaxEvaluateConnection(&c,FALSE)==0 && strcmp(actions,"R")==0);
    // Same SRC IRQ must not restart OTG after BYP overcurrent (Linux current_pr).
    reset();raw[0]=0;assert(B2lMaxEvaluateConnection(&c,FALSE)==0);
    reset();raw[0]=0x42;assert(B2lMaxEvaluateConnection(&c,FALSE)==0 && strcmp(actions,"RV")==0);
    reset();read_error=-40;assert(B2lMaxHandleBypass(&c)==-40 && strcmp(actions,"UB")==0 && c.OtgEnabled);
    reset();read_error=-30;assert(B2lMaxEvaluateConnection(&c,FALSE)==-30 && strcmp(actions,"R")==0);
    puts("MAX77705 CC/PD power-data roles, water and BYP tests: PASS");
}

'''
with tempfile.TemporaryDirectory(prefix='b2l-connection-test-') as directory:
    temp=Path(directory); (temp/'test.c').write_text(harness)
    compiler=shlex.split(os.environ.get('CC','cc'))
    subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror',str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
