/** @file
  Staged MAX77705 diagnostics for beyond2lte HSI2C45.

  The GPIO and controller pages are passive.  The data page performs only
  targeted reads from the MAX77705 PMIC/charger (7-bit address 0x66) and its
  fuel-gauge function (7-bit address 0x36).  The one-byte I2C writes select a
  register address; no MAX77705 configuration data, role command or reset is
  written.
**/

#include <Uefi.h>

#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EFIGpio.h>

#include "B2LMax77705.h"

#define B2L_HSI2C45_BASE           0x15D60000UL
#define B2L_MAX77705_PMIC_ADDRESS  0x66
#define B2L_MAX77705_FG_ADDRESS    0x36
#define B2L_HSI2C_TIMEOUT_US       20000

#define HSI2C_CTRL                 0x00
#define HSI2C_FIFO_CTRL            0x04
#define HSI2C_CLK_CTRL             0x0C
#define HSI2C_INT_ENABLE           0x20
#define HSI2C_INT_STATUS           0x24
#define HSI2C_ERR_STATUS           0x2C
#define HSI2C_FIFO_STATUS          0x30
#define HSI2C_TX_DATA              0x34
#define HSI2C_RX_DATA              0x38
#define HSI2C_CONF                 0x40
#define HSI2C_AUTO_CONF            0x44
#define HSI2C_TIMEOUT              0x48
#define HSI2C_TRANS_STATUS         0x50
#define HSI2C_TIMING_FS1           0x60
#define HSI2C_TIMING_FS2           0x64
#define HSI2C_TIMING_FS3           0x68
#define HSI2C_ADDRESS              0x70

#define HSI2C_MASTER               BIT3
#define HSI2C_RXCHON               BIT6
#define HSI2C_TXCHON               BIT7
#define HSI2C_AUTO_MODE            BIT31
#define HSI2C_TIMEOUT_EN           BIT31
#define HSI2C_RXFIFO_EN            BIT0
#define HSI2C_TXFIFO_EN            BIT1
#define HSI2C_RXFIFO_LEVEL(x)      ((x) << 4)
#define HSI2C_TXFIFO_LEVEL(x)      ((x) << 16)
#define HSI2C_READ_WRITE           BIT16
#define HSI2C_STOP_AFTER_TRANS     BIT17
#define HSI2C_MASTER_RUN           BIT31
#define HSI2C_INT_TRANSFER_DONE    BIT7
#define HSI2C_TX_FIFO_LEVEL(x)     ((x) & 0x7F)
#define HSI2C_TX_FIFO_EMPTY        BIT8
#define HSI2C_RX_FIFO_EMPTY        BIT24

#define MAX77705_PMIC_ID1          0x00
#define MAX77705_PMIC_REV          0x01
#define MAX77705_CHG_INT_OK        0xB2
#define MAX77705_CHG_DETAILS_00    0xB3
#define MAX77705_CHG_DETAILS_01    0xB4
#define MAX77705_CHG_DETAILS_02    0xB5

#define MAX77705_FG_STATUS         0x00
#define MAX77705_FG_REPSOC         0x06
#define MAX77705_FG_TEMPERATURE    0x08
#define MAX77705_FG_VCELL          0x09
#define MAX77705_FG_CURRENT        0x0A
#define MAX77705_FG_AVG_CURRENT    0x0B
#define MAX77705_FG_AVG_VCELL      0x19
#define MAX77705_FG_OCV            0xEE
#define MAX77705_FG_BAT_ABSENT     BIT3

typedef struct {
  UINT32  Ctrl;
  UINT32  FifoCtrl;
  UINT32  IntEnable;
  UINT32  Conf;
  UINT32  AutoConf;
  UINT32  Timeout;
  UINT32  Address;
} B2L_HSI2C_SAVED_STATE;

STATIC BOOLEAN  mControllerTransferReady;

STATIC
UINTN
Hsi2cReg (
  IN UINTN  Offset
  )
{
  return B2L_HSI2C45_BASE + Offset;
}

STATIC
VOID
PrintGpioState (
  IN CONST CHAR16  *Name,
  IN EFI_STATUS     Status,
  IN BOOLEAN        High
  )
{
  if (EFI_ERROR (Status)) {
    Print (L"%s: %r\n", Name, Status);
  } else {
    Print (L"%s: RAW=%u (%s)\n", Name, High ? 1 : 0,
           High ? L"HIGH" : L"LOW");
  }
}

