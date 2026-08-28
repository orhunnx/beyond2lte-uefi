#include "b2ltouch.h"

#define HSI2C_CTRL              0x00U
#define HSI2C_FIFO_CTRL         0x04U
#define HSI2C_TRAIL_CTRL        0x08U
#define HSI2C_INT_ENABLE        0x20U
#define HSI2C_INT_STATUS        0x24U
#define HSI2C_FIFO_STATUS       0x30U
#define HSI2C_TX_DATA           0x34U
#define HSI2C_RX_DATA           0x38U
#define HSI2C_CONF              0x40U
#define HSI2C_AUTO_CONF         0x44U
#define HSI2C_TIMEOUT           0x48U
#define HSI2C_ADDRESS           0x70U

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
#define HSI2C_TX_FIFO_LEVEL(x)  ((x) & 0x7FUL)
#define HSI2C_TX_FIFO_EMPTY     (1UL << 8)
#define HSI2C_RX_FIFO_EMPTY     (1UL << 24)

#define B2L_USI_CONFIG_OFFSET   0x1CU
#define B2L_USI_I2C_MODE        0x04U
#define B2L_HSI2C_TIMEOUT_US    100000UL

static ULONG
B2lReadRegister(PB2L_TOUCH_CONTEXT Context, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Context->HsI2cRegisters + Offset));
}

static VOID
B2lWriteRegister(PB2L_TOUCH_CONTEXT Context, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->HsI2cRegisters + Offset), Value);
}

static VOID
B2lAndOrRegister(
    PB2L_TOUCH_CONTEXT Context,
    ULONG Offset,
    ULONG AndMask,
    ULONG OrMask
    )
{
    ULONG Value = B2lReadRegister(Context, Offset);
    B2lWriteRegister(Context, Offset, (Value & AndMask) | OrMask);
}

static VOID
B2lResetController(PB2L_TOUCH_CONTEXT Context)
{
    B2lAndOrRegister(Context, HSI2C_CTRL, ~0UL, HSI2C_SW_RST);
    B2lAndOrRegister(Context, HSI2C_CTRL, ~HSI2C_SW_RST, 0UL);
    B2lWriteRegister(Context, HSI2C_CTRL, HSI2C_MASTER);
    B2lWriteRegister(Context, HSI2C_TRAIL_CTRL, HSI2C_TRAILING_COUNT);
    B2lAndOrRegister(Context, HSI2C_CONF, ~0UL, HSI2C_AUTO_MODE);
}

VOID
B2lHsI2cQuiesce(PB2L_TOUCH_CONTEXT Context)
{
    ULONG InterruptStatus;

    if ((Context == NULL) || (Context->HsI2cRegisters == NULL)) {
        return;
    }

    B2lWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
    InterruptStatus = B2lReadRegister(Context, HSI2C_INT_STATUS);
    if (InterruptStatus != 0UL) {
        B2lWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
    }
    B2lAndOrRegister(Context, HSI2C_AUTO_CONF, ~HSI2C_MASTER_RUN, 0UL);
    B2lAndOrRegister(
        Context,
        HSI2C_CTRL,
        ~(HSI2C_RXCHON | HSI2C_TXCHON),
        0UL
        );
}

NTSTATUS
B2lHsI2cInitialize(PB2L_TOUCH_CONTEXT Context)
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

    B2lResetController(Context);
    B2lHsI2cQuiesce(Context);
    return STATUS_SUCCESS;
}

