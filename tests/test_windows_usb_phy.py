#!/usr/bin/env python3
"""Execute production DWC3/PHY code against ordered, bounded MMIO fixtures.

Host-only test, not a Windows/firmware build or hardware measurement. The
fixture models CSFTRST self-clear and failure; it does not emulate USB traffic.
Golden write order follows Samsung 55f0426 core_soft_reset/USB3P1 v0x301.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WIN = ROOT / 'windows/b2lusb'
header = (WIN / 'b2lusb.h').read_text()
context = header[header.index('typedef struct _B2L_USB_CONTEXT'):header.index('WDF_DECLARE_CONTEXT_TYPE')]
public = (WIN / 'b2lusb_public.h').read_text().replace('#include <guiddef.h>', '')
public = re.sub(r'DEFINE_GUID\(.*?\);', '', public, flags=re.S)
source = '\n'.join((WIN/name).read_text().replace('#include "b2lusb.h"', '')
                   for name in ['b2lusb_power.c', 'b2lusb_hw.c'])
h = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef void VOID;
typedef uint8_t UCHAR, *PUCHAR;
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef int BOOLEAN;
typedef void *WDFDEVICE, *WDFWAITLOCK, *WDFSPINLOCK, *WDFTIMER;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#define KernelMode 0
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define STATUS_SUCCESS 0
#define STATUS_IO_TIMEOUT (-1)
#define STATUS_DEVICE_HARDWARE_ERROR (-2)
#define STATUS_DEVICE_BUSY (-3)
#define STATUS_DEVICE_NOT_READY (-4)
#define STATUS_DEVICE_POWER_FAILURE (-5)
#define STATUS_NOT_SUPPORTED (-6)
'''
h += public + '\n' + context
h += r'''
static ULONG dwc[0x10000/4], phy[0x200/4], pmu;
static ULONG cmu[0x8000/4], alive[2], fsys, drcg;
static int stuck_domain_on, stuck_domain_off;
static ULONG stuck_mux_offset; static char stuck_mux_bank;
typedef struct { char op, bank; ULONG off, value; } EVENT;
static EVENT events[40000];
static unsigned count, reset_reads, clear_after, reset_pending;
static int stuck_reset, stuck_sus, stuck_power, stuck_siddq, stuck_mode;
static unsigned sync_delays;
static void record(char op, char bank, ULONG off, ULONG value) {
    assert(count < sizeof(events)/sizeof(events[0]));
    events[count++] = (EVENT){op,bank,off,value};
}
static char bankof(const volatile void *p, ULONG *offset) {
    uintptr_t a=(uintptr_t)p;
    if(a >= (uintptr_t)dwc && a < (uintptr_t)dwc + sizeof(dwc)) {
        *offset=(ULONG)(a-(uintptr_t)dwc);return 'D';
    }
    if(a >= (uintptr_t)phy && a < (uintptr_t)phy + sizeof(phy)) {
        *offset=(ULONG)(a-(uintptr_t)phy);return 'P';
    }
    if(a >= (uintptr_t)cmu && a < (uintptr_t)cmu + sizeof(cmu)) {
        *offset=(ULONG)(a-(uintptr_t)cmu);return 'C';
    }
    if(a >= (uintptr_t)alive && a < (uintptr_t)alive + sizeof(alive)) {
        *offset=(ULONG)(a-(uintptr_t)alive);return 'A';
    }
    *offset=0;
    if(a==(uintptr_t)&fsys) return 'F';
    if(a==(uintptr_t)&drcg) return 'G';
    assert(a==(uintptr_t)&pmu);return 'M';
}
static ULONG READ_REGISTER_ULONG(volatile ULONG *p) {
    ULONG offset, value; char bank=bankof(p,&offset);
    assert((offset&3)==0);
    if(bank=='C' || bank=='G') assert(alive[1]&1);
    if(bank=='D' || bank=='P') {
        assert(alive[1]&1);assert(fsys&16);
        assert(cmu[0x100/4]&16);assert(cmu[0x120/4]&16);
    }
    if(bank=='D' && offset==0xc704 && reset_pending) {
        ++reset_reads;
        if(!stuck_reset && reset_reads>=clear_after) {
            dwc[offset/4] &= ~(1U<<30);reset_pending=0;
        }
    }
    value=*p;record('R',bank,offset,value);return value;
}
static UCHAR READ_REGISTER_UCHAR(volatile UCHAR *p) {
    ULONG offset;char bank=bankof(p,&offset);
    UCHAR value=*p;record('R',bank,offset,value);return value;
}
static void WRITE_REGISTER_ULONG(volatile ULONG *p, ULONG value) {
    ULONG offset;char bank=bankof(p,&offset);assert((offset&3)==0);
    record('W',bank,offset,value);
    if(bank=='D' && offset==0xc704 && (value&(1U<<30))) {
        reset_pending=1;reset_reads=0;
    }
    if(bank=='D' && offset==0xc110) {
        // The platform must never substitute a GCTL global core reset.
        assert(!(value&(1U<<11)));
        if(!stuck_mode) dwc[0xc118/4]=((value>>12)&3)==1 ? 1 : 0;
    }
    if(stuck_sus && bank=='D' && offset==0xc2c0) value |= 1U<<17;
    if(stuck_power && bank=='M') value &= ~1U;
    if(stuck_siddq && bank=='P' && offset==0x5c) value |= 1U<<24;
    if(bank==stuck_mux_bank && offset==stuck_mux_offset) value |= 128;
    *p=value;
    if(bank=='A' && offset==0) {
        if(value&1) {if(!stuck_domain_on) alive[1]|=1;}
        else if(!stuck_domain_off) alive[1]&=~1U;
    }
}
static void KeStallExecutionProcessor(ULONG us) { record('T',0,0,us); }
static NTSTATUS KeDelayExecutionThread(int mode,int alert,LARGE_INTEGER *delay) {
    assert(mode==KernelMode && !alert);
    if(delay->QuadPart == -10000) { record('S',0,0,1);return 0; }
    assert(!reset_pending);
    assert(delay->QuadPart == -500000); // 50 ms after hardware self-clear
    ++sync_delays;record('S',0,0,50);return 0;
}
static void WdfSpinLockAcquire(WDFSPINLOCK l) { (void)l; }
static void WdfSpinLockRelease(WDFSPINLOCK l) { (void)l; }
'''
h += '\n' + source + '\n'
h += r'''
static void init(B2L_USB_CONTEXT *c, int host) {
    memset(c,0,sizeof(*c));memset(dwc,0,sizeof(dwc));memset(phy,0,sizeof(phy));
    memset(cmu,0,sizeof(cmu));alive[0]=0xa0;alive[1]=0xb0;fsys=0x80000000;drcg=0xa1234567;
    stuck_domain_on=stuck_domain_off=0;stuck_mux_bank=0;stuck_mux_offset=0;
    cmu[0x100/4]=cmu[0x120/4]=0x80000000;
    c->Fsys0aCmu=(PUCHAR)cmu;c->Fsys0aPmu=(PUCHAR)alive;c->Fsys0Bus=&fsys;c->Fsys0Drcg=&drcg;
    pmu=0xa0;count=reset_reads=reset_pending=sync_delays=0;clear_after=3;
    stuck_reset=stuck_sus=stuck_power=stuck_siddq=stuck_mode=0;
    c->Dwc3Registers=(PUCHAR)dwc;c->PhyRegisters=(PUCHAR)phy;c->PmuUsbPhy=&pmu;
    c->Dwc3Length=sizeof(dwc);c->PhyLength=sizeof(phy);
    dwc[0]=0x01100020;dwc[0x24/4]=1;
    dwc[0xc120/4]=0x33313130;dwc[0xc118/4]=host ? 1 : 0;
    dwc[0xc110/4]=host ? 0x1000 : 0x2000;
    dwc[0xc704/4]=0x55;dwc[0xc70c/4]=1U<<22;
    dwc[0xc200/4]=(1U<<6)|0x80000000;
    dwc[0xc2c0/4]=(1U<<17)|0x80000000;
    phy[0x20/4]=0x80000000;phy[0x28/4]=0xabcdef12;
    phy[0x04/4]=0x80000000|(1U<<17);
    phy[0x48/4]=0x80000002;
    phy[0x50/4]=0x8000000f;phy[0x54/4]=0x80000000;
    phy[0x58/4]=0xffffffff;phy[0x5c/4]=1U<<24;
    phy[0x100/4]=0x81;phy[0x108/4]=0x01001234;
    phy[0x11c/4]=0x80000000;
}
static unsigned find_write(char bank, ULONG off, ULONG mask, ULONG val, unsigned start) {
    unsigned i;for(i=start;i<count;i++) {
        EVENT e=events[i];
        if(e.op=='W' && e.bank==bank && e.off==off && (e.value&mask)==val) return i;
    }
    assert(!"required write absent");return 0;
}
static unsigned writes(char bank, ULONG off) {
    unsigned i,n=0;for(i=0;i<count;i++) n+=events[i].op=='W' && events[i].bank==bank && events[i].off==off;
    return n;
}
static ULONG written(char bank, ULONG off, unsigned nth) {
    unsigned i;for(i=0;i<count;i++) if(events[i].op=='W' && events[i].bank==bank && events[i].off==off) {
        if(!nth--) return events[i].value;
    }
    assert(!"write absent");return 0;
}
static void assert_sequence(int host) {
    // Golden order includes original link pulse, PIPE before UTMI, then POR.
    // Every expected write is followed in order; no GCTL reset, DUALPHYSEL,
    // xHCI command/ring/doorbell or external combo address is permitted.
    const char banks[]="PPDDMPPPPPPPPPPPPPPPPPPPD";
    const ULONG offsets[]={0x20,0x20,0xc2c0,0xc200,0,0x04,0x04,0x20,
        0x50,0x54,0x04,0x50,0x54,0x58,0x5c,0x20,0x08,
        0x100,0x108,0x110,0x10c,0x11c,0x04,0x48,0xc704};
    unsigned i,n=0,reset_at=0,sync_at=0,tune_at=0;
    for(i=0;i<count && n<sizeof(offsets)/sizeof(offsets[0]);i++) if(events[i].op=='W' && (events[i].bank=='D' || events[i].bank=='P' || events[i].bank=='M')) {
        assert(events[i].bank==banks[n] && events[i].off==offsets[n]);
        if(events[i].off==0xc704) reset_at=i;
        ++n;
    }
    assert(n==sizeof(offsets)/sizeof(offsets[0]));
    assert(events[find_write('P',0x20,1,1,0)+1].op=='T');
    assert(events[find_write('P',0x20,1,1,0)+1].value==10);
    for(i=0;i<count;i++) if(events[i].op=='S') sync_at=i;
    tune_at=find_write('P',0x58,0,0,reset_at+1);
    assert(reset_at<sync_at && sync_at<tune_at);
    assert(find_write('D',0xc110,0x3000,host ? 0x1000 : 0x2000,0)>tune_at);
    assert(writes('D',0xc704)==1 && writes('D',0xc110)==1);
    assert(writes('P',0x28)==0 && phy[0x28/4]==0xabcdef12);
    for(i=0;i<count;i++) if(events[i].op=='W' && events[i].bank=='D')
        assert(events[i].off>=0xc000);
    assert(phy[0x100/4]==0x80 && phy[0x108/4]==0x10001234);
    assert(phy[0x110/4]==1 && phy[0x10c/4]==0xff00 && phy[0x11c/4]==0x80070007);
    assert((phy[0x04/4]&0x70000)==0x50000 && phy[0x48/4]==0x80000012);
    assert((phy[0x50/4]&0x3f)==(host ? 0x0c : 0x30));
    assert((phy[0x54/4]&0x3300)==(host ? 0x300 : 0x3300));
    assert((phy[0x20/4]&0x300f)==0x1004);
}
int main(void) {
    B2L_USB_CONTEXT c; ULONG last_tune; unsigned i;
    init(&c,0);
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==0);
    assert_sequence(1);
    assert(c.CoreResetCount==1 && sync_delays==1 && reset_reads==3);
    assert((written('P',0x58,0)&B2L_HSP_TUNE_BOARD_MASK)==0);
    assert((phy[0x58/4]&B2L_HSP_TUNE_BOARD_MASK)==0xc02c0507);
    last_tune=c.PhyTuneValue;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==0);
    assert((pmu&1)==0 && (phy[0x5c/4]&(1U<<24)) && c.PhyTuneValue==last_tune);
    count=0; // Resume reuses prior provider tune before selecting new role.
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleDevice)==0);
    assert((written('P',0x58,0)&B2L_HSP_TUNE_BOARD_MASK)==last_tune);
    assert((phy[0x58/4]&B2L_HSP_TUNE_BOARD_MASK)==0xc06c0307);
    assert(c.PhyTuneValue==0xc06c0307 && c.CoreResetCount==2);
    assert_sequence(0);
    init(&c,1);assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleDevice)==0);assert_sequence(0);
    init(&c,1);dwc[0x24/4]=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleDevice)==STATUS_DEVICE_BUSY);
    for(i=0;i<count;i++) assert(events[i].op!='W' || (events[i].bank!='D' && events[i].bank!='P'));
    assert(c.ClocksEnabled && (alive[1]&1)); // Busy child must retain its clocks.
    init(&c,0);dwc[0xc704/4]|=1U<<31;dwc[0xc70c/4]=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_DEVICE_BUSY);
    assert(writes('D',0xc704)==1 && writes('P',0x20)==0 && !c.CoreResetCount);
    init(&c,0);stuck_reset=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_IO_TIMEOUT);
    assert(reset_reads==1000 && !sync_delays && !c.CoreResetCount);
    assert(writes('D',0xc110)==0 && writes('P',0x58)==1 && writes('D',0xc704)==1);
    assert((pmu&1)==0 && (phy[0x5c/4]&(1U<<24)) && reset_pending);
    init(&c,0);clear_after=1000;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==0 && reset_reads==1000 && sync_delays==1);
    init(&c,0);stuck_sus=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_DEVICE_HARDWARE_ERROR);
    assert(writes('M',0)==0 && writes('P',0x58)==0 && writes('D',0xc704)==0);
    init(&c,0);stuck_power=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_DEVICE_POWER_FAILURE);
    assert(writes('P',0x58)==0 && writes('D',0xc704)==0 && !c.CoreResetCount);
    init(&c,0);stuck_siddq=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_DEVICE_POWER_FAILURE);
    assert((pmu&1)==0 && writes('D',0xc704)==0 && !c.CoreResetCount);
    init(&c,0);stuck_mode=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_IO_TIMEOUT);
    assert((pmu&1)==0 && c.CoreResetCount==1); // Reset count != role success.
    init(&c,0);dwc[0xc120/4]=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_DEVICE_HARDWARE_ERROR);
    assert(!writes('D',0xc704) && c.ClocksEnabled); // Unknown core: don't power it down.
    init(&c,0);assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==0);
    phy[0x0c/4]=(2U<<10)|0x3ff;phy[0x10/4]=3;count=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==STATUS_IO_TIMEOUT);
    assert(c.BusIdleSamples==100 && c.BusReadOutstanding==2 && c.BusWriteOutstanding==3);
    assert(c.ClocksEnabled && c.PowerStage==B2L_USB_POWER_BUS_IDLE);
    assert(!writes('C',0x100) && !writes('A',0));
    phy[0x0c/4]=0x3ff;phy[0x10/4]=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==0 && c.BusIdleSamples==1);
    // Native power sequence must precede every core/PHY access.
    init(&c,0);assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==0);
    assert(find_write('A',0,1,1,0) < find_write('F',0,16,16,0));
    assert(find_write('F',0,16,16,0) < find_write('C',0x100,16,16,0));
    assert(find_write('C',0x100,16,16,0) < find_write('C',0x120,16,16,0));
    assert(find_write('C',0x120,16,16,0) < find_write('P',0x20,1,1,0));
    assert(c.PowerStage==B2L_USB_POWER_READY && c.PowerReadMask==15 && c.ClocksEnabled);
    // No blanket QCH enabling. Their original values survive a real off/on.
    cmu[0x3030/4]=0x12345679;cmu[0x140/4]=0x88776611;cmu[0x800/4]=0xfedc1234;
    count=0;assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==0);
    assert(find_write('C',0x100,16,0,0)<find_write('F',0,16,0,0));
    assert(find_write('F',0,16,0,0)<find_write('C',0x120,16,0,0));
    assert(find_write('C',0x120,16,0,0)<find_write('A',0,1,0,0));
    assert(c.DomainSaved && !c.ClocksEnabled && !(alive[1]&1) && c.PowerStage==B2L_USB_POWER_OFF);
    memset(cmu,0,sizeof(cmu));drcg=0;count=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleDevice)==0);
    assert(cmu[0x3030/4]==0x12345679 && cmu[0x140/4]==0x88776611 && cmu[0x800/4]==0xfedc1234);
    assert(drcg==0xa1234567 && !c.DomainSaved);
    assert(find_write('C',0x140,0,0,0)<find_write('C',0x120,0,0,0));
    assert(find_write('C',0x3030,0,0,0)<find_write('G',0,0,0,0));
    assert(find_write('G',0,0,0,0)<find_write('F',0,16,16,0));
    // Connected HOST D0 exit keeps only the domain, not root clock refs.
    c.RequestedRole=B2lUsbRoleHost;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==0);
    assert(c.PowerStage==B2L_USB_POWER_DOMAIN_RETAINED && (alive[1]&1) && !c.ClocksEnabled);
    c.RequestedRole=B2lUsbRoleNone;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==0 && !(alive[1]&1));
    init(&c,0);alive[1]|=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==0 && !writes('A',0));
    // Each failed USER mux stops the path before any DWC3/PHY access.
    for(i=0;i<3;i++) {
        init(&c,0);stuck_mux_bank=i ? 'C' : 'F';
        stuck_mux_offset=i==1 ? 0x100 : i==2 ? 0x120 : 0;
        assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_IO_TIMEOUT);
        assert(c.PowerStage==B2L_USB_POWER_FSYS0_BUS+i && !c.ClocksEnabled);
        {unsigned k;for(k=0;k<count;k++) assert(events[k].bank!='D' && events[k].bank!='P');}
    }
    init(&c,0);stuck_domain_on=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_IO_TIMEOUT);
    assert(c.PowerStage==B2L_USB_POWER_DOMAIN_ON && c.PowerReadMask==1);
    assert(!writes('F',0) && !writes('P',0x20) && !c.ClocksEnabled);
    init(&c,0);stuck_mux_bank='C';stuck_mux_offset=0x120;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==STATUS_IO_TIMEOUT);
    assert(c.PowerStage==B2L_USB_POWER_SCLK && !c.ClocksEnabled);
    assert(!(fsys&16) && !(cmu[0x100/4]&16) && !writes('P',0x20));
    init(&c,0);assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleHost)==0);stuck_domain_off=1;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==STATUS_IO_TIMEOUT);
    assert(c.DomainSaved && !c.ClocksEnabled && c.PowerStage==B2L_USB_POWER_DOMAIN_OFF);
    // Cancel an off request that timed out before restoring and restarting.
    stuck_domain_off=0;count=0;
    assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleDevice)==0);
    assert(find_write('A',0,1,1,0)<find_write('C',0x140,0,0,0));
    { B2L_USB_STATUS status={0};
      assert(B2lUsbHardwareSetRole(&c,B2lUsbRoleNone)==0);c.HardwareReady=1;count=0;
      B2lUsbHardwareGetStatus(&c,&status);
      assert(count==0 && status.PowerStage==B2L_USB_POWER_OFF && status.PowerReadMask==15);
    }
    puts("Samsung PHY/DWC3 ordered MMIO, self-clear, tune lifetime and failure cleanup: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-usb-phy-') as directory:
    p = Path(directory)
    (p / 'test.c').write_text(h)
    subprocess.run(shlex.split(os.getenv('CC', 'cc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', str(p/'test.c'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