VOID
B2LMax77705ShowGpio (
  VOID
  )
{
  EFI_GPIO_PROTOCOL  *Gpio;
  EFI_STATUS          Status;
  EFI_STATUS          IrqStatus;
  EFI_STATUS          SclStatus;
  EFI_STATUS          SdaStatus;
  BOOLEAN             IrqHigh;
  BOOLEAN             SclHigh;
  BOOLEAN             SdaHigh;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - ASAMA 8 MAX77705 GPIO ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"Yalniz EFI GPIO GetState; pin modu veya seviye yazilmaz.\n\n");

  Gpio = NULL;
  Status = gBS->LocateProtocol (
                  &gEfiGpioProtocolGuid,
                  NULL,
                  (VOID **)&Gpio
                  );
  if (EFI_ERROR (Status)) {
    Print (L"EFI GPIO protokolu bulunamadi: %r\n", Status);
    return;
  }

  IrqHigh = FALSE;
  SclHigh = FALSE;
  SdaHigh = FALSE;
  IrqStatus = Gpio->GetState (BANK_ID_A, 1, 6, &IrqHigh);
  SclStatus = Gpio->GetState (BANK_ID_M, 12, 0, &SclHigh);
  SdaStatus = Gpio->GetState (BANK_ID_M, 13, 0, &SdaHigh);

  PrintGpioState (L"MAX77705 IRQ GPA1-6", IrqStatus, IrqHigh);
  PrintGpioState (L"HSI2C45 SCL GPM12-0", SclStatus, SclHigh);
  PrintGpioState (L"HSI2C45 SDA GPM13-0", SdaStatus, SdaHigh);
  if (!EFI_ERROR (IrqStatus)) {
    Print (L"IRQ DT active-low: RAW=0 ise hat asserted olabilir.\n");
  }
  Print (L"Bu sayfada HSI2C MMIO veya I2C transferi yapilmadi.\n");
}

BOOLEAN
B2LMax77705ShowController (
  VOID
  )
{
  UINT32  Ctrl;
  UINT32  Clock;
  UINT32  IntEnable;
  UINT32  IntStatus;
  UINT32  ErrorStatus;
  UINT32  FifoStatus;
  UINT32  Conf;
  UINT32  AutoConf;
  UINT32  TransferStatus;
  UINT32  TimingFs1;
  UINT32  TimingFs2;
  UINT32  TimingFs3;
  UINT32  Address;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - ASAMA 9 HSI2C45 DURUM ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"BASE=15D60000 DT_LEN=1000; yalniz controller okuma.\n");
  Print (L"Her MMIO degeri okunduktan hemen sonra yazdirilir.\n\n");

  Ctrl = MmioRead32 (Hsi2cReg (HSI2C_CTRL));
  Print (L"CTRL       [000]=%08x\n", Ctrl);
  Clock = MmioRead32 (Hsi2cReg (HSI2C_CLK_CTRL));
  Print (L"CLK_CTRL   [00C]=%08x\n", Clock);
  IntEnable = MmioRead32 (Hsi2cReg (HSI2C_INT_ENABLE));
  Print (L"INT_ENABLE [020]=%08x\n", IntEnable);
  IntStatus = MmioRead32 (Hsi2cReg (HSI2C_INT_STATUS));
  Print (L"INT_STATUS [024]=%08x\n", IntStatus);
  ErrorStatus = MmioRead32 (Hsi2cReg (HSI2C_ERR_STATUS));
  Print (L"ERR_STATUS [02C]=%08x\n", ErrorStatus);
  FifoStatus = MmioRead32 (Hsi2cReg (HSI2C_FIFO_STATUS));
  Print (L"FIFO_STAT  [030]=%08x\n", FifoStatus);
  Conf = MmioRead32 (Hsi2cReg (HSI2C_CONF));
  Print (L"CONF       [040]=%08x\n", Conf);
  AutoConf = MmioRead32 (Hsi2cReg (HSI2C_AUTO_CONF));
  Print (L"AUTO_CONF  [044]=%08x\n", AutoConf);
  TransferStatus = MmioRead32 (Hsi2cReg (HSI2C_TRANS_STATUS));
  Print (L"TRANS_STAT [050]=%08x\n", TransferStatus);
  TimingFs1 = MmioRead32 (Hsi2cReg (HSI2C_TIMING_FS1));
  Print (L"TIMING_FS1 [060]=%08x\n", TimingFs1);
  TimingFs2 = MmioRead32 (Hsi2cReg (HSI2C_TIMING_FS2));
  Print (L"TIMING_FS2 [064]=%08x\n", TimingFs2);
  TimingFs3 = MmioRead32 (Hsi2cReg (HSI2C_TIMING_FS3));
  Print (L"TIMING_FS3 [068]=%08x\n", TimingFs3);
  Address = MmioRead32 (Hsi2cReg (HSI2C_ADDRESS));
  Print (L"ADDRESS    [070]=%08x\n", Address);

  mControllerTransferReady =
    (BOOLEAN)((Clock != 0) &&
              ((TimingFs1 | TimingFs2 | TimingFs3) != 0) &&
              ((AutoConf & HSI2C_MASTER_RUN) == 0) &&
              (ErrorStatus == 0) &&
              ((FifoStatus & 0x7F) == 0) &&
              ((FifoStatus & HSI2C_RX_FIFO_EMPTY) != 0));
  Print (L"\nKALITIM CLOCK/TIMING/FIFO=%s  MASTER_RUN=%u\n",
         mControllerTransferReady ? L"UYGUN" : L"UYGUN DEGIL",
         (AutoConf & HSI2C_MASTER_RUN) != 0);
  Print (L"Aktif veri yalniz UYGUN sonucunda sonraki onayla baslar.\n");
  Print (L"Bu sayfada controller veya MAX77705'e yazma yapilmadi.\n");
  return mControllerTransferReady;
}