static NTSTATUS
B2lTransferMessage(
    PB2L_TOUCH_CONTEXT Context,
    UCHAR *Buffer,
    ULONG BufferLength,
    BOOLEAN IsRead
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

    if ((Buffer == NULL) || (BufferLength == 0UL) ||
        (BufferLength > 0xFFFFUL)) {
        return STATUS_INVALID_PARAMETER;
    }

    B2lAndOrRegister(Context, HSI2C_TIMEOUT, ~HSI2C_TIMEOUT_EN, 0UL);
    TriggerLevel = (BufferLength >= 8UL) ? 8UL : BufferLength;
    FifoControl = HSI2C_RXFIFO_EN | HSI2C_TXFIFO_EN |
                  HSI2C_RXFIFO_LEVEL(TriggerLevel) |
                  HSI2C_TXFIFO_LEVEL(TriggerLevel);
    B2lWriteRegister(Context, HSI2C_FIFO_CTRL, FifoControl);
    B2lAndOrRegister(
        Context,
        HSI2C_CTRL,
        IsRead ? ~HSI2C_TXCHON : ~HSI2C_RXCHON,
        IsRead ? HSI2C_RXCHON : HSI2C_TXCHON
        );

    TargetAddress = B2lReadRegister(Context, HSI2C_ADDRESS);
    TargetAddress &= ~((0x3FFUL << 10) | 0x3FFUL | (0xFFUL << 24));
    TargetAddress |= (B2L_HSI2C_SLAVE & 0x7FUL) << 10;
    B2lWriteRegister(Context, HSI2C_ADDRESS, TargetAddress);

    AutoConfig = B2lReadRegister(Context, HSI2C_AUTO_CONF);
    AutoConfig &= ~(HSI2C_READ_WRITE | HSI2C_STOP_AFTER_TRANS |
                    HSI2C_MASTER_RUN | 0xFFFFUL);
    if (IsRead) {
        AutoConfig |= HSI2C_READ_WRITE;
    }
    AutoConfig |= HSI2C_STOP_AFTER_TRANS | BufferLength;
    B2lWriteRegister(Context, HSI2C_AUTO_CONF, AutoConfig);

    InterruptStatus = B2lReadRegister(Context, HSI2C_INT_STATUS);
    if (InterruptStatus != 0UL) {
        B2lWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
    }
    B2lWriteRegister(Context, HSI2C_INT_ENABLE, HSI2C_INT_TRANSFER_DONE);
    B2lAndOrRegister(Context, HSI2C_AUTO_CONF, ~0UL, HSI2C_MASTER_RUN);

    Index = 0UL;
    Timeout = B2L_HSI2C_TIMEOUT_US;
    if (IsRead) {
        while (Timeout-- != 0UL) {
            if ((Index < BufferLength) &&
                ((B2lReadRegister(Context, HSI2C_FIFO_STATUS) &
                  HSI2C_RX_FIFO_EMPTY) == 0UL)) {
                Buffer[Index++] =
                    (UCHAR)B2lReadRegister(Context, HSI2C_RX_DATA);
            }
            if (Index >= BufferLength) {
                InterruptStatus = B2lReadRegister(Context, HSI2C_INT_STATUS);
                if ((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0UL) {
                    B2lWriteRegister(
                        Context,
                        HSI2C_INT_STATUS,
                        InterruptStatus
                        );
                    B2lWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
                    return STATUS_SUCCESS;
                }
            }
            KeStallExecutionProcessor(1U);
        }
    } else {
        while ((Timeout != 0UL) && (Index < BufferLength)) {
            FifoStatus = B2lReadRegister(Context, HSI2C_FIFO_STATUS);
            if (HSI2C_TX_FIFO_LEVEL(FifoStatus) < 16UL) {
                B2lWriteRegister(Context, HSI2C_TX_DATA, Buffer[Index++]);
            }
            --Timeout;
            KeStallExecutionProcessor(1U);
        }
        while (Timeout-- != 0UL) {
            InterruptStatus = B2lReadRegister(Context, HSI2C_INT_STATUS);
            FifoStatus = B2lReadRegister(Context, HSI2C_FIFO_STATUS);
            if (((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0UL) &&
                ((FifoStatus & HSI2C_TX_FIFO_EMPTY) != 0UL)) {
                B2lWriteRegister(Context, HSI2C_INT_STATUS, InterruptStatus);
                B2lWriteRegister(Context, HSI2C_INT_ENABLE, 0UL);
                return STATUS_SUCCESS;
            }
            KeStallExecutionProcessor(1U);
        }
    }

    B2lResetController(Context);
    B2lHsI2cQuiesce(Context);
    return STATUS_IO_TIMEOUT;
}

NTSTATUS
B2lHsI2cWriteRead(
    PB2L_TOUCH_CONTEXT Context,
    const UCHAR *WriteBuffer,
    ULONG WriteLength,
    UCHAR *ReadBuffer,
    ULONG ReadLength
    )
{
    NTSTATUS Status;

    if ((Context == NULL) || !Context->HardwareReady ||
        ((WriteLength == 0UL) && (ReadLength == 0UL)) ||
        ((WriteLength != 0UL) && (WriteBuffer == NULL)) ||
        ((ReadLength != 0UL) && (ReadBuffer == NULL))) {
        return STATUS_INVALID_PARAMETER;
    }

    B2lResetController(Context);
    if (WriteLength != 0UL) {
        Status = B2lTransferMessage(
            Context,
            (UCHAR *)(ULONG_PTR)WriteBuffer,
            WriteLength,
            FALSE
            );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    if (ReadLength != 0UL) {
        Status = B2lTransferMessage(Context, ReadBuffer, ReadLength, TRUE);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }
    return STATUS_SUCCESS;
}
