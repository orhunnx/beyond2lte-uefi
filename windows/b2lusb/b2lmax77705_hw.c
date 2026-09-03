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

#define HSI2C_FUNC_MODE_I2C     (1UL << 0)
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
#define HSI2C_INT_TRANS_ABORT   (1UL << 8)
#define HSI2C_INT_NO_DEV_ACK    (1UL << 9)
#define HSI2C_INT_NO_DEVICE     (1UL << 10)
#define HSI2C_INT_TIMEOUT       (1UL << 11)
#define HSI2C_INT_TRANSFER_MASK (HSI2C_INT_TRANSFER_DONE | \
                                 HSI2C_INT_TRANS_ABORT | \
                                 HSI2C_INT_NO_DEV_ACK | \
                                 HSI2C_INT_NO_DEVICE | \
                                 HSI2C_INT_TIMEOUT)
#define HSI2C_TX_FIFO_LEVEL(x)  ((x) & 0x7FUL)
#define HSI2C_TX_FIFO_EMPTY     (1UL << 8)
#define HSI2C_RX_FIFO_EMPTY     (1UL << 24)
#define HSI2C_MASTER_STATE(x)   ((x) & 0x0FUL)
#define HSI2C_MASTER_BUSY       (1UL << 17)
#define HSI2C_MASTER_NO_ACK     0x0BUL
#define HSI2C_MASTER_LOST       0x0CUL
#define HSI2C_FAST_MASTER_ID    (0x7UL << 24)

#define B2L_USI_CONFIG_OFFSET   0x30U
#define B2L_USI_I2C_MODE        0x04U
#define B2L_HSI2C_TIMEOUT_US    100000UL
#define B2L_HSI2C_RETRIES       3UL

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
}

static VOID
B2lMaxResetController(PB2L_MAX_CONTEXT Context)
{
    B2L_HSI2C_TIMING_STATE Timing;

    // Exynos resets can clear timing registers.  Preserve the exact 200 MHz
    // FAST_SPD values programmed by B2LMax77705Dxe instead of guessing the
    // controller input clock in this Windows leaf driver.
    Timing.ClockSlot = B2lMaxReadRegister(Context, HSI2C_CLK_SLOT);
    Timing.Conf = B2lMaxReadRegister(Context, HSI2C_CONF);
    Timing.TimingHs1 = B2lMaxReadRegister(Context, HSI2C_TIMING_HS1);
    Timing.TimingHs2 = B2lMaxReadRegister(Context, HSI2C_TIMING_HS2);
    Timing.TimingHs3 = B2lMaxReadRegister(Context, HSI2C_TIMING_HS3);
    Timing.TimingFs1 = B2lMaxReadRegister(Context, HSI2C_TIMING_FS1);
    Timing.TimingFs2 = B2lMaxReadRegister(Context, HSI2C_TIMING_FS2);
    Timing.TimingFs3 = B2lMaxReadRegister(Context, HSI2C_TIMING_FS3);
    Timing.TimingSla = B2lMaxReadRegister(Context, HSI2C_TIMING_SLA);

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
    B2lMaxWriteRegister(
        Context,
        HSI2C_CTRL,
        HSI2C_FUNC_MODE_I2C | HSI2C_MASTER
        );
    B2lMaxWriteRegister(Context, HSI2C_TRAIL_CTRL, HSI2C_TRAILING_COUNT);
    B2lMaxWriteRegister(Context, HSI2C_CONF, Timing.Conf | HSI2C_AUTO_MODE);
}

