#include "b2lmax77705.h"

#define HSI2C_CTRL              0x00U
#define HSI2C_FIFO_CTRL         0x04U
#define HSI2C_TRAIL_CTRL        0x08U
#define HSI2C_CLK_CTRL          0x0CU
#define HSI2C_CLK_SLOT          0x10U
#define HSI2C_INT_ENABLE        0x20U
#define HSI2C_INT_STATUS        0x24U
#define HSI2C_ERR_STATUS        0x2CU
#define HSI2C_FIFO_STATUS       0x30U
#define HSI2C_TX_DATA           0x34U
#define HSI2C_RX_DATA           0x38U
#define HSI2C_CONF              0x40U
#define HSI2C_AUTO_CONF         0x44U
#define HSI2C_TIMEOUT           0x48U
#define HSI2C_TRANS_STATUS      0x50U
#define HSI2C_TIMING_HS1        0x54U
#define HSI2C_TIMING_HS2        0x58U
#define HSI2C_TIMING_HS3        0x5CU
#define HSI2C_TIMING_FS1        0x60U
#define HSI2C_TIMING_FS2        0x64U
#define HSI2C_TIMING_FS3        0x68U
#define HSI2C_TIMING_SLA        0x6CU
#define HSI2C_ADDRESS           0x70U
#define HSI2C_USI_CON           0xC4U

#define HSI2C_MASTER            (1UL << 3)
#define HSI2C_RXCHON            (1UL << 6)
#define HSI2C_TXCHON            (1UL << 7)
#define HSI2C_SW_RST            (1UL << 31)
#define HSI2C_TRAILING_COUNT    0x00FFFFFFUL
#define HSI2C_AUTO_MODE         (1UL << 31)
#define HSI2C_TIMEOUT_EN        (1UL << 31)
#define HSI2C_RXFIFO_EN         (1UL << 0)
#define HSI2C_TXFIFO_EN         (1UL << 1)
#define HSI2C_RXFIFO_LEVEL(x)   ((x) << 4)
#define HSI2C_TXFIFO_LEVEL(x)   ((x) << 16)
#define HSI2C_READ_WRITE        (1UL << 16)
#define HSI2C_STOP_AFTER_TRANS  (1UL << 17)
#define HSI2C_MASTER_RUN        (1UL << 31)
#define HSI2C_INT_TX_ALMOSTEMPTY (1UL << 0)
#define HSI2C_INT_RX_ALMOSTFULL  (1UL << 1)
#define HSI2C_INT_FIFO_ERROR     (0xFUL << 2)
#define HSI2C_INT_TRAILING       (1UL << 6)
#define HSI2C_INT_TRANSFER_DONE (1UL << 7)
#define HSI2C_INT_CHECK_STATE    (0xFUL << 8)
#define HSI2C_TX_FIFO_FULL       (1UL << 7)
#define HSI2C_INT_NO_DEVICE     (1UL << 10)
#define HSI2C_TX_FIFO_LEVEL(x)  ((x) & 0x7FUL)
#define HSI2C_TX_FIFO_EMPTY     (1UL << 8)
#define HSI2C_RX_FIFO_EMPTY     (1UL << 24)
#define HSI2C_MASTER_STATE(x)   ((x) & 0x0FUL)
#define HSI2C_MASTER_NO_ACK     0x0BUL
#define HSI2C_MASTER_LOST       0x0CUL
#define HSI2C_FAST_MASTER_ID    (0x7UL << 24)
#define HSI2C_USI_RESET_RELEASE 0x00000000UL

#define B2L_USI_CONFIG_OFFSET   0x30U
#define B2L_USI_I2C_MODE        0x04U
#define B2L_CMGP_MUX_USI3       0x1020U
#define B2L_CMGP_DIV_USI3       0x1824U
#define B2L_CMGP_QCH_USI3       0x304CU
#define B2L_CMGP_MUX_SELECT     (1UL << 0)
#define B2L_CMGP_DIV_RATIO_MASK 0x0000000FUL
#define B2L_CMGP_DIV_200MHZ     0x00000001UL
#define B2L_CMGP_CLOCK_BUSY     (1UL << 16)
#define B2L_CMGP_QCH_MASK       0x00000007UL
#define B2L_CMGP_QCH_NORMAL     0x00000001UL

#define B2L_GPM12_CON           0x180U
#define B2L_GPM12_PUD           0x188U
#define B2L_GPM12_DRV           0x18CU
#define B2L_GPM13_CON           0x1A0U
#define B2L_GPM13_PUD           0x1A8U
#define B2L_GPM13_DRV           0x1ACU
#define B2L_PIN0_FIELD_MASK     0x0000000FUL
#define B2L_PIN_FUNCTION_I2C    0x00000002UL

