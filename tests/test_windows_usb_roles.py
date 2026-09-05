#!/usr/bin/env python3
"""Execute URS role/power callbacks; exercise target-WIM INF/runtime validation."""
from pathlib import Path
import subprocess, tempfile, os, shlex, importlib.util, struct, sys
sys.dont_write_bytecode = True
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'windows/b2lusb/b2lusb.c').read_text()
def function(name):
 pos=s.index('\n'+name+'(')+1;a=s.rfind('\n\n',0,pos)+2;b=s.index('{',pos);e=b+1;d=1
 while d:d+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
h=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef void *WDFDEVICE,*WDFWAITLOCK,*WDFSPINLOCK,*WDFTIMER;
typedef uint64_t ULONGLONG; typedef uint32_t ULONG;
typedef void VOID;
typedef int NTSTATUS,BOOLEAN,URS_ROLE,B2L_USB_ROLE,WDF_POWER_DEVICE_STATE;
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s)>=0)
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY (-1)
#define STATUS_INVALID_PARAMETER (-2)
#define STATUS_DEVICE_CONFIGURATION_ERROR (-3)
#define B2lUsbRoleNone 0
#define B2lUsbRoleHost 1
#define B2lUsbRoleDevice 2
#define UrsRoleNone 0
#define UrsRoleHost 1
#define UrsRoleFunction 2
#define B2L_XHCI_OBSERVE_ACTIVE 1
#define WdfPowerDeviceD3Final 9
#define B2L_DWC3_PHYSICAL_BASE 0x10c00000ULL
#define B2L_DWC3_LENGTH 0x10000UL
#define CmResourceTypeMemory 1
#define UNREFERENCED_PARAMETER(x) ((void)(x))
typedef struct {
 int ClocksEnabled,HardwareReady,RoleInitialized,RegistersValid,RequestedRole,CurrentRole,LastRoleStatus,XhciObserveFlags;
 WDFTIMER XhciObserveTimer; WDFWAITLOCK RoleLock; WDFSPINLOCK XhciObserveLock;
} B2L_USB_CONTEXT,*PB2L_USB_CONTEXT;
static B2L_USB_CONTEXT c;
static int locked,spin,stops,hardware_calls,last_hardware_role,hw_failure;
static PB2L_USB_CONTEXT B2lUsbGetContext(WDFDEVICE d) { assert(d==&c);return d; }
static void WdfTimerStop(WDFTIMER t,int wait) { (void)t;assert(wait);++stops; }
static void WdfWaitLockAcquire(WDFWAITLOCK l,void *t) {(void)l;assert(!locked && !t);locked=1;}
static void WdfWaitLockRelease(WDFWAITLOCK l) {(void)l;assert(locked);locked=0;}
static void WdfSpinLockAcquire(WDFSPINLOCK l) {(void)l;assert(locked && !spin);spin=1;}
static void WdfSpinLockRelease(WDFSPINLOCK l) {(void)l;assert(spin);spin=0;}
static NTSTATUS B2lUsbHardwareSetRole(PB2L_USB_CONTEXT p,B2L_USB_ROLE r) {
 assert(p==&c && locked && !spin && !(c.XhciObserveFlags&1) && !c.RegistersValid);
 ++hardware_calls;last_hardware_role=r;return hw_failure;
}
typedef struct { int Type; union { struct { struct { int64_t QuadPart; } MinimumAddress,MaximumAddress; ULONG Length; } Memory; } u; } IO_RESOURCE_DESCRIPTOR;
typedef IO_RESOURCE_DESCRIPTOR *PIO_RESOURCE_DESCRIPTOR;
typedef struct { ULONG Count; IO_RESOURCE_DESCRIPTOR entries[5]; } LIST,*WDFIORESLIST,*URSIORESLIST;
typedef struct { ULONG Count; LIST list; } REQS,*WDFIORESREQLIST;
static ULONG WdfIoResourceRequirementsListGetCount(WDFIORESREQLIST l) {return l->Count;}
static WDFIORESLIST WdfIoResourceRequirementsListGetIoResList(WDFIORESREQLIST l,ULONG i) {assert(!i);return &l->list;}
static ULONG WdfIoResourceListGetCount(WDFIORESLIST l) {return l->Count;}
static PIO_RESOURCE_DESCRIPTOR WdfIoResourceListGetDescriptor(WDFIORESLIST l,ULONG i) {return &l->entries[i];}
static NTSTATUS UrsIoResourceListAppendDescriptor(URSIORESLIST l,PIO_RESOURCE_DESCRIPTOR d) {assert(l->Count<5);l->entries[l->Count++]=*d;return 0;}
'''
# Callback ordering in the fixture follows actual prototypes, not source position.
for name in ['B2lUsbEvtSetRole','B2lUsbEvtD0Entry','B2lUsbEvtD0Exit','B2lUsbRequirementIsDwc3','B2lUsbEvtFilterResources']:h+='\n'+function(name)
h+=r'''
int main(void) {
 REQS req={0};LIST host={0},device={0};
 c.HardwareReady=1;c.XhciObserveFlags=1;
 assert(B2lUsbEvtD0Entry(&c,0)==0 && last_hardware_role==0 && hardware_calls==1 && !c.RegistersValid);
 assert(B2lUsbEvtSetRole(&c,UrsRoleHost)==0 && c.CurrentRole==1 && c.RegistersValid);
 assert(B2lUsbEvtD0Exit(&c,0)==0 && c.CurrentRole==0 && c.RequestedRole==1 && !c.RegistersValid);
 assert(B2lUsbEvtD0Entry(&c,0)==0 && c.CurrentRole==1 && c.RegistersValid);
 assert(B2lUsbEvtSetRole(&c,UrsRoleFunction)==0 && c.CurrentRole==2 && c.RegistersValid);
 hw_failure=-77;
 assert(B2lUsbEvtSetRole(&c,UrsRoleHost)==-77 && c.LastRoleStatus==-77 && !c.RegistersValid);
 assert(B2lUsbEvtSetRole(&c,3)==STATUS_INVALID_PARAMETER);
 hw_failure=0;assert(B2lUsbEvtSetRole(&c,UrsRoleHost)==0);
 hw_failure=-78;assert(B2lUsbEvtD0Exit(&c,0)==-78 && !c.RoleInitialized && !c.RegistersValid);
 {int before=hardware_calls;
  hw_failure=0;assert(B2lUsbEvtD0Entry(&c,0)==0 && hardware_calls==before+1 && c.RegistersValid);}
 hw_failure=0;assert(B2lUsbEvtSetRole(&c,UrsRoleNone)==0 && c.CurrentRole==0 && !c.RegistersValid);
 c.ClocksEnabled=1;{int before=hardware_calls;
  assert(B2lUsbEvtD0Exit(&c,0)==0 && hardware_calls==before+1);}
 assert(B2lUsbEvtSetRole(&c,UrsRoleHost)==0);
 assert(B2lUsbEvtD0Exit(&c,WdfPowerDeviceD3Final)==0 && c.RequestedRole==0 && c.CurrentRole==0);
 req.Count=1;req.list.Count=3;
 req.list.entries[0].Type=1;req.list.entries[0].u.Memory.MinimumAddress.QuadPart=B2L_DWC3_PHYSICAL_BASE;
 req.list.entries[0].u.Memory.MaximumAddress.QuadPart=B2L_DWC3_PHYSICAL_BASE+B2L_DWC3_LENGTH-1;
 req.list.entries[0].u.Memory.Length=B2L_DWC3_LENGTH;
 req.list.entries[1].Type=1;req.list.entries[1].u.Memory.MinimumAddress.QuadPart=0x10b00000;
 req.list.entries[2].Type=1;req.list.entries[2].u.Memory.MinimumAddress.QuadPart=0x1586072c;
 assert(B2lUsbEvtFilterResources(&c,&req,&host,&device)==0 && host.Count==1 && device.Count==1);
 assert(host.entries[0].u.Memory.Length==0x10000 && device.entries[0].u.Memory.Length==0x10000);
 req.list.entries[2]=req.list.entries[0];host.Count=device.Count=0;
 assert(B2lUsbEvtFilterResources(&c,&req,&host,&device)==STATUS_DEVICE_CONFIGURATION_ERROR);
 req.Count=2;assert(B2lUsbEvtFilterResources(&c,&req,&host,&device)==STATUS_DEVICE_CONFIGURATION_ERROR);
 puts("URS real role authority, resume, failure and resource ownership: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-urs-') as d:
 p=Path(d);(p/'test.c').write_text(h)
 subprocess.run(shlex.split(os.getenv('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
module=importlib.util.spec_from_file_location('function_inf',ROOT/'windows/b2lusb/make-function-inf.py')
m=importlib.util.module_from_spec(module);module.loader.exec_module(m)
inf='''[Version]
Class=USBFunctionController
ClassGuid={88BAE032-5A81-49f0-BC3D-A4FF138216D6}
[Manufacturer]
%ms%=Models,NTarm64
[Models.NTarm64]
%dev%=Synopsys,ACPI\\TEST0001
[Synopsys.NT]
CopyFiles=Core
[Synopsys.NT.Services]
AddService=ufxsynopsys,2,Service
[Synopsys.NT.HW]
AddReg=Hw
'''
for raw in [inf.encode(),inf.encode('utf-16')]:
 out=m.make_inf(raw);assert 'Needs=synopsys.nt.services' in out and 'URS\\BYN0002&FUNCTION' in out
# Windows section references are case-insensitive; aliases are one install.
case_alias = inf + '\n[Models.NTarm64]\n%alias%=SYNOPSYS,ACPI\\TEST0002\n'
assert 'Needs=synopsys.nt.services' in m.make_inf(case_alias.encode())
for bad, message in [(inf.replace('NTarm64','NTamd64'), 'no ARM64 DDInstall'),
                     (inf+'\n[Models.NTarm64]\n%other%=Other,ACPI\\TEST0002', 'multiple ARM64 DDInstall')]:
 try:m.make_inf(bad.encode())
 except ValueError as error:assert message in str(error)
 else:raise AssertionError('invalid models accepted')
for bad in [inf.replace('NTarm64','NTamd64'),inf.replace('[Synopsys.NT.Services]','[Missing]'),inf+'\n[Models.NTarm64]\n%other%=Other,ACPI\\TEST0002']:
 try:m.make_inf(bad.encode())
 except ValueError:pass
 else:raise AssertionError('accepted unsupported inbox layout')
def pe(build=28000,machine=0xaa64):
 raw=bytearray(512);raw[:2]=b'MZ';struct.pack_into('<I',raw,0x3c,64);raw[64:68]=b'PE\0\0'
 struct.pack_into('<HH',raw,68,machine,1);raw[88:96]=b'.rsrc\0\0\0';struct.pack_into('<II',raw,104,128,256)
 struct.pack_into('<IIII',raw,256,0xfeef04bd,0x10000,10<<16,build<<16)
 return bytes(raw)
assert m.arm64_build(pe())==(10,0,28000)
with tempfile.TemporaryDirectory() as d:
 paths=[Path(d)/'ntoskrnl.exe',Path(d)/'Urscx01000.sys'];[p.write_bytes(pe()) for p in paths];m.check_runtimes(paths)
 paths[1].write_bytes(pe(15063))
 try:m.check_runtimes(paths)
 except ValueError:pass
 else:raise AssertionError('foreign runtime accepted')
try:m.arm64_build(pe(machine=0x8664))
except ValueError:pass
else:raise AssertionError('x64 runtime accepted')
print('Target ARM64 UFX INF and same-build runtime checks: PASS')