STATIC
VOID
SaveControllerState (
  OUT B2L_HSI2C_SAVED_STATE  *State
  )
{
  State->Ctrl      = MmioRead32 (Hsi2cReg (HSI2C_CTRL));
  State->FifoCtrl  = MmioRead32 (Hsi2cReg (HSI2C_FIFO_CTRL));
  State->IntEnable = MmioRead32 (Hsi2cReg (HSI2C_INT_ENABLE));
  State->Conf      = MmioRead32 (Hsi2cReg (HSI2C_CONF));
  State->AutoConf  = MmioRead32 (Hsi2cReg (HSI2C_AUTO_CONF));
  State->Timeout   = MmioRead32 (Hsi2cReg (HSI2C_TIMEOUT));
  State->Address   = MmioRead32 (Hsi2cReg (HSI2C_ADDRESS));
}

STATIC
VOID
QuiesceController (
  VOID
  )
{
  UINT32  InterruptStatus;

  MmioWrite32 (Hsi2cReg (HSI2C_INT_ENABLE), 0);
  InterruptStatus = MmioRead32 (Hsi2cReg (HSI2C_INT_STATUS));
  if (InterruptStatus != 0) {
    MmioWrite32 (Hsi2cReg (HSI2C_INT_STATUS), InterruptStatus);
  }
  MmioAnd32 (Hsi2cReg (HSI2C_AUTO_CONF), ~HSI2C_MASTER_RUN);
  MmioAnd32 (Hsi2cReg (HSI2C_CTRL), ~(HSI2C_RXCHON | HSI2C_TXCHON));
}

STATIC
VOID
RestoreControllerState (
  IN CONST B2L_HSI2C_SAVED_STATE  *State
  )
{
  QuiesceController ();
  MmioWrite32 (Hsi2cReg (HSI2C_FIFO_CTRL), State->FifoCtrl);
  MmioWrite32 (Hsi2cReg (HSI2C_TIMEOUT), State->Timeout);
  MmioWrite32 (Hsi2cReg (HSI2C_ADDRESS), State->Address);
  MmioWrite32 (
    Hsi2cReg (HSI2C_AUTO_CONF),
    State->AutoConf & ~HSI2C_MASTER_RUN
    );
  MmioWrite32 (Hsi2cReg (HSI2C_CONF), State->Conf);
  MmioWrite32 (
    Hsi2cReg (HSI2C_CTRL),
    State->Ctrl & ~(HSI2C_RXCHON | HSI2C_TXCHON)
    );
  MmioWrite32 (Hsi2cReg (HSI2C_INT_ENABLE), State->IntEnable);
}

