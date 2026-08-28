/** @file
  Minimal combined-transfer transport for beyond2lte HSI2C10.

  SamsungPkg's public HSI2C protocol initializes the controller but its data
  API always emits a four-byte register address.  sec_ts uses one-byte
  commands.  The stock Android DT explicitly sets samsung,stop-after-trans
  on HSI2C10, so command writes and following reads are separate messages,
  each ending with STOP.
**/

#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationHelperLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EFIHsI2c.h>
#include <Protocol/EFIGpio.h>

#include "B2LHsI2c.h"

// SamsungPkg's protocol header predates the usual GUID extern declaration.
extern EFI_GUID  gEfiHsI2cProtocolGuid;

#define B2L_HSI2C_BUS           10
#define B2L_HSI2C_SLAVE         0x48
#define B2L_HSI2C_BASE          0x104B0000UL
#define B2L_USI_CONFIG_BASE      0x10421000UL
#define B2L_USI_CONFIG_REGISTER  0x1042101CUL
#define B2L_USI_CONFIG_SIZE      0x00001000UL
#define B2L_HSI2C_RETRIES       1
#define B2L_HSI2C_TIMEOUT_US    100000

#define HSI2C_CTRL              0x00
#define HSI2C_FIFO_CTRL         0x04
#define HSI2C_TRAIL_CTRL        0x08
#define HSI2C_CLK_CTRL          0x0C
#define HSI2C_CLK_SLOT          0x10
#define HSI2C_INT_ENABLE        0x20
#define HSI2C_INT_STATUS        0x24
#define HSI2C_ERR_STATUS        0x2C
#define HSI2C_FIFO_STATUS       0x30
#define HSI2C_TX_DATA           0x34
#define HSI2C_RX_DATA           0x38
#define HSI2C_CONF              0x40
#define HSI2C_AUTO_CONF         0x44
#define HSI2C_TIMEOUT           0x48
#define HSI2C_CMD               0x4C
#define HSI2C_TRANS_STATUS      0x50
#define HSI2C_TIMING_HS1        0x54
#define HSI2C_TIMING_HS2        0x58
#define HSI2C_TIMING_HS3        0x5C
#define HSI2C_TIMING_FS1        0x60
#define HSI2C_TIMING_FS2        0x64
#define HSI2C_TIMING_FS3        0x68
#define HSI2C_TIMING_SLA        0x6C
// Exynos5/7 HSI2C target address register.  The previous 0x74 offset is a
// reserved word on this controller; the on-device diagnostic consequently
// showed ADDR=00000000 while the transfer stalled in address state 6.
#define HSI2C_ADDRESS           0x70

#define HSI2C_MASTER            BIT3
#define HSI2C_RXCHON            BIT6
#define HSI2C_TXCHON            BIT7
#define HSI2C_SW_RST            BIT31

#define HSI2C_TRAILING_COUNT    0x00FFFFFF
#define HSI2C_AUTO_MODE         BIT31
#define HSI2C_TIMEOUT_EN        BIT31

#define HSI2C_RXFIFO_EN         BIT0
#define HSI2C_TXFIFO_EN         BIT1
#define HSI2C_RXFIFO_LEVEL(x)   ((x) << 4)
#define HSI2C_TXFIFO_LEVEL(x)   ((x) << 16)

#define HSI2C_READ_WRITE        BIT16
#define HSI2C_STOP_AFTER_TRANS  BIT17
#define HSI2C_MASTER_RUN        BIT31

#define HSI2C_INT_TRANSFER_DONE BIT7

#define HSI2C_TX_FIFO_LEVEL(x)  ((x) & 0x7F)
#define HSI2C_TX_FIFO_EMPTY     BIT8
#define HSI2C_RX_FIFO_EMPTY     BIT24