#define B2L_HSI2C_FS1_MASK      0x00FF0000UL
#define B2L_HSI2C_FS1_400KHZ    0x00F00000UL
#define B2L_HSI2C_FS2_MASK      0x000000FFUL
#define B2L_HSI2C_FS2_400KHZ    0x000000E0UL
#define B2L_HSI2C_FS3_MASK      0x00FF0000UL
#define B2L_HSI2C_FS3_400KHZ    0x00210000UL
// exynos5-i2c IRQ completion timeout: msecs_to_jiffies(1000).
#define B2L_HSI2C_IRQ_TIMEOUT_100NS 10000000ULL
#define B2L_HSI2C_RETRIES       2UL
#define B2L_CMGP_CLOCK_TIMEOUT_US 10000UL

typedef struct _B2L_HSI2C_TIMING_STATE {
    ULONG ClockSlot;
    ULONG Conf;
    ULONG TimingHs1;
    ULONG TimingHs2;
    ULONG TimingHs3;
    ULONG TimingFs1;
    ULONG TimingFs2;
    ULONG TimingFs3;
    ULONG TimingSla;
} B2L_HSI2C_TIMING_STATE;

static ULONG
B2lMaxReadRegister(PB2L_MAX_CONTEXT Context, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Context->HsI2cRegisters + Offset));
}

static VOID
B2lMaxWriteRegister(PB2L_MAX_CONTEXT Context, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->HsI2cRegisters + Offset), Value);
}

static VOID
B2lMaxAndOrRegister(
    PB2L_MAX_CONTEXT Context,
    ULONG Offset,
    ULONG AndMask,
    ULONG OrMask
    )
{
    ULONG Value = B2lMaxReadRegister(Context, Offset);
    B2lMaxWriteRegister(Context, Offset, (Value & AndMask) | OrMask);
}

static ULONG
B2lMaxReadMappedRegister(PUCHAR Registers, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Registers + Offset));
}

static VOID
B2lMaxWriteMappedRegister(PUCHAR Registers, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Registers + Offset), Value);
}

static VOID
B2lMaxAndOrMappedRegister(
    PUCHAR Registers,
    ULONG Offset,
    ULONG AndMask,
    ULONG OrMask
    )
{
    ULONG Value = B2lMaxReadMappedRegister(Registers, Offset);

    B2lMaxWriteMappedRegister(Registers, Offset, (Value & AndMask) | OrMask);
}