static NTSTATUS
B2lMaxCheckTransferError(PB2L_MAX_CONTEXT Context)
{
    ULONG InterruptStatus = B2lMaxReadRegister(Context, HSI2C_INT_STATUS);
    ULONG TransferStatus = B2lMaxReadRegister(Context, HSI2C_TRANS_STATUS);
    ULONG MasterState = HSI2C_MASTER_STATE(TransferStatus);

    if (((InterruptStatus &
          (HSI2C_INT_NO_DEV_ACK | HSI2C_INT_NO_DEVICE)) != 0UL) ||
        (MasterState == HSI2C_MASTER_NO_ACK)) {
        return STATUS_NO_SUCH_DEVICE;
    }
    if (((InterruptStatus & HSI2C_INT_TRANS_ABORT) != 0UL) ||
        (MasterState == HSI2C_MASTER_LOST)) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if ((InterruptStatus & HSI2C_INT_TIMEOUT) != 0UL) {
        return STATUS_IO_TIMEOUT;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lMaxWaitBusIdle(PB2L_MAX_CONTEXT Context)
{
    ULONG Timeout = B2L_HSI2C_TIMEOUT_US;

    while (Timeout-- != 0UL) {
        if ((B2lMaxReadRegister(Context, HSI2C_TRANS_STATUS) &
             HSI2C_MASTER_BUSY) == 0UL) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1U);
    }
    return STATUS_IO_TIMEOUT;
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

    if ((Context == NULL) || (Context->HsI2cRegisters == NULL) ||
        (Context->UsiRegisters == NULL)) {
        return STATUS_DEVICE_NOT_READY;
    }

    UsiMode = (PULONG)(Context->UsiRegisters + B2L_USI_CONFIG_OFFSET);
    if (READ_REGISTER_ULONG(UsiMode) != B2L_USI_I2C_MODE) {
        WRITE_REGISTER_ULONG(UsiMode, B2L_USI_I2C_MODE);
        if (READ_REGISTER_ULONG(UsiMode) != B2L_USI_I2C_MODE) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    B2lMaxResetController(Context);
    B2lMaxHsI2cQuiesce(Context);
    B2lMaxCaptureControllerState(Context);
    // CLK_CTRL is zero on the working Exynos9820 HSI2C45 hand-off.  Samsung's
    // driver never treats that register as a readiness flag; the programmed
    // FS timing and AUTO_MODE state are the meaningful controller contract.
    if (((Context->HsI2cTimingFs[0] | Context->HsI2cTimingFs[1] |
          Context->HsI2cTimingFs[2]) == 0UL) ||
        ((Context->HsI2cConfiguration & HSI2C_AUTO_MODE) == 0UL)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
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
    ULONG Timeout;
    ULONG TriggerLevel;
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
    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, HSI2C_INT_TRANSFER_MASK);
    B2lMaxAndOrRegister(Context, HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN);

    Timeout = B2L_HSI2C_TIMEOUT_US;
    if (IsRead) {
        while (Timeout-- != 0UL) {
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
                if ((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0UL) {
                    B2lMaxWriteRegister(
                        Context,
                        HSI2C_INT_STATUS,
                        InterruptStatus
                        );
                    B2lMaxWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
                    if (Stop) {
                        Status = B2lMaxWaitBusIdle(Context);
                        if (!NT_SUCCESS(Status)) {
                            goto Failed;
                        }
                    }
                    return STATUS_SUCCESS;
                }
            }
            KeStallExecutionProcessor(1U);
        }
    } else {
        while ((Timeout != 0UL) && (Index < BufferLength)) {
            Status = B2lMaxCheckTransferError(Context);
            if (!NT_SUCCESS(Status)) {
                goto Failed;
            }
            FifoStatus = B2lMaxReadRegister(Context, HSI2C_FIFO_STATUS);
            if (HSI2C_TX_FIFO_LEVEL(FifoStatus) < 16UL) {
                B2lMaxWriteRegister(Context, HSI2C_TX_DATA, Buffer[Index++]);
            }
            --Timeout;
            KeStallExecutionProcessor(1U);
        }
        while (Timeout-- != 0UL) {
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
                if (Stop) {
                    Status = B2lMaxWaitBusIdle(Context);
                    if (!NT_SUCCESS(Status)) {
                        goto Failed;
                    }
                }
                return STATUS_SUCCESS;
            }
            KeStallExecutionProcessor(1U);
        }
    }

    Status = STATUS_IO_TIMEOUT;

Failed:
    B2lMaxCaptureControllerState(Context);
    B2lMaxResetController(Context);
    B2lMaxHsI2cQuiesce(Context);
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
        B2lMaxResetController(Context);
        Status = B2lMaxWaitBusIdle(Context);
        if (!NT_SUCCESS(Status)) {
            B2lMaxCaptureControllerState(Context);
            goto RetryTransfer;
        }
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
                goto RetryTransfer;
            }
        }
        B2lMaxCaptureControllerState(Context);
        B2lMaxHsI2cQuiesce(Context);
        return STATUS_SUCCESS;

RetryTransfer:
        B2lMaxResetController(Context);
        B2lMaxHsI2cQuiesce(Context);
        KeStallExecutionProcessor(100U);
    }
    Context->HsI2cRetryCount = B2L_HSI2C_RETRIES;
    return Status;
}