STATIC
EFI_STATUS
TransferMessage (
  IN     UINT8    Slave,
  IN OUT UINT8   *Buffer,
  IN     UINTN    BufferLength,
  IN     BOOLEAN  IsRead
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

  if ((Buffer == NULL) || (BufferLength == 0) || (BufferLength > 16)) {
    return EFI_INVALID_PARAMETER;
  }

  MmioAnd32 (Hsi2cReg (HSI2C_TIMEOUT), ~HSI2C_TIMEOUT_EN);
  MmioOr32 (Hsi2cReg (HSI2C_CTRL), HSI2C_MASTER);
  MmioOr32 (Hsi2cReg (HSI2C_CONF), HSI2C_AUTO_MODE);

  TriggerLevel = (UINT32)BufferLength;
  FifoControl  = HSI2C_RXFIFO_EN                   |
                 HSI2C_TXFIFO_EN                   |
                 HSI2C_RXFIFO_LEVEL (TriggerLevel) |
                 HSI2C_TXFIFO_LEVEL (TriggerLevel);
  MmioWrite32 (Hsi2cReg (HSI2C_FIFO_CTRL), FifoControl);
  MmioAndThenOr32 (
    Hsi2cReg (HSI2C_CTRL),
    IsRead ? ~HSI2C_TXCHON : ~HSI2C_RXCHON,
    IsRead ? HSI2C_RXCHON : HSI2C_TXCHON
    );

  TargetAddress  = MmioRead32 (Hsi2cReg (HSI2C_ADDRESS));
  TargetAddress &= ~((0x3FFU << 10) | (0x3FFU << 0) | (0xFFU << 24));
  TargetAddress |= (Slave & 0x7F) << 10;
  MmioWrite32 (Hsi2cReg (HSI2C_ADDRESS), TargetAddress);

  AutoConfig  = MmioRead32 (Hsi2cReg (HSI2C_AUTO_CONF));
  AutoConfig &= ~(HSI2C_READ_WRITE | HSI2C_STOP_AFTER_TRANS |
                  HSI2C_MASTER_RUN | 0xFFFFU);
  AutoConfig |= IsRead ? HSI2C_READ_WRITE : 0;
  AutoConfig |= HSI2C_STOP_AFTER_TRANS | (UINT32)BufferLength;
  MmioWrite32 (Hsi2cReg (HSI2C_AUTO_CONF), AutoConfig);

  InterruptStatus = MmioRead32 (Hsi2cReg (HSI2C_INT_STATUS));
  if (InterruptStatus != 0) {
    MmioWrite32 (Hsi2cReg (HSI2C_INT_STATUS), InterruptStatus);
  }
  MmioWrite32 (Hsi2cReg (HSI2C_INT_ENABLE), HSI2C_INT_TRANSFER_DONE);

  if (!IsRead) {
    for (Index = 0; Index < BufferLength; Index++) {
      MmioWrite32 (Hsi2cReg (HSI2C_TX_DATA), Buffer[Index]);
    }
  }

  MmioOr32 (Hsi2cReg (HSI2C_AUTO_CONF), HSI2C_MASTER_RUN);
  Index   = 0;
  Timeout = B2L_HSI2C_TIMEOUT_US;
  while (Timeout != 0) {
    Timeout--;
    FifoStatus = MmioRead32 (Hsi2cReg (HSI2C_FIFO_STATUS));
    if (IsRead && (Index < BufferLength) &&
        ((FifoStatus & HSI2C_RX_FIFO_EMPTY) == 0)) {
      Buffer[Index++] = (UINT8)MmioRead32 (Hsi2cReg (HSI2C_RX_DATA));
    }

    InterruptStatus = MmioRead32 (Hsi2cReg (HSI2C_INT_STATUS));
    if ((InterruptStatus & HSI2C_INT_TRANSFER_DONE) != 0) {
      if ((!IsRead && ((FifoStatus & HSI2C_TX_FIFO_EMPTY) != 0)) ||
          (IsRead && (Index == BufferLength))) {
        MmioWrite32 (Hsi2cReg (HSI2C_INT_STATUS), InterruptStatus);
        MmioWrite32 (Hsi2cReg (HSI2C_INT_ENABLE), 0);
        MmioAnd32 (Hsi2cReg (HSI2C_AUTO_CONF), ~HSI2C_MASTER_RUN);
        MmioAnd32 (
          Hsi2cReg (HSI2C_CTRL),
          ~(HSI2C_RXCHON | HSI2C_TXCHON)
          );
        return EFI_SUCCESS;
      }
    }
    gBS->Stall (1);
  }

  QuiesceController ();
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
ReadRegister (
  IN  UINT8   Slave,
  IN  UINT8   Register,
  OUT UINT8  *Buffer,
  IN  UINTN   BufferLength
  )
{
  EFI_STATUS  Status;
  UINT8       RegisterPointer;

  RegisterPointer = Register;
  Status = TransferMessage (Slave, &RegisterPointer, 1, FALSE);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return TransferMessage (Slave, Buffer, BufferLength, TRUE);
}

STATIC
EFI_STATUS
ReadRegister8 (
  IN  UINT8   Slave,
  IN  UINT8   Register,
  OUT UINT8  *Value
  )
{
  return ReadRegister (Slave, Register, Value, 1);
}

STATIC
EFI_STATUS
ReadRegister16 (
  IN  UINT8    Slave,
  IN  UINT8    Register,
  OUT UINT16  *Value
  )
{
  EFI_STATUS  Status;
  UINT8       Data[2];

  Status = ReadRegister (Slave, Register, Data, sizeof (Data));
  if (!EFI_ERROR (Status)) {
    *Value = (UINT16)(Data[0] | (Data[1] << 8));
  }
  return Status;
}

STATIC
BOOLEAN
PrintRead8 (
  IN  CONST CHAR16  *Name,
  IN  UINT8          Slave,
  IN  UINT8          Register,
  OUT UINT8         *Value
  )
{
  EFI_STATUS  Status;

  Status = ReadRegister8 (Slave, Register, Value);
  Print (L"%-11s A=%02x R=%02x: %r", Name, Slave, Register, Status);
  if (!EFI_ERROR (Status)) {
    Print (L" RAW=%02x", *Value);
  }
  Print (L"\n");
  return (BOOLEAN)!EFI_ERROR (Status);
}

STATIC
BOOLEAN
PrintRead16 (
  IN  CONST CHAR16  *Name,
  IN  UINT8          Register,
  OUT UINT16        *Value
  )
{
  EFI_STATUS  Status;

  Status = ReadRegister16 (
             B2L_MAX77705_FG_ADDRESS,
             Register,
             Value
             );
  Print (L"%-11s A=36 R=%02x: %r", Name, Register, Status);
  if (!EFI_ERROR (Status)) {
    Print (L" RAW=%04x", *Value);
  }
  Print (L"\n");
  return (BOOLEAN)!EFI_ERROR (Status);
}

STATIC
UINT64
FuelGaugeVoltageMilliVolts (
  IN UINT16  Raw
  )
{
  return ((UINT64)Raw * 78125ULL) / 1000000ULL;
}

STATIC
VOID
PrintTemperature (
  IN UINT16  Raw
  )
{
  INT32  AbsTenth;
  INT32  Tenth;

  Tenth    = ((INT32)(INT16)Raw * 10) / 256;
  AbsTenth = (Tenth < 0) ? -Tenth : Tenth;
  Print (L"TEMP=%s%d.%d C\n", (Tenth < 0) ? L"-" : L"",
         AbsTenth / 10, AbsTenth % 10);
}

VOID
B2LMax77705ShowData (
  VOID
  )
{
  B2L_HSI2C_SAVED_STATE  Saved;
  UINT8                   PmicId;
  UINT8                   PmicRev;
  UINT8                   ChgIntOk;
  UINT8                   ChgDetails0;
  UINT8                   ChgDetails1;
  UINT8                   ChgDetails2;
  UINT16                  StatusRaw;
  UINT16                  SocRaw;
  UINT16                  TemperatureRaw;
  UINT16                  VcellRaw;
  UINT16                  CurrentRaw;
  UINT16                  AverageCurrentRaw;
  UINT16                  AverageVcellRaw;
  UINT16                  OcvRaw;
  BOOLEAN                 PmicOkay;
  BOOLEAN                 FuelOkay;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"=== B2L USB TANI R%u - ASAMA 10 MAX77705 VERI ===\n",
         B2L_USB_TEST_REVISION);
  Print (L"A66 PMIC/sarj ve A36 fuel-gauge; hedefli salt veri okuma.\n");
  Print (L"I2C write yalniz 1-byte register pointer; config/rol/reset yok.\n\n");

  if (!mControllerTransferReady) {
    Print (L"HSI2C45 clock/timing preflight uygun degil; transfer atlandi.\n");
    return;
  }

  SaveControllerState (&Saved);

  PmicOkay = PrintRead8 (
               L"PMIC_ID1",
               B2L_MAX77705_PMIC_ADDRESS,
               MAX77705_PMIC_ID1,
               &PmicId
               );
  PmicOkay = PrintRead8 (
               L"PMIC_REV",
               B2L_MAX77705_PMIC_ADDRESS,
               MAX77705_PMIC_REV,
               &PmicRev
               ) && PmicOkay;
  if (PmicOkay) {
    PrintRead8 (L"CHG_INT_OK", B2L_MAX77705_PMIC_ADDRESS,
                MAX77705_CHG_INT_OK, &ChgIntOk);
    PrintRead8 (L"CHG_DTLS00", B2L_MAX77705_PMIC_ADDRESS,
                MAX77705_CHG_DETAILS_00, &ChgDetails0);
    PrintRead8 (L"CHG_DTLS01", B2L_MAX77705_PMIC_ADDRESS,
                MAX77705_CHG_DETAILS_01, &ChgDetails1);
    PrintRead8 (L"CHG_DTLS02", B2L_MAX77705_PMIC_ADDRESS,
                MAX77705_CHG_DETAILS_02, &ChgDetails2);
  } else {
    Print (L"A66 kimligi okunamadi; diger A66 registerleri atlandi.\n");
  }

  Print (L"\n");
  FuelOkay = PrintRead16 (L"FG_STATUS", MAX77705_FG_STATUS, &StatusRaw);
  FuelOkay = PrintRead16 (L"FG_REPSOC", MAX77705_FG_REPSOC, &SocRaw) &&
             FuelOkay;
  FuelOkay = PrintRead16 (
               L"FG_TEMP",
               MAX77705_FG_TEMPERATURE,
               &TemperatureRaw
               ) && FuelOkay;
  FuelOkay = PrintRead16 (L"FG_VCELL", MAX77705_FG_VCELL, &VcellRaw) &&
             FuelOkay;
  FuelOkay = PrintRead16 (
               L"FG_CURRENT",
               MAX77705_FG_CURRENT,
               &CurrentRaw
               ) && FuelOkay;
  FuelOkay = PrintRead16 (
               L"FG_AVGCURR",
               MAX77705_FG_AVG_CURRENT,
               &AverageCurrentRaw
               ) && FuelOkay;
  FuelOkay = PrintRead16 (
               L"FG_AVGVCELL",
               MAX77705_FG_AVG_VCELL,
               &AverageVcellRaw
               ) && FuelOkay;
  FuelOkay = PrintRead16 (L"FG_OCV", MAX77705_FG_OCV, &OcvRaw) && FuelOkay;

  if (FuelOkay) {
    Print (L"\nBAT_PRESENT=%s  SOC=%Lu.%Lu %%\n",
           (StatusRaw & MAX77705_FG_BAT_ABSENT) ? L"HAYIR" : L"EVET",
           ((UINT64)SocRaw * 10ULL / 256ULL) / 10ULL,
           ((UINT64)SocRaw * 10ULL / 256ULL) % 10ULL);
    PrintTemperature (TemperatureRaw);
    Print (L"VCELL=%Lu mV AVG=%Lu mV OCV=%Lu mV\n",
           FuelGaugeVoltageMilliVolts (VcellRaw),
           FuelGaugeVoltageMilliVolts (AverageVcellRaw),
           FuelGaugeVoltageMilliVolts (OcvRaw));
    Print (L"CURRENT raw signed=%d AVG raw signed=%d; mA donusumu yok.\n",
           (INT16)CurrentRaw, (INT16)AverageCurrentRaw);
  } else {
    Print (L"\nFuel-gauge serisi tamamlanmadi; donusumler gosterilmedi.\n");
  }

  RestoreControllerState (&Saved);
  Print (L"\nHSI2C45 config durumu geri yuklendi ve kanal quiesce edildi.\n");
  Print (L"MAX77705 config/rol/reset registerine veri yazilmadi.\n");
}