static VOID
B2lMaxSleepMicroseconds(ULONG Microseconds)
{
    LARGE_INTEGER Interval;

    // Clock setup/stretch and recovery waits run at PASSIVE_LEVEL.
    // Message completion itself is signaled by the real HSI2C interrupt.
    // Yield during peripheral clock settling on the single-core WinPE.
    Interval.QuadPart = -((LONGLONG)Microseconds * 10LL);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static NTSTATUS
B2lMaxWaitCmgpClockReady(PB2L_MAX_CONTEXT Context, ULONG Offset)
{
    ULONGLONG Deadline;

    Deadline = KeQueryInterruptTime() +
               ((ULONGLONG)B2L_CMGP_CLOCK_TIMEOUT_US * 10ULL);
    while (KeQueryInterruptTime() < Deadline) {
        if ((B2lMaxReadMappedRegister(Context->CmgpCmuRegisters, Offset) &
             B2L_CMGP_CLOCK_BUSY) == 0UL) {
            return STATUS_SUCCESS;
        }
        B2lMaxSleepMicroseconds(50UL);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
B2lMaxPreparePlatform(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status;
    ULONG Gpm12Pull;
    ULONG Gpm12Drive;
    ULONG Gpm13Pull;
    ULONG Gpm13Drive;

    Context->PlatformPrepared = FALSE;
    if ((Context->CmgpCmuRegisters == NULL) ||
        (Context->CmgpPinctrlRegisters == NULL)) {
        return STATUS_DEVICE_NOT_READY;
    }

    // exynos9820.dtsi hsi2c_45 selects hsi2c45_bus: GPM12-0=SCL and
    // GPM13-0=SDA, function 2, no pull, drive setting 0.  Apply that pinctrl
    // state before exposing the controller to the MAX77705 transaction path.
    B2lMaxAndOrMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM12_PUD,
        ~B2L_PIN0_FIELD_MASK,
        0UL
        );
    B2lMaxAndOrMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM12_DRV,
        ~B2L_PIN0_FIELD_MASK,
        0UL
        );
    B2lMaxAndOrMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM13_PUD,
        ~B2L_PIN0_FIELD_MASK,
        0UL
        );
    B2lMaxAndOrMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM13_DRV,
        ~B2L_PIN0_FIELD_MASK,
        0UL
        );
    B2lMaxAndOrMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM12_CON,
        ~B2L_PIN0_FIELD_MASK,
        B2L_PIN_FUNCTION_I2C
        );
    B2lMaxAndOrMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM13_CON,
        ~B2L_PIN0_FIELD_MASK,
        B2L_PIN_FUNCTION_I2C
        );

    Context->Gpm12Con = B2lMaxReadMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM12_CON
        );
    Context->Gpm13Con = B2lMaxReadMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM13_CON
        );
    Gpm12Pull = B2lMaxReadMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM12_PUD
        );
    Gpm12Drive = B2lMaxReadMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM12_DRV
        );
    Gpm13Pull = B2lMaxReadMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM13_PUD
        );
    Gpm13Drive = B2lMaxReadMappedRegister(
        Context->CmgpPinctrlRegisters,
        B2L_GPM13_DRV
        );
    if (((Context->Gpm12Con & B2L_PIN0_FIELD_MASK) !=
         B2L_PIN_FUNCTION_I2C) ||
        ((Context->Gpm13Con & B2L_PIN0_FIELD_MASK) !=
         B2L_PIN_FUNCTION_I2C) ||
        ((Gpm12Pull & B2L_PIN0_FIELD_MASK) != 0UL) ||
        ((Gpm12Drive & B2L_PIN0_FIELD_MASK) != 0UL) ||
        ((Gpm13Pull & B2L_PIN0_FIELD_MASK) != 0UL) ||
        ((Gpm13Drive & B2L_PIN0_FIELD_MASK) != 0UL)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    // The stock DT asks the exynos5-hsi2c driver for 200 MHz.  Make that
    // result deterministic here: first install the divide-by-two ratio, then
    // select the 400 MHz CMGP bus parent.  This order cannot momentarily
    // over-clock an already active USI3 instance.  Preserve unrelated CMU
    // fields and wait for each glitch-free clock update to leave BUSY.
    B2lMaxAndOrMappedRegister(
        Context->CmgpCmuRegisters,
        B2L_CMGP_DIV_USI3,
        ~B2L_CMGP_DIV_RATIO_MASK,
        B2L_CMGP_DIV_200MHZ
        );
    Status = B2lMaxWaitCmgpClockReady(Context, B2L_CMGP_DIV_USI3);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    B2lMaxAndOrMappedRegister(
        Context->CmgpCmuRegisters,
        B2L_CMGP_MUX_USI3,
        ~B2L_CMGP_MUX_SELECT,
        B2L_CMGP_MUX_SELECT
        );
    Status = B2lMaxWaitCmgpClockReady(Context, B2L_CMGP_MUX_USI3);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    B2lMaxAndOrMappedRegister(
        Context->CmgpCmuRegisters,
        B2L_CMGP_QCH_USI3,
        ~B2L_CMGP_QCH_MASK,
        B2L_CMGP_QCH_NORMAL
        );

    Context->CmgpMuxUsi3 = B2lMaxReadMappedRegister(
        Context->CmgpCmuRegisters,
        B2L_CMGP_MUX_USI3
        );
    Context->CmgpDivUsi3 = B2lMaxReadMappedRegister(
        Context->CmgpCmuRegisters,
        B2L_CMGP_DIV_USI3
        );
    Context->CmgpQchUsi3 = B2lMaxReadMappedRegister(
        Context->CmgpCmuRegisters,
        B2L_CMGP_QCH_USI3
        );
    if (((Context->CmgpMuxUsi3 & B2L_CMGP_MUX_SELECT) == 0UL) ||
        ((Context->CmgpDivUsi3 & B2L_CMGP_DIV_RATIO_MASK) !=
         B2L_CMGP_DIV_200MHZ) ||
        ((Context->CmgpQchUsi3 & B2L_CMGP_QCH_MASK) !=
         B2L_CMGP_QCH_NORMAL)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Context->PlatformPrepared = TRUE;
    return STATUS_SUCCESS;
}

static VOID
B2lMaxCaptureControllerState(PB2L_MAX_CONTEXT Context)
{
    Context->HsI2cControl = B2lMaxReadRegister(Context, HSI2C_CTRL);
    Context->HsI2cClockControl = B2lMaxReadRegister(Context, HSI2C_CLK_CTRL);
    Context->HsI2cIntStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    Context->HsI2cErrorStatus = B2lMaxReadRegister(Context, HSI2C_ERR_STATUS);
    Context->HsI2cTransferStatus =
        B2lMaxReadRegister(Context, HSI2C_TRANS_STATUS);
    Context->HsI2cFifoStatus = B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS);
    Context->HsI2cTimingFs[0] = B2lMaxReadRegister(Context, HSI2C_TIMING_FS1);
    Context->HsI2cTimingFs[1] = B2lMaxReadRegister(Context, HSI2C_TIMING_FS2);
    Context->HsI2cTimingFs[2] = B2lMaxReadRegister(Context, HSI2C_TIMING_FS3);
    Context->HsI2cConfiguration = B2lMaxReadRegister(Context, HSI2C_CONF);
    Context->HsI2cAutoConfiguration =
        B2lMaxReadRegister(Context, HSI2C_AUTO_CONF);
    Context->HsI2cAddress = B2lMaxReadRegister(Context, HSI2C_ADDRESS);
    Context->HsI2cUsiControl = B2lMaxReadRegister(Context, HSI2C_USI_CON);
}