STATIC BOOLEAN     mInitialized;
STATIC EFI_GPIO_PROTOCOL  *mGpio;
STATIC UINT64      mTransferCount;
STATIC UINT64      mTransferSuccessCount;
STATIC UINT64      mTransferFailureCount;
STATIC EFI_STATUS  mLastTransferStatus = EFI_NOT_READY;
STATIC UINT8       mLastCommand;
STATIC UINT8       mLastReadLength;
STATIC UINT8       mLastRead[B2L_TOUCH_DIAGNOSTIC_READ_MAX];
STATIC UINT32      mLastIntStatusRaw;
STATIC UINT32      mLastErrorStatusRaw;
STATIC UINT32      mLastTransferStatusRaw;
STATIC UINT32      mLastFifoStatusRaw;
STATIC EFI_STATUS  mInitialSclStatus = EFI_NOT_READY;
STATIC EFI_STATUS  mInitialSdaStatus = EFI_NOT_READY;
STATIC BOOLEAN     mInitialSclHigh;
STATIC BOOLEAN     mInitialSdaHigh;

STATIC
UINTN
Reg (
  IN UINTN  Offset
  )
{
  return B2L_HSI2C_BASE + Offset;
}

STATIC
VOID
CaptureHardwareStatus (
  VOID
  )
{
  mLastIntStatusRaw      = MmioRead32 (Reg (HSI2C_INT_STATUS));
  mLastErrorStatusRaw    = MmioRead32 (Reg (HSI2C_ERR_STATUS));
  mLastTransferStatusRaw = MmioRead32 (Reg (HSI2C_TRANS_STATUS));
  mLastFifoStatusRaw     = MmioRead32 (Reg (HSI2C_FIFO_STATUS));
}

STATIC
VOID
CaptureInitialLineState (
  VOID
  )
{
  if (mGpio == NULL) {
    mInitialSclStatus = EFI_NOT_FOUND;
    mInitialSdaStatus = EFI_NOT_FOUND;
    return;
  }

  mInitialSclStatus = mGpio->GetState (
                              BANK_ID_P,
                              1,
                              4,
                              &mInitialSclHigh
                              );
  mInitialSdaStatus = mGpio->GetState (
                              BANK_ID_P,
                              1,
                              5,
                              &mInitialSdaHigh
                              );
}

STATIC
VOID
ResetController (
  VOID
  )
{
  MmioOr32  (Reg (HSI2C_CTRL), HSI2C_SW_RST);
  MmioAnd32 (Reg (HSI2C_CTRL), ~HSI2C_SW_RST);

  MmioWrite32 (Reg (HSI2C_CTRL), HSI2C_MASTER);
  MmioWrite32 (Reg (HSI2C_TRAIL_CTRL), HSI2C_TRAILING_COUNT);
  MmioOr32    (Reg (HSI2C_CONF), HSI2C_AUTO_MODE);
}

VOID
B2LHsI2cQuiesce (
  VOID
  )
{
  UINT32  InterruptStatus;

  // This driver polls transfer status.  It must never leave an interrupt or
  // an active channel behind for Windows to inherit at ExitBootServices.
  MmioWrite32 (Reg (HSI2C_INT_ENABLE), 0);

  InterruptStatus = MmioRead32 (Reg (HSI2C_INT_STATUS));
  if (InterruptStatus != 0) {
    MmioWrite32 (Reg (HSI2C_INT_STATUS), InterruptStatus);
  }

  MmioAnd32 (Reg (HSI2C_AUTO_CONF), ~HSI2C_MASTER_RUN);
  MmioAnd32 (Reg (HSI2C_CTRL), ~(HSI2C_RXCHON | HSI2C_TXCHON));
}

