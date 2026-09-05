#!/usr/bin/env python3
"""Exercise production parent PnP resource mapping, rejection and unwind.

Host fixture only. No WDK build or physical MMIO access.
"""
from pathlib import Path
import os, re, shlex, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
WIN = ROOT/'windows/b2lusb'
source = (WIN/'b2lusb.c').read_text()
header = (WIN/'b2lusb.h').read_text()
def function(name):
    pos = source.index('\n'+name+'(')+1
    start = source.rfind('\n\n', 0, pos)+2
    brace = source.index('{', pos); end = brace+1; depth = 1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}'); end += 1
    return source[start:end]
context = header[header.index('typedef struct _B2L_USB_CONTEXT'):header.index('WDF_DECLARE_CONTEXT_TYPE')]
defines = '\n'.join(re.findall(r'^#define B2L_.*$', header, re.M))
h = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef void VOID;
typedef uint32_t ULONG,*PULONG;
typedef uint8_t *PUCHAR;
typedef uint64_t ULONGLONG;
typedef int NTSTATUS,BOOLEAN,B2L_USB_ROLE;
typedef void *WDFDEVICE,*WDFWAITLOCK,*WDFSPINLOCK,*WDFTIMER;
#define NULL_DEVICE 0
#define FALSE 0
#define TRUE 1
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_CONFIGURATION_ERROR (-1)
#define STATUS_INSUFFICIENT_RESOURCES (-2)
#define PAGE_READWRITE 1
#define PAGE_NOCACHE 2
#define CmResourceTypeMemory 1
#define CmResourceTypeDevicePrivate 2
#define B2lUsbRoleNone 0
#define NT_SUCCESS(s) ((s)>=0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
typedef struct { int64_t QuadPart; } PHYSICAL_ADDRESS;
typedef struct { ULONG Type; union { struct { PHYSICAL_ADDRESS Start; ULONG Length; } Memory; } u; } RESOURCE,*PCM_PARTIAL_RESOURCE_DESCRIPTOR;
typedef struct { ULONG count; RESOURCE list[10]; } RESOURCES,*WDFCMRESLIST;
'''+defines+'\n'+context+r'''
static B2L_USB_CONTEXT c;
static unsigned mapped,freed,fail_at;
static unsigned char memory[7][0x10000];
static void *active[7];
static const uint64_t bases[]={0x10c00000,0x10b00000,0x1586072c,0x15862200,0x10a00000,0x13000120,0x13020104};
static const ULONG lengths[]={0x10000,0x200,4,8,0x8000,4,4};
static void *MmMapIoSpaceEx(PHYSICAL_ADDRESS p,ULONG n,ULONG flags) {
    unsigned i;assert(flags==(PAGE_READWRITE|PAGE_NOCACHE));
    for(i=0;i<7;i++) if(bases[i]==(uint64_t)p.QuadPart) break;
    assert(i<7 && n==lengths[i] && !active[i]);
    ++mapped;if(fail_at && mapped==fail_at) return NULL;
    active[i]=memory[i];return active[i];
}
static void MmUnmapIoSpace(void *p,ULONG n) {
    unsigned i;for(i=0;i<7;i++) if(active[i]==p) break;
    assert(i<7 && n==lengths[i]);active[i]=NULL;++freed;
}
static PB2L_USB_CONTEXT B2lUsbGetContext(WDFDEVICE d) { assert(d==&c);return d; }
static ULONG WdfCmResourceListGetCount(WDFCMRESLIST r) {return r->count;}
static PCM_PARTIAL_RESOURCE_DESCRIPTOR WdfCmResourceListGetDescriptor(WDFCMRESLIST r,ULONG i) {assert(i<r->count);return &r->list[i];}
static void UrsSetHardwareEventSupport(WDFDEVICE d,BOOLEAN enabled) {assert(d==&c && !enabled);}
'''
for n in ['B2lUsbMapResource','B2lUsbUnmapResources','B2lUsbEvtPrepareHardware']: h+='\n'+function(n)
h+=r'''
static void init(RESOURCES *r) {
    unsigned i;memset(&c,0,sizeof(c));memset(r,0,sizeof(*r));
    memset(active,0,sizeof(active));mapped=freed=fail_at=0;r->count=7;
    for(i=0;i<7;i++) {r->list[i].Type=1;r->list[i].u.Memory.Start.QuadPart=bases[i];r->list[i].u.Memory.Length=lengths[i];}
}
static void empty(void) {
    unsigned i;for(i=0;i<7;i++) assert(!active[i]);
    assert(!c.Dwc3Registers && !c.PhyRegisters && !c.PmuUsbPhy && !c.Fsys0aPmu && !c.Fsys0aCmu && !c.Fsys0Bus && !c.Fsys0Drcg);
}
int main(void) {
    RESOURCES r;unsigned i;
    init(&r);assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==0 && mapped==7 && c.HardwareReady);
    assert(c.Dwc3Registers==memory[0] && c.PhyRegisters==memory[1]);
    assert(c.PmuUsbPhy==(PULONG)memory[2] && c.Fsys0aPmu==memory[3] && c.Fsys0aCmu==memory[4]);
    assert(c.Fsys0Bus==(PULONG)memory[5] && c.Fsys0Drcg==(PULONG)memory[6]);
    B2lUsbUnmapResources(&c);assert(freed==7);empty();
    for(i=1;i<=7;i++) {
        init(&r);fail_at=i;
        assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==STATUS_INSUFFICIENT_RESOURCES && freed==i-1);empty();
    }
    for(i=0;i<7;i++) {
        init(&r);r.list[i].u.Memory.Length++;
        assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==STATUS_DEVICE_CONFIGURATION_ERROR);empty();
        init(&r);r.list[i].u.Memory.Start.QuadPart+=4;
        assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==STATUS_DEVICE_CONFIGURATION_ERROR);empty();
    }
    init(&r);r.count=6;assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==STATUS_DEVICE_CONFIGURATION_ERROR);empty();
    init(&r);r.list[6]=r.list[0];assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==STATUS_DEVICE_CONFIGURATION_ERROR);empty();
    init(&r);r.list[7].Type=3;r.count=8;assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==STATUS_DEVICE_CONFIGURATION_ERROR);empty();
    init(&r);r.list[7].Type=CmResourceTypeDevicePrivate;r.count=8;
    assert(B2lUsbEvtPrepareHardware(&c,&r,&r)==0);B2lUsbUnmapResources(&c);empty();
    puts("USB parent exact PnP resources, no MMIO in prepare, rejection/unwind: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-usb-resources-') as directory:
    p=Path(directory);(p/'test.c').write_text(h)
    subprocess.run(shlex.split(os.getenv('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
