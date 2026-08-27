#include <Uefi.h>

#include <Library/HsI2cLib.h>

//
// HSI2C4..10 share the ALIVE/PERIC GPIO block described by the stock DT.  Bus
// 10 is the enabled controller used by beyond2lte sec_ts; the preceding exact
// entries keep the protocol's bus-number indexing compatible with UsiLib.
//
STATIC
EFI_HSI2C_BUS_DATA
gHsI2cBusData[11] = {
  [4] = {
    .Gpio = {
      .Function   = FUNCTION_2,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 0,
      .SdaBankNum = 0,
      .SclPin     = 0,
      .SdaPin     = 1
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  },

  [5] = {
    .Gpio = {
      .Function   = FUNCTION_3,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 0,
      .SdaBankNum = 0,
      .SclPin     = 2,
      .SdaPin     = 3
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  },

  [6] = {
    .Gpio = {
      .Function   = FUNCTION_2,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 0,
      .SdaBankNum = 0,
      .SclPin     = 4,
      .SdaPin     = 5
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  },

  [7] = {
    .Gpio = {
      .Function   = FUNCTION_3,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 0,
      .SdaBankNum = 0,
      .SclPin     = 6,
      .SdaPin     = 7
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  },

  [8] = {
    .Gpio = {
      .Function   = FUNCTION_2,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 1,
      .SdaBankNum = 1,
      .SclPin     = 0,
      .SdaPin     = 1
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  },

  [9] = {
    .Gpio = {
      .Function   = FUNCTION_3,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 1,
      .SdaBankNum = 1,
      .SclPin     = 2,
      .SdaPin     = 3
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  },

  [10] = {
    .Gpio = {
      .Function   = FUNCTION_2,
      .SclBankId  = BANK_ID_P,
      .SdaBankId  = BANK_ID_P,
      .SclBankNum = 1,
      .SdaBankNum = 1,
      .SclPin     = 4,
      .SdaPin     = 5
    },
    .Clock = {
      .Freq      = 200000000,
      .SpeedMode = FAST_SPD
    },
    .Initialized = FALSE
  }
};

VOID
GetHsI2cBusData (
  OUT EFI_HSI2C_BUS_DATA **Data,
  OUT UINT8               *Count
  )
{
  *Data  = gHsI2cBusData;
  *Count = ARRAY_SIZE (gHsI2cBusData);
}