VOID
B2LHsI2cPopulateDiagnostic (
  IN OUT B2L_TOUCH_DIAGNOSTIC_DATA  *Data
  )
{
  UINTN  Index;

  if (Data == NULL) {
    return;
  }

  Data->Bus                  = B2L_HSI2C_BUS;
  Data->Slave                = B2L_HSI2C_SLAVE;
  Data->ControllerAddress    = B2L_HSI2C_BASE;
  Data->UsiAddress           = B2L_USI_CONFIG_REGISTER;
  Data->LastCommand          = mLastCommand;
  Data->LastReadLength       = mLastReadLength;
  Data->LastTransferStatus   = mLastTransferStatus;
  Data->TransferCount        = mTransferCount;
  Data->TransferSuccessCount = mTransferSuccessCount;
  Data->TransferFailureCount = mTransferFailureCount;
  Data->LastIntStatusRaw     = mLastIntStatusRaw;
  Data->LastErrorStatusRaw   = mLastErrorStatusRaw;
  Data->LastTransferStatusRaw = mLastTransferStatusRaw;
  Data->LastFifoStatusRaw    = mLastFifoStatusRaw;
  Data->SclStatus            = EFI_NOT_READY;
  Data->SdaStatus            = EFI_NOT_READY;
  Data->InitialSclStatus     = mInitialSclStatus;
  Data->InitialSdaStatus     = mInitialSdaStatus;
  Data->InitialSclHigh       = mInitialSclHigh;
  Data->InitialSdaHigh       = mInitialSdaHigh;
  for (Index = 0; Index < B2L_TOUCH_DIAGNOSTIC_READ_MAX; Index++) {
    Data->LastRead[Index] = mLastRead[Index];
  }

  if (!mInitialized) {
    return;
  }

  Data->UsiMode        = MmioRead32 (B2L_USI_CONFIG_REGISTER);
  Data->Ctrl           = MmioRead32 (Reg (HSI2C_CTRL));
  Data->FifoCtrl       = MmioRead32 (Reg (HSI2C_FIFO_CTRL));
  Data->ClockControl   = MmioRead32 (Reg (HSI2C_CLK_CTRL));
  Data->ClockSlot      = MmioRead32 (Reg (HSI2C_CLK_SLOT));
  Data->IntEnable      = MmioRead32 (Reg (HSI2C_INT_ENABLE));
  Data->IntStatus      = MmioRead32 (Reg (HSI2C_INT_STATUS));
  Data->ErrorStatus    = MmioRead32 (Reg (HSI2C_ERR_STATUS));
  Data->FifoStatus     = MmioRead32 (Reg (HSI2C_FIFO_STATUS));
  Data->Conf           = MmioRead32 (Reg (HSI2C_CONF));
  Data->AutoConf       = MmioRead32 (Reg (HSI2C_AUTO_CONF));
  Data->Command        = MmioRead32 (Reg (HSI2C_CMD));
  Data->TransferStatus = MmioRead32 (Reg (HSI2C_TRANS_STATUS));
  Data->Address        = MmioRead32 (Reg (HSI2C_ADDRESS));
  Data->TimingHs1      = MmioRead32 (Reg (HSI2C_TIMING_HS1));
  Data->TimingHs2      = MmioRead32 (Reg (HSI2C_TIMING_HS2));
  Data->TimingHs3      = MmioRead32 (Reg (HSI2C_TIMING_HS3));
  Data->TimingFs1      = MmioRead32 (Reg (HSI2C_TIMING_FS1));
  Data->TimingFs2      = MmioRead32 (Reg (HSI2C_TIMING_FS2));
  Data->TimingFs3      = MmioRead32 (Reg (HSI2C_TIMING_FS3));
  Data->TimingSla      = MmioRead32 (Reg (HSI2C_TIMING_SLA));

  Data->SclStatus = EFI_NOT_FOUND;
  Data->SdaStatus = EFI_NOT_FOUND;
  if (mGpio != NULL) {
    Data->SclStatus = mGpio->GetState (
                                BANK_ID_P,
                                1,
                                4,
                                &Data->SclHigh
                                );
    Data->SdaStatus = mGpio->GetState (
                                BANK_ID_P,
                                1,
                                5,
                                &Data->SdaHigh
                                );
  }
}

