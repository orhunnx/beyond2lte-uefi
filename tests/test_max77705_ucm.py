#!/usr/bin/env python3
"""Execute the actual UCM notification queue with adversarial callback ordering.
Host fixture only: it cannot prove the real UcmCx/Urscx contract or USB I/O.
"""
from pathlib import Path
import re, subprocess, tempfile, os, shlex
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'windows/b2lusb/b2lmax77705_ucm.c').read_text().split('\n',1)[1]
h=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UCHAR; typedef uint32_t ULONG; typedef int32_t NTSTATUS;
typedef int BOOLEAN; typedef void VOID;
typedef void *WDFDEVICE,*WDFOBJECT,*WDFWORKITEM,*WDFWAITLOCK,*UCMCONNECTOR;
typedef int UCM_DATA_ROLE,UCM_TYPEC_CURRENT,UCM_POWER_ROLE;
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s)>=0)
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY (-1)
#define STATUS_NOT_SUPPORTED (-2)
#define STATUS_BUFFER_OVERFLOW (-3)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define B2L_MAX_DATA_DFP 1
#define B2L_MAX_DATA_UFP 0
#define B2L_MAX_CC_SINK 1
#define B2L_MAX_CC_SOURCE 2
#define B2L_MAX_CONNECTOR_EVENTS 64
#define UcmDataRoleInvalid 0
#define UcmDataRoleDfp 1
#define UcmDataRoleUfp 2
#define UcmTypeCPartnerUfp 1
#define UcmTypeCPartnerDfp 2
#define UcmTypeCCurrentInvalid 0
#define UcmTypeCCurrentDefaultUsb 1
#define UcmTypeCCurrent1500mA 2
#define UcmTypeCCurrent3000mA 4
#define UcmPowerRoleSink 1
#define UcmPowerRoleSource 2
#define UcmTypeCOperatingModeDrp 4
'''
header=(ROOT/'windows/b2lusb/b2lmax77705.h').read_text()
a=header.index('typedef struct _B2L_MAX_CONNECTOR_EVENT');b=header.index('// Requests belong',a)
h+=header[a:b]
h+=r'''
typedef struct {
 WDFDEVICE Device; UCMCONNECTOR Connector; WDFWAITLOCK MfdLock;
 WDFWORKITEM ConnectorWork;
 B2L_MAX_CONNECTOR_EVENT ConnectorEvents[64],ConnectorLast,ConnectorReported;
 BOOLEAN ConnectorRunning,ConnectorAttached,ConnectorOverflow,DataRoleValid,PowerRoleValid,WaterDetected,DiagnosticDirty;
 UCHAR DataRole,PowerRole,CcStatus0;
 ULONG ConnectorRead,ConnectorCount,ConnectorQueued,ConnectorAttempts;
 NTSTATUS ConnectorStatus,ConnectorSwapStatus,MfdFault;
} B2L_MAX_CONTEXT,*PB2L_MAX_CONTEXT;
typedef struct { int SupportedOperatingModes,SupportedPowerSourcingCapabilities; NTSTATUS (*EvtSetDataRole)(UCMCONNECTOR,UCM_DATA_ROLE); } UCM_CONNECTOR_TYPEC_CONFIG;
typedef struct { uint64_t ConnectorId; UCM_CONNECTOR_TYPEC_CONFIG *TypeCConfig; void *PdConfig; } UCM_CONNECTOR_CONFIG;
typedef struct { WDFDEVICE ParentObject; } WDF_OBJECT_ATTRIBUTES;
typedef struct { int Partner; UCM_TYPEC_CURRENT CurrentAdvertisement; int ChargingState; } UCM_CONNECTOR_TYPEC_ATTACH_PARAMS;
#define UCM_CONNECTOR_TYPEC_CONFIG_INIT(c,m,p) (memset(c,0,sizeof(*(c))),(c)->SupportedOperatingModes=(m),(c)->SupportedPowerSourcingCapabilities=(p))
#define UCM_CONNECTOR_CONFIG_INIT(c,i) (memset(c,0,sizeof(*(c))),(c)->ConnectorId=(i))
#define WDF_OBJECT_ATTRIBUTES_INIT(a) memset(a,0,sizeof(*(a)))
#define UCM_CONNECTOR_TYPEC_ATTACH_PARAMS_INIT(p,t) (memset(p,0,sizeof(*(p))),(p)->Partner=(t))
static B2L_MAX_CONTEXT c; static int held, enqueues, flushes;
static char calls[1024]; static int n, partners[512],currents[512],role;
static NTSTATUS attach_failure,detach_failure,current_failure;
static void WdfWaitLockAcquire(WDFWAITLOCK l,void *t) { assert(l==&c && !held && !t);held=1; }
static void WdfWaitLockRelease(WDFWAITLOCK l) { assert(l==&c && held);held=0; }
static void WdfWorkItemEnqueue(WDFWORKITEM w) { assert(w==&c);++enqueues; }
static void WdfWorkItemFlush(WDFWORKITEM w) { assert(w==&c && !held);++flushes; }
static WDFOBJECT WdfObjectGetParentObject(WDFOBJECT o) { assert(o==&c);return &c; }
static WDFOBJECT WdfWorkItemGetParentObject(WDFWORKITEM o) { return WdfObjectGetParentObject(o); }
static PB2L_MAX_CONTEXT B2lMaxGetContext(WDFDEVICE d) { assert(d==&c); return &c; }
static void record(char v) { assert(!held && n<1024);calls[n++]=v; }
static NTSTATUS UcmConnectorCreate(WDFDEVICE d,UCM_CONNECTOR_CONFIG *cfg,WDF_OBJECT_ATTRIBUTES *a,UCMCONNECTOR *out) {
 assert(d==&c && cfg->ConnectorId==0 && cfg->TypeCConfig->EvtSetDataRole && !cfg->PdConfig && a->ParentObject==d);
 *out=d;return 0;
}
static NTSTATUS UcmConnectorTypeCAttach(UCMCONNECTOR o,UCM_CONNECTOR_TYPEC_ATTACH_PARAMS *p) {
 assert(o==&c && p->ChargingState==0); partners[n]=p->Partner;currents[n]=p->CurrentAdvertisement;record('A');return attach_failure;
}
static NTSTATUS UcmConnectorTypeCDetach(UCMCONNECTOR o) { assert(o==&c);record('D');return detach_failure; }
static void UcmConnectorDataDirectionChanged(UCMCONNECTOR o,BOOLEAN success,UCM_DATA_ROLE r) {
 assert(o==&c && success);role=r;record('R');
}
static void UcmConnectorPowerDirectionChanged(UCMCONNECTOR o,BOOLEAN success,UCM_POWER_ROLE r) {
 assert(o==&c && success);assert(r==c.ConnectorEvents[0].PowerRole || r==1 || r==2);record('P');
}
static NTSTATUS UcmConnectorTypeCCurrentAdChanged(UCMCONNECTOR o,UCM_TYPEC_CURRENT cur) {
 assert(o==&c); currents[n]=cur;record('C');return current_failure;
}
'''
h+=s
h+=r'''
static void reset(void) {
 memset(&c,0,sizeof(c));memset(calls,0,sizeof(calls));n=held=enqueues=flushes=0;
 attach_failure=detach_failure=current_failure=0;
 c.Device=c.Connector=c.MfdLock=c.ConnectorWork=&c;c.ConnectorRunning=1;
 assert(B2lMaxCreateConnector(&c)==0);
}
static void queue(int connected,UCHAR data,UCHAR power,UCHAR cc) {
 c.PowerRoleValid=c.DataRoleValid=connected;c.DataRole=data;c.PowerRole=power;c.CcStatus0=cc;
 WdfWaitLockAcquire(c.MfdLock,NULL);B2lMaxQueueConnectorLocked(&c);WdfWaitLockRelease(c.MfdLock);
}
int main(void) {
 reset();queue(0,0,0,0);assert(!enqueues); // no fabricated boot HOST
 queue(1,1,2,2);B2lMaxEvtConnectorWork(&c);
 assert(c.ConnectorAttached && strcmp(calls,"AP")==0 && partners[0]==UcmTypeCPartnerUfp);
 assert(currents[0]==UcmTypeCCurrentInvalid); // no invented source Rp value
 queue(1,1,2,2);assert(c.ConnectorQueued==1); // duplicate suppressed
 reset();queue(1,0,1,0x21);queue(0,0,0,0);queue(1,1,2,2);B2lMaxEvtConnectorWork(&c);
 assert(strcmp(calls,"APDAP")==0 && partners[0]==UcmTypeCPartnerDfp && currents[0]==2);
 assert(c.ConnectorQueued==3 && c.ConnectorAttempts==3 && c.ConnectorAttached);
 queue(1,0,2,2);B2lMaxEvtConnectorWork(&c);assert(calls[n-1]=='R' && role==UcmDataRoleUfp);
 queue(1,0,1,0x31);B2lMaxEvtConnectorWork(&c);assert(calls[n-2]=='C' && calls[n-1]=='P');
 assert(B2lMaxEvtSetDataRole(&c,UcmDataRoleUfp)==0 && calls[n-1]=='R');
 {int before=n;assert(B2lMaxEvtSetDataRole(&c,UcmDataRoleDfp)==STATUS_NOT_SUPPORTED && n==before);}
 c.WaterDetected=1;assert(B2lMaxEvtSetDataRole(&c,UcmDataRoleUfp)==STATUS_DEVICE_NOT_READY);
 reset();queue(1,1,2,2);B2lMaxEvtConnectorWork(&c);current_failure=-44;
 queue(1,0,1,0x31);B2lMaxEvtConnectorWork(&c);
 assert(c.ConnectorStatus==-44 && c.ConnectorReported.DataRole==0 && c.ConnectorReported.PowerRole==1);
 assert(calls[n-3]=='R' && calls[n-2]=='C' && calls[n-1]=='P');
 reset();attach_failure=-42;queue(1,0,1,0x11);B2lMaxEvtConnectorWork(&c);
 assert(!c.ConnectorAttached && c.ConnectorStatus==-42 && strcmp(calls,"A")==0);
 reset();queue(1,1,2,2);B2lMaxEvtConnectorWork(&c);detach_failure=-43;
 queue(0,0,0,0);queue(1,0,1,0x21);B2lMaxEvtConnectorWork(&c);
 assert(strcmp(calls,"APD")==0 && c.ConnectorOverflow && c.ConnectorStatus==-43 && !c.ConnectorCount);
 reset();queue(1,1,2,2);B2lMaxEvtConnectorWork(&c);
 for(int i=0;i<66;i++)queue(i%2,0,1,0x11);
 B2lMaxEvtConnectorWork(&c);assert(c.ConnectorOverflow && !c.ConnectorAttached && calls[n-1]=='D');
 assert(c.ConnectorStatus==STATUS_BUFFER_OVERFLOW && !c.ConnectorCount);
 reset();queue(1,1,2,2);B2lMaxEvtConnectorWork(&c);B2lMaxStopConnector(&c);
 assert(!c.ConnectorRunning && !c.ConnectorAttached && flushes==1 && calls[n-1]=='D');
 queue(1,0,1,0x11);assert(!c.ConnectorCount);
 puts("MAX77705 UCM ordering, detach barriers, errors and honest swap results: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-ucm-') as d:
 p=Path(d);(p/'test.c').write_text(h)
 subprocess.run(shlex.split(os.getenv('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