static VOID
B2lMaxResetController(PB2L_MAX_CONTEXT Context)
{
    B2L_HSI2C_TIMING_STATE Timing;

    // Exynos resets can clear timing registers.  Preserve the fields which
    // are unrelated to the bus rate, then replace the three FAST_SPD fields
    // with Samsung exynos5_i2c_set_timing() for DT default-clk=200 MHz and
    // clock-frequency=400 kHz: div=33, TSCL_H=0xe0, TSTART_HD=0xf0.
    Timing.ClockSlot = B2lMaxReadRegister(Context, HSI2C_CLK_SLOT);
    Timing.Conf = B2lMaxReadRegister(Context, HSI2C_CONF);
    Timing.TimingHs1 = B2lMaxReadRegister(Context, HSI2C_TIMING_HS1);
    Timing.TimingHs2 = B2lMaxReadRegister(Context, HSI2C_TIMING_HS2);
    Timing.TimingHs3 = B2lMaxReadRegister(Context, HSI2C_TIMING_HS3);
    Timing.TimingFs1 =
        (B2lMaxReadRegister(Context, HSI2C_TIMING_FS1) &
         ~B2L_HSI2C_FS1_MASK) | B2L_HSI2C_FS1_400KHZ;
    Timing.TimingFs2 =
        (B2lMaxReadRegister(Context, HSI2C_TIMING_FS2) &
         ~B2L_HSI2C_FS2_MASK) | B2L_HSI2C_FS2_400KHZ;
    Timing.TimingFs3 =
        (B2lMaxReadRegister(Context, HSI2C_TIMING_FS3) &
         ~B2L_HSI2C_FS3_MASK) | B2L_HSI2C_FS3_400KHZ;
    Timing.TimingSla = B2lMaxReadRegister(Context, HSI2C_TIMING_SLA);

    // usi-i2c-v2 has a second reset gate inside the HSI2C register page.
    // Samsung clears it before touching the controller.  The separate
    // CMGP03 mode register only selects I2C and does not release this gate.
    B2lMaxWriteRegister(Context, HSI2C_USI_CON, HSI2C_USI_RESET_RELEASE);
    B2lMaxAndOrRegister(Context, HSI2C_CTRL, ~0UL, HSI2C_SW_RST);
    B2lMaxAndOrRegister(Context, HSI2C_CTRL, ~HSI2C_SW_RST, 0UL);
    B2lMaxWriteRegister(Context, HSI2C_CLK_SLOT, Timing.ClockSlot);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_HS1, Timing.TimingHs1);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_HS2, Timing.TimingHs2);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_HS3, Timing.TimingHs3);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_FS1, Timing.TimingFs1);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_FS2, Timing.TimingFs2);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_FS3, Timing.TimingFs3);
    B2lMaxWriteRegister(Context, HSI2C_TIMING_SLA, Timing.TimingSla);
    // universal9820 android-9.0 exynos5_i2c_init() writes MASTER alone.
    // FUNC_MODE_I2C is defined by the shared IP header but is not asserted
    // on this USI-backed controller; setting it changes the exact working
    // HSI2C45 state before the first MAX77705 address phase.
    B2lMaxWriteRegister(Context, HSI2C_CTRL, HSI2C_MASTER);
    B2lMaxWriteRegister(Context, HSI2C_TRAIL_CTRL, HSI2C_TRAILING_COUNT);
    B2lMaxWriteRegister(Context, HSI2C_CONF, Timing.Conf | HSI2C_AUTO_MODE);
}

