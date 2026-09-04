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
#define HSI2C_INT_TRANSFER_DONE (1UL << 7)
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
#define B2L_HSI2C_TIMEOUT_US    100000UL
#define B2L_HSI2C_POLL_DELAY_US 1000UL
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

    // Every caller is serialized by the PASSIVE_LEVEL device/timer
    // contract.  A single-core WinPE must yield while HSI2C advances;
    // spinning here can starve PnP and make the whole boot appear hung.
    Interval.QuadPart = -((LONGLONG)Microseconds * 10LL);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static ULONGLONG
B2lMaxPollingDeadline(VOID)
{
    return KeQueryInterruptTime() +
           ((ULONGLONG)B2L_HSI2C_TIMEOUT_US * 10ULL);
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

static NTSTATUS
B2lMaxCheckTransferError(PB2L_MAX_CONTEXT Context)
{
    ULONG InterruptStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    ULONG TransferStatus = B2lMaxReadRegister(Context, HSI2C_TRANS_STATUS);
    ULONG MasterState = HSI2C_MASTER_STATE(TransferStatus);

    // In polling mode Exynos enables only TRANSFER_DONE.  Bits 8..11 are a
    // grouped transfer-state indication, not four independent enable bits.
    // NODEV is the only individually defined status bit in Samsung's current
    // Exynos9820 driver; detailed failures come from TRANS_STATUS.
    if (((InterruptStatus & HSI2C_INT_NO_DEVICE) != 0UL) ||
        (MasterState == HSI2C_MASTER_NO_ACK)) {
        return STATUS_NO_SUCH_DEVICE;
    }
    if (MasterState == HSI2C_MASTER_LOST) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    return STATUS_SUCCESS;
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
    ULONG FifoStatus;
    ULONG Index;
    ULONG InterruptStatus;
    ULONG TargetAddress;
    ULONG TriggerLevel;
    ULONGLONG Deadline;
    NTSTATUS Status;

    if ((Buffer == NULL) || (BufferLength == 0UL) ||
        (BufferLength > 0xFFFFUL) || (SlaveAddress > 0x7FU)) {
        return STATUS_INVALID_PARAMETER;
    }

    B2lMaxAndOrRegister(Context, HSI2C_TIMEOUT, ~HSI2C_TIMEOUT_EN, 0UL);
    TriggerLevel = (BufferLength >= 8UL) ? 8UL : BufferLength;
    FifoControl = HSI2C_RXFIFO_EN | HSI2C_TXFIFO_EN |
                  HSI2C_RXFIFO_LEVEL(TriggerLevel) |
                  HSI2C_TXFIFO_LEVEL(TriggerLevel);
    B2lMaxWriteRegister(Context, HSI2C_FIFO_CTRL, FifoControl);
    B2lMaxAndOrRegister(
        Context,
        HSI2C_CTRL,
        IsRead ? ~HSI2C_TXCHON : ~HSI2C_RXCHON,
        IsRead ? HSI2C_RXCHON : HSI2C_TXCHON
        );

    TargetAddress = B2lMaxReadRegister(Context, HSI2C_ADDRESS);
    TargetAddress &= ~((0x3FFUL << 10) | 0x3FFUL | (0xFFUL << 24));
    // Stock Exynos9820 fast-mode HSI2C writes master ID 7 in bits 31:24
    // before the 7-bit target address.  Leaving it zero can stall the state
    // machine before ADDR0 even though the bus and FIFO both look idle.
    TargetAddress |= HSI2C_FAST_MASTER_ID |
                     ((SlaveAddress & 0x7FUL) << 10);
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
    if (InterruptStatus != 0UL) {
        B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
    }
    Index = 0UL;
    // All register/opcode writes used by this driver fit in the 16-byte FIFO.
    // Preload before MASTER_RUN, matching the working UEFI transport, so a
    // polling Windows path cannot lose the first byte to TX underrun.
    if (!IsRead) {
        while ((Index < BufferLength) && (Index < 16UL)) {
            B2lMaxWriteRegister(Context, HSI2C_TX_DATA, Buffer[Index++]);
        }
    }
    // Stock HSI2C45 uses its DT interrupt.  ACPI\BYN0004 deliberately does
    // not expose GPA1-6 until a Windows GPIO interrupt-controller contract
    // exists, so this leaf driver applies Samsung's bounded polling branch.
    // That branch enables only TRANSFER_DONE; the 0xF00 state group belongs
    // to Samsung's interrupt-driven branch.
    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, HSI2C_INT_TRANSFER_DONE);
    B2lMaxAndOrRegister(Context, HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN);

    Deadline = B2lMaxPollingDeadline();
    if (IsRead) {
        for (;;) {
            Status = B2lMaxCheckTransferError(Context);
            if (!NT_SUCCESS(Status)) {
                goto Failed;
            }
            if ((Index < BufferLength) &&
                ((B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS) &
                  HSI2C_RX_FIFO_EMPTY) == 0UL)) {
                Buffer[Index++] =
                    (UCHAR)B2lMaxReadRegister(Context, HSI2C_RX_DATA);
            }
            if (Index >= BufferLength) {
                InterruptStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
                if (InterruptStatus != 0UL) {
                    B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
                }
                B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
                // Samsung's polling branch completes a read when the
                // requested bytes have left RX FIFO.  TRANSFER_DONE is not a
                // second required condition and may already be clear.
                return STATUS_SUCCESS;
            }
            if (KeQueryInterruptTime() >= Deadline) {
                break;
            }
            B2lMaxSleepMicroseconds(B2L_HSI2C_POLL_DELAY_US);
        }
    } else {
        while (Index < BufferLength) {
            Status = B2lMaxCheckTransferError(Context);
            if (!NT_SUCCESS(Status)) {
                goto Failed;
            }
            FifoStatus = B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS);
            if (HSI2C_TX_FIFO_LEVEL(FifoStatus) < 16UL) {
                B2lMaxWriteRegister(Context, HSI2C_TX_DATA, Buffer[Index++]);
            }
            if (KeQueryInterruptTime() >= Deadline) {
                break;
            }
            B2lMaxSleepMicroseconds(B2L_HSI2C_POLL_DELAY_US);
        }
        while (KeQueryInterruptTime() < Deadline) {
            Status = B2lMaxCheckTransferError(Context);
            if (!NT_SUCCESS(Status)) {
                goto Failed;
            }
            InterruptStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
            FifoStatus = B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS);
            if (((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0UL) &&
                ((FifoStatus & HSI2C_TX_FIFO_EMPTY) != 0UL)) {
                B2lMaxWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
                B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
                return STATUS_SUCCESS;
            }
            B2lMaxSleepMicroseconds(B2L_HSI2C_POLL_DELAY_US);
        }
    }

    Status = STATUS_IO_TIMEOUT;

Failed:
    B2lMaxCaptureControllerState(Context);
    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
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
