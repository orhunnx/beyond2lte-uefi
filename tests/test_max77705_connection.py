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
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define NT_SUCCESS(x) ((x)>=0)
#define RTL_NUMBER_OF(x) (sizeof(x)/sizeof(x[0]))
typedef struct {
    UCHAR CcStatus0, CcStatus1, CcState, PreviousCcState, CcInterrupt, MuicControl1;
    ULONGLONG CcLastSampleTime, CcLastTransitionTime;
    ULONG CcSampleCount, CcSourceEventCount, CcTransitionCount;
    int CcStatusValid, CcStateChanged, DiagnosticDirty;
    int OtgEnabled, OtgHardwareActive, MuicPathVerified;
} CONTEXT, *PB2L_MAX_CONTEXT;
static UCHAR raw[2];
static NTSTATUS read_error, power_error, muic_error;
static char actions[32]; static size_t action_count;
static ULONGLONG KeQueryInterruptTime(void) { return 123; }
static void action(char c) { assert(action_count+1<sizeof(actions)); actions[action_count++]=c; }
static NTSTATUS B2lMaxReadBlock(PB2L_MAX_CONTEXT c,UCHAR addr,UCHAR reg,UCHAR *data,ULONG len) {
    (void)c; assert(addr==0x25 && reg==0x0a && len==2); action('R');
    if (read_error) return read_error;
    memcpy(data,raw,2); return 0;
}
static void B2lMaxPublishDiagnostic(PB2L_MAX_CONTEXT c, ULONG stage, NTSTATUS s) {
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
static NTSTATUS B2lMaxSetMuicPath(PB2L_MAX_CONTEXT c,UCHAR path) {
    assert(path==9 || path==0x3f); action(path==9?'M':'O');
    if(muic_error) return muic_error;
    c->MuicControl1=path; c->MuicPathVerified=1; return 0;
}
'''
harness += '\n'.join(re.findall(r'^#define MAX77705_.*', source.replace('\\\n', ' '), re.M))+'\n'
harness += '\n'.join(re.findall(r'^#define B2L_MAX_.*', header, re.M))+'\n'
harness += function('B2lMaxReadCcStatus')+'\n'
harness += 'static NTSTATUS B2lMaxReadStatus(PB2L_MAX_CONTEXT c) { return B2lMaxReadCcStatus(c); }\n'
harness += function('B2lMaxEvaluateConnection')+'\n'
harness += r'''
static void reset(void) {
    memset(actions,0,sizeof(actions)); action_count=0;
    read_error=power_error=muic_error=0;
}
int main(void) {
    CONTEXT c; UCHAR state;
    // Pin activity is not a source role. Include every non-source role.
    for(state=0;state<8;++state) {
        if(state==2) continue;
        reset(); memset(&c,0,sizeof(c)); raw[0]=(UCHAR)(0x80|state); raw[1]=0;
        assert(B2lMaxEvaluateConnection(&c)==0);
        assert(strcmp(actions,"RO")==0 && c.CcState==state);
        assert(c.CcSourceEventCount==0 && !c.OtgEnabled);
    }
    reset(); memset(&c,0,sizeof(c)); raw[0]=0x42;
    assert(B2lMaxEvaluateConnection(&c)==0);
    assert(strcmp(actions,"RVM")==0); // Linux: CC -> VBUS -> MUIC
    assert(c.CcSourceEventCount==1 && c.OtgEnabled && c.MuicControl1==9);
    reset(); assert(B2lMaxEvaluateConnection(&c)==0);
    assert(strcmp(actions,"R")==0); // stable source is idempotent
    reset(); raw[0]=0x80;
    assert(B2lMaxEvaluateConnection(&c)==0);
    assert(strcmp(actions,"RDO")==0); // source lost: power off before OPEN
    assert(c.CcTransitionCount==1 && !c.OtgEnabled && c.MuicControl1==0x3f);
    reset(); raw[0]=0x42; power_error=-10;
    assert(B2lMaxEvaluateConnection(&c)==-10 && strcmp(actions,"RV")==0);
    reset(); muic_error=-20;
    assert(B2lMaxEvaluateConnection(&c)==-20 && strcmp(actions,"RVM")==0);
    reset(); assert(B2lMaxEvaluateConnection(&c)==0);
    assert(strcmp(actions,"RM")==0); // retry failed MUIC without a second power-on
    reset(); read_error=-30;
    assert(B2lMaxEvaluateConnection(&c)==-30 && strcmp(actions,"R")==0);
    // A failed status read must not activate an inherited/stale SOURCE sample.
    reset(); c.OtgEnabled=0; read_error=-30;
    assert(B2lMaxEvaluateConnection(&c)==-30 && strcmp(actions,"R")==0);
    puts("MAX77705 CC/source/VBUS/MUIC ordering tests: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-connection-test-') as directory:
    temp=Path(directory); (temp/'test.c').write_text(harness)
    compiler=shlex.split(os.environ.get('CC','cc'))
    subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror',str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
