#include "b2lusb.h"

// Samsung universal9820 55f0426: exynos-pd.c, pmucal_local.c,
// flexpmu_cal_local_exynos9820.h, composite.c and ra.c. These are the DT
// USB consumers' USER muxes, not the TOP PLL/rate or Q-channel controls.
#define B2L_MUX_SEL (1UL << 4)
#define B2L_MUX_BUSY (1UL << 7)

static const ULONG B2lFsys0aSaveOffsets[] = {
    0x140, 0x120, 0x100, 0x3030, 0x302c, 0x3028, 0x3020,
    0x3018, 0x3014, 0x3010, 0x3000, 0x800
};
static const ULONG B2lFsys0aDebugOffsets[] = {
    0x7030, 0x702c, 0x7028, 0x7020, 0x7018, 0x7014, 0x7010, 0x7000
};

static PULONG B2lPowerRegister(PUCHAR Base, ULONG Offset)
{
    return (PULONG)(Base + Offset);
}

static NTSTATUS B2lPowerResult(PB2L_USB_CONTEXT Context, NTSTATUS Status)
{
    Context->PowerStatus = Status;
    return Status;
}

static NTSTATUS B2lSetUserMux(PULONG Register, BOOLEAN Enable, PULONG Last)
{
    ULONG Value, Index;
    Value = READ_REGISTER_ULONG(Register);
    Value = (Value & ~B2L_MUX_SEL) | (Enable ? B2L_MUX_SEL : 0);
    WRITE_REGISTER_ULONG(Register, Value);
    // ra_set_div_mux -> ra_wait_done: 100 reads, 1 us after each busy read.
    for (Index = 0; Index < 100; ++Index) {
        *Last = READ_REGISTER_ULONG(Register);
        if ((*Last & B2L_MUX_BUSY) == 0) {
            return ((*Last & B2L_MUX_SEL) == (Enable ? B2L_MUX_SEL : 0)) ?
                STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR;
        }
        KeStallExecutionProcessor(1);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS B2lSetFsys0aDomain(PB2L_USB_CONTEXT Context, BOOLEAN Enable)
{
    ULONG Value, Index;
    PULONG Config = B2lPowerRegister(Context->Fsys0aPmu, 0);
    PULONG Status = B2lPowerRegister(Context->Fsys0aPmu, 4);
    Value = READ_REGISTER_ULONG(Config);
    WRITE_REGISTER_ULONG(Config, (Value & ~1UL) | (Enable ? 1UL : 0));
    // pmucal_rae_wait: status bit 0 only; never clear/set the status in SW.
    for (Index = 0; Index <= 2000; ++Index) {
        Context->PowerPmuStatus = READ_REGISTER_ULONG(Status);
        if ((Context->PowerPmuStatus & 1UL) == (Enable ? 1UL : 0))
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(1);
    }
    return STATUS_IO_TIMEOUT;
}

NTSTATUS B2lUsbPowerAcquire(PB2L_USB_CONTEXT Context)
{
    ULONG Index;
    NTSTATUS Status;
    if (Context->ClocksEnabled) return STATUS_SUCCESS;
    if (!Context->Fsys0aPmu || !Context->Fsys0aCmu ||
        !Context->Fsys0Bus || !Context->Fsys0Drcg)
        return B2lPowerResult(Context, STATUS_DEVICE_NOT_READY);

    Context->PowerStage = B2L_USB_POWER_DOMAIN_ON;
    Context->PowerPmuStatus = READ_REGISTER_ULONG(
        B2lPowerRegister(Context->Fsys0aPmu, 4));
    Context->PowerReadMask |= 1UL;
    if (!(Context->PowerPmuStatus & 1UL) || Context->DomainSaved) {
        Status = B2lSetFsys0aDomain(Context, TRUE);
        if (!NT_SUCCESS(Status)) return B2lPowerResult(Context, Status);
    }
    // Like pmucal, no invented cold-boot restore values. Only restore a
    // snapshot actually taken before our own power-off, in table order.
    if (Context->DomainSaved) {
        Context->PowerStage = B2L_USB_POWER_RESTORE;
        for (Index = 0; Index < 12; ++Index)
            WRITE_REGISTER_ULONG(B2lPowerRegister(Context->Fsys0aCmu,
                B2lFsys0aSaveOffsets[Index]), Context->DomainSave[Index]);
        WRITE_REGISTER_ULONG(Context->Fsys0Drcg, Context->DomainSave[12]);
        Context->DomainSaved = FALSE;
    }

    // CCF parent recursion for aclk: FSYS0_BUS -> FSYS0A_BUS -> SLV_LINK.
    // SLV_LINK has VCLK_GATE without VCLK_QCH_DIS: cal_vclk_enable is a
    // no-op. PHY's second reference to that clock adds no register write.
    // sclk (USB31DRD USER mux) follows aclk in the real DT clock list.
    Context->PowerStage = B2L_USB_POWER_FSYS0_BUS;
    Context->PowerReadMask |= 2UL;
    Status = B2lSetUserMux(Context->Fsys0Bus, TRUE, &Context->PowerFsys0Bus);
    if (!NT_SUCCESS(Status)) return B2lPowerResult(Context, Status);
    Context->PowerStage = B2L_USB_POWER_ACLK;
    Context->PowerReadMask |= 4UL;
    Status = B2lSetUserMux(B2lPowerRegister(Context->Fsys0aCmu, 0x100),
        TRUE, &Context->PowerBus);
    if (!NT_SUCCESS(Status)) {
        (VOID)B2lSetUserMux(Context->Fsys0Bus, FALSE, &Context->PowerFsys0Bus);
        return B2lPowerResult(Context, Status);
    }
    Context->PowerStage = B2L_USB_POWER_SCLK;
    Context->PowerReadMask |= 8UL;
    Status = B2lSetUserMux(B2lPowerRegister(Context->Fsys0aCmu, 0x120),
        TRUE, &Context->PowerSclk);
    if (!NT_SUCCESS(Status)) {
        (VOID)B2lSetUserMux(B2lPowerRegister(Context->Fsys0aCmu, 0x100),
            FALSE, &Context->PowerBus);
        (VOID)B2lSetUserMux(Context->Fsys0Bus, FALSE, &Context->PowerFsys0Bus);
        return B2lPowerResult(Context, Status);
    }
    Context->ClocksEnabled = TRUE;
    Context->PowerStage = B2L_USB_POWER_READY;
    return B2lPowerResult(Context, STATUS_SUCCESS);
}

NTSTATUS B2lUsbPowerRelease(PB2L_USB_CONTEXT Context)
{
    ULONG Index, Value;
    NTSTATUS Status;
    LARGE_INTEGER Delay;
    if (!Context->ClocksEnabled) return STATUS_SUCCESS;
    // dwc3_otg_check_bus_act, after core/PHY exit and before runtime_put.
    // Select the real outstanding AXI counters, not synthetic completion.
    Context->PowerStage = B2L_USB_POWER_BUS_IDLE;
    Context->BusIdleSamples = 0;
    Value = READ_REGISTER_ULONG(B2lPowerRegister(Context->Dwc3Registers, 0xc170));
    WRITE_REGISTER_ULONG(B2lPowerRegister(Context->Dwc3Registers, 0xc170),
        Value | (0x3fUL << 16));
    Delay.QuadPart = -10000; // Linux waits 1 ms between up to 100 samples.
    for (Index = 0; Index < 100; ++Index) {
        Value = READ_REGISTER_ULONG(B2lPowerRegister(Context->PhyRegisters, 0x0c));
        Context->BusReadOutstanding = (Value >> 10) & 0x3fffffUL;
        Value = READ_REGISTER_ULONG(B2lPowerRegister(Context->PhyRegisters, 0x10));
        Context->BusWriteOutstanding = Value & 0x3fffffUL;
        ++Context->BusIdleSamples;
        if (!Context->BusReadOutstanding && !Context->BusWriteOutstanding) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    // Linux logs this timeout. Windows propagates failure and retains power
    // rather than declare a successful D-state transition with DMA pending.
    if (Index == 100) return B2lPowerResult(Context, STATUS_IO_TIMEOUT);

    // Caller has stopped the role child and completed PHY exit. Root aclk
    // releases its last SLV_LINK reference, then sclk, in Linux list order.
    Context->ClocksEnabled = FALSE;
    Context->PowerStage = B2L_USB_POWER_CLOCKS_OFF;
    Status = B2lSetUserMux(B2lPowerRegister(Context->Fsys0aCmu, 0x100),
        FALSE, &Context->PowerBus);
    if (!NT_SUCCESS(Status)) return B2lPowerResult(Context, Status);
    Status = B2lSetUserMux(Context->Fsys0Bus, FALSE, &Context->PowerFsys0Bus);
    if (!NT_SUCCESS(Status)) return B2lPowerResult(Context, Status);
    Status = B2lSetUserMux(B2lPowerRegister(Context->Fsys0aCmu, 0x120),
        FALSE, &Context->PowerSclk);
    if (!NT_SUCCESS(Status)) return B2lPowerResult(Context, Status);

    // exynos-pd power-down-ok=USB uses otg_is_connect: HOST ownership,
    // not generic CC attachment. D0 exit retains RequestedRole; the real
    // URS host-stop/detach changes it. Function does not assert that veto.
    if (Context->RequestedRole == B2lUsbRoleHost) {
        Context->PowerStage = B2L_USB_POWER_DOMAIN_RETAINED;
        return B2lPowerResult(Context, STATUS_SUCCESS);
    }
    Context->PowerStage = B2L_USB_POWER_SAVE;
    for (Index = 0; Index < 3; ++Index)
        Context->DomainSave[Index] = READ_REGISTER_ULONG(B2lPowerRegister(
            Context->Fsys0aCmu, B2lFsys0aSaveOffsets[Index]));
    for (Index = 0; Index < 8; ++Index)
        Context->DomainDebug[Index] = READ_REGISTER_ULONG(B2lPowerRegister(
            Context->Fsys0aCmu, B2lFsys0aDebugOffsets[Index]));
    for (Index = 3; Index < 12; ++Index)
        Context->DomainSave[Index] = READ_REGISTER_ULONG(B2lPowerRegister(
            Context->Fsys0aCmu, B2lFsys0aSaveOffsets[Index]));
    Context->DomainSave[12] = READ_REGISTER_ULONG(Context->Fsys0Drcg);
    Context->DomainSaved = TRUE;
    Context->PowerStage = B2L_USB_POWER_DOMAIN_OFF;
    Status = B2lSetFsys0aDomain(Context, FALSE);
    if (NT_SUCCESS(Status)) Context->PowerStage = B2L_USB_POWER_OFF;
    return B2lPowerResult(Context, Status);
}
