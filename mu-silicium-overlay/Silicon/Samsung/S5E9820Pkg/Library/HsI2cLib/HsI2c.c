#include <Uefi.h>

#include <Library/HsI2cLib.h>

//
// HSI2C4..10 share the ALIVE/PERIC GPIO block described by the stock DT.  Bus
// 10 is the enabled controller used by beyond2lte sec_ts.  HSI2C45 is the
// CMGP03 controller used by the stock MAX77705 node.  Sparse entries preserve
// the SoC bus numbering expected by UsiLib.
//
STATIC
EFI_HSI2C_BUS_DATA
gHsI2cBusData[46] = {
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
  },

  // Stock exynos9820-pinctrl.dtsi: hsi2c45_bus = GPM12-0/GPM13-0,
  // pin-function 2.  The controller input clock is 200 MHz and the Linux
  // node uses the normal fast-speed HSI2C timing profile.
  [45] = {
    .Gpio = {
      .Function   = FUNCTION_2,
      .SclBankId  = BANK_ID_M,
      .SdaBankId  = BANK_ID_M,
      .SclBankNum = 12,
      .SdaBankNum = 13,
      .SclPin     = 0,
      .SdaPin     = 0
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