VOID
B2lMaxHsI2cQuiesce(PB2L_MAX_CONTEXT Context)
{
    ULONG InterruptStatus;

    if ((Context == NULL) || (Context->HsI2cRegisters == NULL)) {
        return;
    }

    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
    InterruptStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    if (InterruptStatus != 0UL) {
        B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
    }
    B2lMaxAndOrRegister(Context, HSI2C_AUTO_CONF, ~HSI2C_MASTER_RUN, 0UL);
    B2lMaxAndOrRegister(
        Context,
        HSI2C_CTRL,
        ~(HSI2C_RXCHON | HSI2C_TXCHON),
        0UL
        );
}

NTSTATUS
B2lMaxHsI2cInitialize(PB2L_MAX_CONTEXT Context)
{
    PULONG UsiMode;
    NTSTATUS Status;

    if ((Context == NULL) || (Context->HsI2cRegisters == NULL) ||
        (Context->UsiRegisters == NULL) ||
        (Context->CmgpCmuRegisters == NULL) ||
        (Context->CmgpPinctrlRegisters == NULL)) {
        return STATUS_DEVICE_NOT_READY;
    }

    Context->HsI2cFailurePhase = B2L_MAX_HSI2C_PHASE_PLATFORM;
    Status = B2lMaxPreparePlatform(Context);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    UsiMode = (PULONG)(Context->UsiRegisters + B2L_USI_CONFIG_OFFSET);
    if (READ_REGISTER_ULONG(UsiMode) != B2L_USI_I2C_MODE) {
        WRITE_REGISTER_ULONG(UsiMode, B2L_USI_I2C_MODE);
        if (READ_REGISTER_ULONG(UsiMode) != B2L_USI_I2C_MODE) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    B2lMaxWriteRegister(Context, HSI2C_USI_CON, HSI2C_USI_RESET_RELEASE);
    if (B2lMaxReadRegister(Context, HSI2C_USI_CON) !=
        HSI2C_USI_RESET_RELEASE) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    B2lMaxResetController(Context);
    B2lMaxHsI2cQuiesce(Context);
    B2lMaxCaptureControllerState(Context);
    // CLK_CTRL is zero on the working Exynos9820 HSI2C45 hand-off.  Samsung's
    // driver never treats that register as a readiness flag; the programmed
    // FS timing and AUTO_MODE state are the meaningful controller contract.
    if (((Context->HsI2cTimingFs[0] & B2L_HSI2C_FS1_MASK) !=
         B2L_HSI2C_FS1_400KHZ) ||
        ((Context->HsI2cTimingFs[1] & B2L_HSI2C_FS2_MASK) !=
         B2L_HSI2C_FS2_400KHZ) ||
        ((Context->HsI2cTimingFs[2] & B2L_HSI2C_FS3_MASK) !=
         B2L_HSI2C_FS3_400KHZ) ||
        ((Context->HsI2cConfiguration & HSI2C_AUTO_MODE) == 0UL)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Context->HsI2cFailurePhase = B2L_MAX_HSI2C_PHASE_NONE;
    return STATUS_SUCCESS;
}

// Called at DIRQL with the framework interrupt lock held. No waits, I2C
// client commands, registry calls or completion of pageable work belong here.
BOOLEAN
B2lMaxHsI2cEvtInterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(WdfInterruptGetDevice(Interrupt));
    ULONG Pending;
    ULONG Enabled;
    ULONG Budget;
    NTSTATUS Status = STATUS_PENDING;

    UNREFERENCED_PARAMETER(MessageId);
    if (!Context->HsI2cIrqEnabled || !Context->PlatformPrepared) {
        return FALSE;
    }
    Pending = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    Enabled = B2lMaxReadRegister(Context, HSI2C_INT_ENABLE);
    if ((Pending & Enabled) == 0UL) {
        return FALSE;
    }
    ++Context->HsI2cInterruptCount;
    Context->HsI2cLastInterruptStatus = Pending;
    if (!Context->HsI2cMessageActive) {
        B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
        B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, Pending);
        return TRUE;
    }

    // Never move more than one FIFO's worth of data during this ISR.
    if ((Pending & (HSI2C_INT_CHECK_STATE | HSI2C_INT_FIFO_ERROR)) != 0UL) {
        // Decode errors below, including any newly latched while in the ISR.
    } else if (Context->HsI2cMessageRead) {
        Budget = 16UL;
        while ((Budget-- != 0UL) &&
               (Context->HsI2cMessagePosition < Context->HsI2cMessageLength) &&
               ((B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS) &
                 HSI2C_RX_FIFO_EMPTY) == 0UL)) {
            Context->HsI2cMessageBuffer[Context->HsI2cMessagePosition++] =
                (UCHAR)B2lMaxReadRegister(Context, HSI2C_RX_DATA);
        }
    } else {
        Budget = 16UL;
        while ((Budget-- != 0UL) &&
               (Context->HsI2cMessagePosition < Context->HsI2cMessageLength) &&
               ((B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS) &
                 HSI2C_TX_FIFO_FULL) == 0UL)) {
            B2lMaxWriteRegister(Context, HSI2C_TX_DATA,
                Context->HsI2cMessageBuffer[Context->HsI2cMessagePosition++]);
        }
        if (Context->HsI2cMessagePosition == Context->HsI2cMessageLength) {
            Enabled &= ~HSI2C_INT_TX_ALMOSTEMPTY;
            B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, Enabled);
        }
    }
    // Samsung samples transfer status after FIFO service. Capture errors
    // latched during that service too, and give them priority over DONE.
    Pending |= B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    Context->HsI2cLastInterruptStatus = Pending;
    if ((Pending & HSI2C_INT_NO_DEVICE) != 0UL) {
        Status = STATUS_NO_SUCH_DEVICE;
    } else if ((Pending & (HSI2C_INT_CHECK_STATE | HSI2C_INT_FIFO_ERROR)) != 0UL) {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    } else if (Context->HsI2cMessagePosition == Context->HsI2cMessageLength) {
        // Like exynos5_i2c_irq(), RX needs all bytes, not an extra DONE bit.
        // TX needs real transfer completion as well as every queued byte.
        if (Context->HsI2cMessageRead ||
            (((Pending & HSI2C_INT_TRANSFER_DONE) != 0UL) &&
             ((B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS) &
               HSI2C_TX_FIFO_EMPTY) != 0UL))) {
            Status = STATUS_SUCCESS;
        }
    }
    if (Status != STATUS_PENDING) {
        B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
        Context->HsI2cMessageStatus = Status;
        Context->HsI2cMessageActive = FALSE;
        ++Context->HsI2cCompletionCount;
    }
    // W1C only the captured status, never a read/modify/write ack.
    B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, Pending);
    if (Status != STATUS_PENDING) {
        (VOID)WdfInterruptQueueDpcForIsr(Interrupt);
    }
    return TRUE;
}