STATIC
EFI_STATUS
TransferMessage (
  IN OUT UINT8    *Buffer,
  IN     UINTN     BufferLength,
  IN     BOOLEAN   IsRead,
  IN     BOOLEAN   Stop
  )
{
  UINT32  AutoConfig;
  UINT32  FifoControl;
  UINT32  FifoStatus;
  UINT32  InterruptStatus;
  UINT32  TargetAddress;
  UINT32  TriggerLevel;
  UINTN   Index;
  UINTN   Timeout;

  if ((Buffer == NULL) || (BufferLength == 0) || (BufferLength > MAX_UINT16)) {
    return EFI_INVALID_PARAMETER;
  }

  MmioAnd32 (Reg (HSI2C_TIMEOUT), ~HSI2C_TIMEOUT_EN);

  TriggerLevel = (BufferLength >= 8) ? 8 : (UINT32)BufferLength;
  FifoControl  = HSI2C_RXFIFO_EN                   |
                 HSI2C_TXFIFO_EN                   |
                 HSI2C_RXFIFO_LEVEL (TriggerLevel) |
                 HSI2C_TXFIFO_LEVEL (TriggerLevel);
  MmioWrite32 (Reg (HSI2C_FIFO_CTRL), FifoControl);

  MmioAndThenOr32 (
    Reg (HSI2C_CTRL),
    IsRead ? ~HSI2C_TXCHON : ~HSI2C_RXCHON,
    IsRead ? HSI2C_RXCHON : HSI2C_TXCHON
    );

  TargetAddress  = MmioRead32 (Reg (HSI2C_ADDRESS));
  TargetAddress &= ~((0x3FFU << 10) | (0x3FFU << 0) | (0xFFU << 24));
  TargetAddress |= (B2L_HSI2C_SLAVE & 0x7F) << 10;
  MmioWrite32 (Reg (HSI2C_ADDRESS), TargetAddress);

  AutoConfig  = MmioRead32 (Reg (HSI2C_AUTO_CONF));
  AutoConfig &= ~(HSI2C_READ_WRITE | HSI2C_STOP_AFTER_TRANS |
                  HSI2C_MASTER_RUN | 0xFFFFU);
  AutoConfig |= IsRead ? HSI2C_READ_WRITE : 0;
  AutoConfig |= Stop ? HSI2C_STOP_AFTER_TRANS : 0;
  AutoConfig |= (UINT32)BufferLength;
  MmioWrite32 (Reg (HSI2C_AUTO_CONF), AutoConfig);

  InterruptStatus = MmioRead32 (Reg (HSI2C_INT_STATUS));
  if (InterruptStatus != 0) {
    MmioWrite32 (Reg (HSI2C_INT_STATUS), InterruptStatus);
  }

  // Match SamsungPkg's HsI2cDxe and the DT check-transdone-int behavior.
  // Enable only TRANSFER_DONE while this bounded transfer is active; every
  // success and failure path clears both the source and this enable again.
  MmioWrite32 (Reg (HSI2C_INT_ENABLE), HSI2C_INT_TRANSFER_DONE);
  MmioOr32 (Reg (HSI2C_AUTO_CONF), HSI2C_MASTER_RUN);

  Index   = 0;
  Timeout = B2L_HSI2C_TIMEOUT_US;

  if (IsRead) {
    while (Timeout != 0) {
      Timeout--;
      if ((Index < BufferLength) &&
          ((MmioRead32 (Reg (HSI2C_FIFO_STATUS)) & HSI2C_RX_FIFO_EMPTY) == 0)) {
        Buffer[Index++] = (UINT8)MmioRead32 (Reg (HSI2C_RX_DATA));
      }

      if (Index >= BufferLength) {
        InterruptStatus = MmioRead32 (Reg (HSI2C_INT_STATUS));
        if ((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0) {
          CaptureHardwareStatus ();
          MmioWrite32 (Reg (HSI2C_INT_STATUS), InterruptStatus);
          MmioWrite32 (Reg (HSI2C_INT_ENABLE), 0);
          return EFI_SUCCESS;
        }
      }

      gBS->Stall (1);
    }
  } else {
    while ((Timeout != 0) && (Index < BufferLength)) {
      FifoStatus = MmioRead32 (Reg (HSI2C_FIFO_STATUS));
      if (HSI2C_TX_FIFO_LEVEL (FifoStatus) < 16) {
        MmioWrite32 (Reg (HSI2C_TX_DATA), Buffer[Index++]);
      }

      Timeout--;
      gBS->Stall (1);
    }

    while (Timeout != 0) {
      Timeout--;
      InterruptStatus = MmioRead32 (Reg (HSI2C_INT_STATUS));
      FifoStatus      = MmioRead32 (Reg (HSI2C_FIFO_STATUS));
      if (((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0) &&
          ((FifoStatus & HSI2C_TX_FIFO_EMPTY) != 0)) {
        CaptureHardwareStatus ();
        MmioWrite32 (Reg (HSI2C_INT_STATUS), InterruptStatus);
        MmioWrite32 (Reg (HSI2C_INT_ENABLE), 0);
        return EFI_SUCCESS;
      }

      gBS->Stall (1);
    }
  }

  CaptureHardwareStatus ();
  // A timed-out Exynos transfer can keep the pin function driving the bus.
  // Reset only this already mapped controller so SCL/SDA are released before
  // the diagnostic screen and before Windows inherits the hardware.
  ResetController ();
  B2LHsI2cQuiesce ();
  return EFI_TIMEOUT;
}

EFI_STATUS
B2LHsI2cInitialize (
  VOID
  )
{
  EFI_HSI2C_PROTOCOL  *HsI2c;
  EFI_STATUS           Status;

  // Optional read-only line-state diagnostics.  HsI2cDxe still owns pin
  // setup; failure to locate GPIO must not block touch initialization.
  Status = gBS->LocateProtocol (
                  &gEfiGpioProtocolGuid,
                  NULL,
                  (VOID **)&mGpio
                  );
  if (EFI_ERROR (Status)) {
    mGpio = NULL;
  }

  Status = gBS->LocateProtocol (
                  &gEfiHsI2cProtocolGuid,
                  NULL,
                  (VOID **)&HsI2c
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // HsI2cDxe maps the HSI2C controller, then UsiDxe selects its mode through
  // the separate USI configuration register at 0x1042101C.  Map only that
  // device-MMIO page before InitBus; it is not DRAM and must not be added to
  // the platform's reserved-memory map.
  //
  Status = MapMemoryRegion (
             B2L_USI_CONFIG_BASE,
             B2L_USI_CONFIG_SIZE,
             EfiMemoryMappedIO
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "B2LTouch: failed to map HSI2C10 USI MMIO page: %r\n",
      Status
      ));
    return Status;
  }

  Status = HsI2c->InitBus (B2L_HSI2C_BUS);
  mInitialized = (BOOLEAN)!EFI_ERROR (Status);
  if (mInitialized) {
    CaptureInitialLineState ();
  }
  return Status;
}

EFI_STATUS
B2LHsI2cWriteRead (
  IN  CONST UINT8  *WriteBuffer OPTIONAL,
  IN  UINTN         WriteLength,
  OUT UINT8        *ReadBuffer OPTIONAL,
  IN  UINTN         ReadLength
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  UINTN       Retry;

  if (((WriteLength == 0) && (ReadLength == 0)) ||
      ((WriteLength != 0) && (WriteBuffer == NULL)) ||
      ((ReadLength != 0) && (ReadBuffer == NULL)) ||
      (WriteLength > MAX_UINT16) || (ReadLength > MAX_UINT16)) {
    return EFI_INVALID_PARAMETER;
  }

  mTransferCount++;
  mLastCommand    = (WriteLength != 0) ? WriteBuffer[0] : 0;
  mLastReadLength = 0;
  for (Index = 0; Index < B2L_TOUCH_DIAGNOSTIC_READ_MAX; Index++) {
    mLastRead[Index] = 0;
  }

  for (Retry = 0; Retry < B2L_HSI2C_RETRIES; Retry++) {
    // The stock DT explicitly requests reset-before-trans for HSI2C10.
    ResetController ();

    if (WriteLength != 0) {
      Status = TransferMessage (
                 (UINT8 *)(VOID *)WriteBuffer,
                 WriteLength,
                 FALSE,
                 TRUE
                 );
      if (EFI_ERROR (Status)) {
        gBS->Stall (100);
        continue;
      }
    }

    if (ReadLength != 0) {
      Status = TransferMessage (ReadBuffer, ReadLength, TRUE, TRUE);
      if (EFI_ERROR (Status)) {
        gBS->Stall (100);
        continue;
      }
    }

    mLastTransferStatus = EFI_SUCCESS;
    mTransferSuccessCount++;
    mLastReadLength = (UINT8)((ReadLength < B2L_TOUCH_DIAGNOSTIC_READ_MAX) ?
                             ReadLength : B2L_TOUCH_DIAGNOSTIC_READ_MAX);
    for (Index = 0; Index < mLastReadLength; Index++) {
      mLastRead[Index] = ReadBuffer[Index];
    }
    return EFI_SUCCESS;
  }

  B2LHsI2cQuiesce ();
  mLastTransferStatus = EFI_DEVICE_ERROR;
  mTransferFailureCount++;
  DEBUG ((DEBUG_ERROR, "B2LTouch: HSI2C10 transfer failed: %r\n", Status));
  return EFI_DEVICE_ERROR;
}