VOID
B2lMaxHsI2cEvtInterruptDpc(WDFINTERRUPT Interrupt, WDFOBJECT AssociatedObject)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(WdfInterruptGetDevice(Interrupt));

    UNREFERENCED_PARAMETER(AssociatedObject);
    // KeSetEvent is legal at DISPATCH_LEVEL, not at the hardware's DIRQL.
    // A late DPC can only wake a waiter; it never marks a message successful.
    (VOID)KeSetEvent(&Context->HsI2cCompletion, IO_NO_INCREMENT, FALSE);
}

NTSTATUS
B2lMaxHsI2cEvtInterruptEnable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);

    UNREFERENCED_PARAMETER(Interrupt);
    // D0Entry has already prepared clocks/pins and quiesced the controller.
    // Each transfer arms the peripheral only after publishing its buffer.
    Context->HsI2cIrqEnabled = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
B2lMaxHsI2cEvtInterruptDisable(WDFINTERRUPT Interrupt, WDFDEVICE Device)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(Device);

    UNREFERENCED_PARAMETER(Interrupt);
    if (Context->PlatformPrepared) {
        B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
    }
    Context->HsI2cIrqEnabled = FALSE;
    // PreInterruptsDisabled has already stopped/joined the passive worker.
    // Defensive cancellation cannot grant success or touch its buffer.
    if (Context->HsI2cMessageActive) {
        Context->HsI2cMessageStatus = STATUS_DEVICE_NOT_READY;
        Context->HsI2cMessageActive = FALSE;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxFinishClockStretch(PB2L_MAX_CONTEXT Context)
{
    ULONGLONG Deadline = KeQueryInterruptTime() + 1000000ULL;

    // DT samsung,scl-clk-stretching: the Samsung IRQ branch checks INIT
    // after completion, for at most 100 ms. This is not a pre-transfer gate
    // on firmware's retained state and does not synthesize a completion.
    do {
        if (HSI2C_MASTER_STATE(B2lMaxReadRegister(Context, HSI2C_TRANS_STATUS)) == 0UL) {
            return STATUS_SUCCESS;
        }
        B2lMaxSleepMicroseconds(1000UL);
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
B2lMaxTransferMessage(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    UCHAR *Buffer,
    ULONG BufferLength,
    BOOLEAN IsRead,
    BOOLEAN Stop
    )
{
    ULONG AutoConfig;
    ULONG FifoControl;
    ULONG InterruptStatus;
    ULONG InterruptEnable;
    ULONG TargetAddress;
    ULONG TriggerLevel;
    ULONGLONG Deadline;
    ULONGLONG Now;
    LARGE_INTEGER Remaining;
    NTSTATUS Status;
    NTSTATUS WaitStatus = STATUS_SUCCESS;

    if ((Buffer == NULL) || (BufferLength == 0UL) ||
        (BufferLength > 0xFFFFUL) || (SlaveAddress > 0x7FU)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Context->HsI2cInterrupt == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    KeClearEvent(&Context->HsI2cCompletion);
    WdfInterruptAcquireLock(Context->HsI2cInterrupt);
    if (!Context->HsI2cIrqEnabled || !Context->PlatformPrepared) {
        WdfInterruptReleaseLock(Context->HsI2cInterrupt);
        return STATUS_DEVICE_NOT_READY;
    }
    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
    B2lMaxAndOrRegister(Context, HSI2C_TIMEOUT, ~HSI2C_TIMEOUT_EN, 0UL);
    TriggerLevel = (BufferLength >= 8UL) ? 8UL : BufferLength;
    FifoControl = HSI2C_RXFIFO_EN | HSI2C_TXFIFO_EN |
                  HSI2C_RXFIFO_LEVEL(TriggerLevel) |
                  HSI2C_TXFIFO_LEVEL(TriggerLevel);
    B2lMaxWriteRegister(Context, HSI2C_FIFO_CTRL, FifoControl);
    B2lMaxAndOrRegister(Context, HSI2C_CTRL,
        IsRead ? ~HSI2C_TXCHON : ~HSI2C_RXCHON,
        IsRead ? HSI2C_RXCHON : HSI2C_TXCHON);
    TargetAddress = B2lMaxReadRegister(Context, HSI2C_ADDRESS);
    TargetAddress &= ~((0x3FFUL << 10) | 0x3FFUL | (0xFFUL << 24));
    TargetAddress |= HSI2C_FAST_MASTER_ID | ((SlaveAddress & 0x7FUL) << 10);
    B2lMaxWriteRegister(Context, HSI2C_ADDRESS, TargetAddress);
    Context->HsI2cAttemptedAddress = TargetAddress;
    AutoConfig = B2lMaxReadRegister(Context, HSI2C_AUTO_CONF);
    AutoConfig &= ~(HSI2C_READ_WRITE | HSI2C_STOP_AFTER_TRANS |
                    HSI2C_MASTER_RUN | 0xFFFFUL);
    if (IsRead) {
        AutoConfig |= HSI2C_READ_WRITE;
    }
    if (Stop) {
        AutoConfig |= HSI2C_STOP_AFTER_TRANS;
    }
    AutoConfig |= BufferLength;
    B2lMaxWriteRegister(Context, HSI2C_AUTO_CONF, AutoConfig);
    InterruptStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);

    Context->HsI2cMessageBuffer = Buffer;
    Context->HsI2cMessageLength = BufferLength;
    Context->HsI2cMessagePosition = 0UL;
    Context->HsI2cMessageRead = IsRead;
    Context->HsI2cMessageStatus = STATUS_PENDING;
    Context->HsI2cMessageActive = TRUE;
    InterruptEnable = HSI2C_INT_CHECK_STATE | HSI2C_INT_TRANSFER_DONE |
        (IsRead ? (HSI2C_INT_RX_ALMOSTFULL | HSI2C_INT_TRAILING) :
                  HSI2C_INT_TX_ALMOSTEMPTY);
    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, InterruptEnable);
    Deadline = KeQueryInterruptTime() + B2L_HSI2C_IRQ_TIMEOUT_100NS;
    B2lMaxAndOrRegister(Context, HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN);
    WdfInterruptReleaseLock(Context->HsI2cInterrupt);

    for (;;) {
        WdfInterruptAcquireLock(Context->HsI2cInterrupt);
        if (!Context->HsI2cMessageActive) {
            Status = Context->HsI2cMessageStatus;
            break;
        }
        Now = KeQueryInterruptTime();
        if ((WaitStatus != STATUS_SUCCESS) || (Now >= Deadline)) {
            Status = ((WaitStatus == STATUS_SUCCESS) || (WaitStatus == STATUS_TIMEOUT)) ?
                STATUS_IO_TIMEOUT : WaitStatus;
            Context->HsI2cMessageStatus = Status;
            Context->HsI2cMessageActive = FALSE;
            break;
        }
        Remaining.QuadPart = -(LONGLONG)(Deadline - Now);
        WdfInterruptReleaseLock(Context->HsI2cInterrupt);
        WaitStatus = KeWaitForSingleObject(&Context->HsI2cCompletion,
            Executive, KernelMode, FALSE, &Remaining);
        // SynchronizationEvent consumes a stale/late wake. Only ISR state
        // can complete this message, and retries never extend its deadline.
    }
    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
    Context->HsI2cMessageBuffer = NULL;
    WdfInterruptReleaseLock(Context->HsI2cInterrupt);
    if (NT_SUCCESS(Status)) {
        Status = B2lMaxFinishClockStretch(Context);
    }
    if (!NT_SUCCESS(Status)) {
        if (IsRead) {
            ++Context->HsI2cReadFailures;
            Context->HsI2cLastReadFailureStatus = (ULONG)Status;
            Context->HsI2cLastReadFailureLength = BufferLength;
            Context->HsI2cLastReadFailureTransferred = Context->HsI2cMessagePosition;
        }
        B2lMaxCaptureControllerState(Context);
    }
    return Status;
}

NTSTATUS
B2lMaxHsI2cWriteRead(
    PB2L_MAX_CONTEXT Context,
    UCHAR SlaveAddress,
    const UCHAR *WriteBuffer,
    ULONG WriteLength,
    UCHAR *ReadBuffer,
    ULONG ReadLength
    )
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    ULONG Retry;

    if ((Context == NULL) || !Context->HardwareReady ||
        (SlaveAddress > 0x7FU) ||
        ((WriteLength == 0UL) && (ReadLength == 0UL)) ||
        ((WriteLength != 0UL) && (WriteBuffer == NULL)) ||
        ((ReadLength != 0UL) && (ReadBuffer == NULL))) {
        return STATUS_INVALID_PARAMETER;
    }

    for (Retry = 0UL; Retry < B2L_HSI2C_RETRIES; ++Retry) {
        Context->HsI2cRetryCount = Retry;
        // Stock hsi2c_45 has no samsung,reset-before-trans property.
        // exynos5_i2c_xfer() therefore does not gate a message on stale
        // TRANS_STATUS.MASTER_BUSY: xfer_msg overwrites ADDRESS/AUTO_CONF
        // and asserts MASTER_RUN.  Waiting here deadlocked the Windows path
        // in the firmware's retained ADDR1 state before ADDRESS was written.
        if (WriteLength != 0UL) {
            Status = B2lMaxTransferMessage(
                Context,
                SlaveAddress,
                (UCHAR *)(ULONG_PTR)WriteBuffer,
                WriteLength,
                FALSE,
                (BOOLEAN)(ReadLength == 0UL)
            );
            if (!NT_SUCCESS(Status)) {
                Context->HsI2cFailurePhase = B2L_MAX_HSI2C_PHASE_WRITE;
                goto RetryTransfer;
            }
        }
        if (ReadLength != 0UL) {
            Status = B2lMaxTransferMessage(
                Context,
                SlaveAddress,
                ReadBuffer,
                ReadLength,
                TRUE,
                TRUE
            );
            if (!NT_SUCCESS(Status)) {
                Context->HsI2cFailurePhase = B2L_MAX_HSI2C_PHASE_READ;
                goto RetryTransfer;
            }
        }
        Context->HsI2cFailurePhase = B2L_MAX_HSI2C_PHASE_NONE;
        B2lMaxCaptureControllerState(Context);
        B2lMaxHsI2cQuiesce(Context);
        return STATUS_SUCCESS;

RetryTransfer:
        if (!Context->HsI2cIrqEnabled || (Status == STATUS_DEVICE_NOT_READY)) {
            return Status;
        }
        // hsi2c_45 does not carry samsung,reset-before-trans.  Preserve the
        // live controller on the normal path and recover exactly once only
        // after a real timeout/NACK/arbitration failure.
        B2lMaxResetController(Context);
        B2lMaxHsI2cQuiesce(Context);
        B2lMaxSleepMicroseconds(100U);
    }
    Context->HsI2cRetryCount = B2L_HSI2C_RETRIES;
    return Status;
}
