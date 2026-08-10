/**
 * Exynos 9820 (S5E9820) UFS board init.
 *
 * Tum adresler Samsung stock firmware G975FXXSGHWC1'in dt.img/dtbo.img
 * DTB'sinden ve LineageOS exynos9820 kernel kaynagindan dogrulandi.
 * Yapi S5E9830Pkg/Library/PlatformUfsLib/BoardInit.c'den uyarlanmistir.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/UfsHostBridge.h>

#include <Protocol/EFIGpio.h>

#define WARM_RESET                     (1U << 28)
#define LITTLE_WDT_RESET               (1U << 24)
#define EXYNOS9820_EDPCSR_DUMP_EN      (1U << 0)

//
// UNIPRO clock.
//
// DUZELTME: Once cal-if/exynos9820/cmucal-vclk.c icindeki
//   cmucal_vclk_clkcmu_fsys1_ufs_embd_lut[] = { {177667, ...} }  // kHz
// degerine bakip 177.667 MHz varsaymistik. YANLIS.
//
// Cihaz soyledi: UfsVsSet1usToCnt() icindeki okuma-dogrulama
//   "UFS: 1US_TO_CNT bootloader=166 beklenen=177"
// yazdi. sboot HCI_1US_TO_CNT_VAL'e 166 programlamis, yani gercek UNIPRO
// clock'u 166 MHz. (9830 portu da 166000000 kullaniyor.)
//
// LUT'taki 177667 muhtemelen bolucu oncesi CMU cikisi; UNIPRO'ya giren deger
// degil. Donanimdan okunan sayi kaynaktan cikarilan tahminden ustundur.
//
// Bu deger CAL tablolarindaki tum mclk-turevli zamanlamalari etkiliyor:
//   UNIPRO_MCLK_PERIOD    1e9/166e6 = 6   (177 ile 5 cikiyordu - yanlisti)
//   UNIPRO18_MCLK_PERIOD  16e9/166e6 = 96 (177 ile 90)
//
#define UFS_SCLK                       166000000UL

#define CNT_VAL_1US_MASK               0x3FFU
// DIKKAT: bu offsetler VsAddr'a degil IoAddr'a goredir (VS base = IoAddr + 0x1100).
#define UFSHCI_VS_1US_TO_CNT_VAL       0x110CU
#define UFSHCI_VS_UFSHCI_V2P1_CTRL     0x118CU
#define IA_TICK_SEL                    (1U << 16)

//
// DTB: ufs@0x13D60000
//   reg = <0x13d60000 0x200   HCI (standart UFSHCI)
//          0x13d61100 0x200   VS_HCI (Samsung vendor)
//          0x13d50000 0x8000  UNIPRO
//          0x13d70000 0x100>  UFSP (protector, kullanmiyoruz)
//   ufs-phy         reg = <0x13d64000 0x800>
//   ufs-io-coherency reg = <0x13c10700 0x4> mask/bits = 0xC00000
//
// DIKKAT: 9830'da UNIPRO = UFS_BASE + 0x8000 idi. 9820'de degil —
// ayri bir tabanda (0x13D50000). Formul kopyalanirsa link hic kurulmaz.
//
#define EXYNOS9820_UFS_BASE            0x13D60000
#define EXYNOS9820_UFS_VS_BASE         (EXYNOS9820_UFS_BASE + 0x1100)
#define EXYNOS9820_UNIPRO_BASE         0x13D50000
#define EXYNOS9820_PHY_PMA_BASE        (EXYNOS9820_UFS_BASE + 0x4000)

// DTB: system-controller@15860000. Offset 0x724, exynos9810 ve 9830 ile ayni.
#define EXYNOS9820_PMU_BASE            0x15860000
#define EXYNOS9820_PMU_RST_STAT        (EXYNOS9820_PMU_BASE + 0x404)
#define EXYNOS9820_PMU_SEQUENCER       (EXYNOS9820_PMU_BASE + 0x500)
#define EXYNOS9820_PMU_UFS_PHY_CONTROL (EXYNOS9820_PMU_BASE + 0x724)

//
// UFS vcc: DTB fixedregulator@0 "ufs-vcc", gpio = <&gpd0 2 0>, active high.
// gpd0 -> GpioLib tablosunda controller 0x10830000, offset 0xA0. DAT = CON + 4.
//
#define EXYNOS9820_GPD0_CON            (0x10830000 + 0xA0)
#define EXYNOS9820_GPD0_DAT            (EXYNOS9820_GPD0_CON + 0x4)
#define EXYNOS9820_UFS_VCC_PIN         2

// DTB: ufs-io-coherency
#define EXYNOS9820_UFS_IO_COHERENCY    0x13C10700
#define EXYNOS9820_IO_COHERENCY_BITS   0x00C00000   /* BIT22 | BIT23 */

STATIC EFI_GPIO_PROTOCOL *mGpioProtocol;

STATIC
VOID
UfsVsSet1usToCnt (struct UfsHost *Ufs)
{
  UINT32 nVal;
  UINT32 ReadBack;

  nVal  = MmioRead32 ((UINTN)(Ufs->IoAddr + UFSHCI_VS_UFSHCI_V2P1_CTRL));
  nVal |= IA_TICK_SEL;
  MmioWrite32 ((UINTN)(Ufs->IoAddr + UFSHCI_VS_UFSHCI_V2P1_CTRL), nVal);

  //
  // sboot UFS'i zaten baslatip boot.img'i okudu, yani bu register onun
  // ayarladigi degeri tutuyor. Yazmadan once okuyup karsilastiriyoruz:
  // beklenen 177 (0xB1). Farkli cikarsa CAL tablolarindaki tum
  // mclk-turevli degerler yanlis olur ve bunu link kurulmadan once bilmek isteriz.
  //
  ReadBack = MmioRead32 ((UINTN)(Ufs->IoAddr + UFSHCI_VS_1US_TO_CNT_VAL)) & CNT_VAL_1US_MASK;
  DEBUG ((EFI_D_INFO, "UFS: 1US_TO_CNT bootloader=%d beklenen=%d\n",
          ReadBack, (UFS_SCLK / 1000000)));

  MmioWrite32 ((UINTN)(Ufs->IoAddr + UFSHCI_VS_1US_TO_CNT_VAL),
               (UFS_SCLK / 1000000) & CNT_VAL_1US_MASK);
}

EFI_STATUS
UfsBoardInit (struct UfsHost *Ufs)
{
  UINT32     Register;
  UINT32     RstStat;
  UINT32     DfdEn;
  EFI_STATUS Status;

  RstStat = MmioRead32 (EXYNOS9820_PMU_RST_STAT);
  DfdEn   = MmioRead32 (EXYNOS9820_PMU_SEQUENCER);

  //
  // SIFIRLAMA SEBEBI - donanimsiz tani kanali.
  //
  // PMU'nun RST_STAT registeri son sifirlamanin KAYNAGINI tutuyor ve
  // sifirlamayi asiyor. Windows cokup cihaz yeniden baslayinca, bir SONRAKI
  // aciliste burasi bize neden sifirlandigini soyler:
  //
  //   WARM_RESET (bit 28)       -> yazilim sicak sifirlama istedi
  //                                (Windows'un ResetSystem cagrisi bu)
  //   LITTLE_WDT_RESET (bit 24) -> kucuk cekirdek watchdog'u atti
  //                                (yazilim takildi, donanim mudahale etti)
  //   ikisi de yoksa            -> soguk acilis, yani guc kesildi ya da
  //                                cok daha sert bir sey oldu
  //
  // Ayrim onemli: Windows'un kendi istedigi bir reset ile donanimin zorla
  // attigi bir reset tamamen farkli yerlere bakmayi gerektiriyor.
  //
  DEBUG ((EFI_D_ERROR, "B2L RESET: RST_STAT=0x%08x  SEQ=0x%08x\n", RstStat, DfdEn));
  DEBUG ((EFI_D_ERROR, "B2L RESET:   WARM(28)=%d  LITTLE_WDT(24)=%d\n",
          (RstStat & WARM_RESET) ? 1 : 0,
          (RstStat & LITTLE_WDT_RESET) ? 1 : 0));

  Status = gBS->LocateProtocol (&gEfiGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate GPIO Protocol! Status = %r\n", Status));
    return Status;
  }

  DEBUG ((EFI_D_INFO, "UFS: Board init (Exynos 9820 / beyond2lte)\n"));

  /* UFS blok adresleri */
  Ufs->IoAddr     = (VOID *)(UINTN)EXYNOS9820_UFS_BASE;
  Ufs->VsAddr     = (VOID *)(UINTN)EXYNOS9820_UFS_VS_BASE;
  Ufs->UniProAddr = (VOID *)(UINTN)EXYNOS9820_UNIPRO_BASE;
  Ufs->PhyPma     = (VOID *)(UINTN)EXYNOS9820_PHY_PMA_BASE;

  /* Guc ve PHY isolation */
  Ufs->DevPwrAddr  = (VOID *)(UINTN)EXYNOS9820_GPD0_DAT;
  Ufs->DevPwrShift = EXYNOS9820_UFS_VCC_PIN;
  Ufs->PhyIsoAddr  = (VOID *)(UINTN)EXYNOS9820_PMU_UFS_PHY_CONTROL;

  /* DTB: ufs,pmd-attr-gear = [03], ufs,pmd-attr-lane = [02] */
  Ufs->MclkRate = UFS_SCLK;
  Ufs->GearMode = 3;

  //
  // 9830'daki UfsSetUniProClk() burada yok: 9820 icin CMU mux/div register
  // offsetlerini cikaramadik. sboot UFS'ten boot.img okudugu icin clock zaten
  // programlanmis geliyor. UfsVsSet1usToCnt() icindeki okuma-dogrulama bunun
  // gecerli olup olmadigini soyleyecek.
  //
  UfsVsSet1usToCnt (Ufs);

  //
  // DTB pinctrl:
  //   ufs-rst-n       samsung,pins = "gpf3-1", pin-function = 2, pud = 0
  //   ufs-refclk-out  samsung,pins = "gpf3-0", pin-function = 2, pud = 0
  // (9830'da gpf2 idi.)
  //
  Status = mGpioProtocol->SetPull (BANK_ID_F, 3, 0, PULL_NONE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to set GPIO pull for REFCLK! Status = %r\n", Status));
    return Status;
  }

  Status = mGpioProtocol->SetPull (BANK_ID_F, 3, 1, PULL_NONE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to set GPIO pull for RST_N! Status = %r\n", Status));
    return Status;
  }

  Status = mGpioProtocol->SetFunction (BANK_ID_F, 3, 0, FUNCTION_2);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to configure GPIO pin for REFCLK! Status = %r\n", Status));
    return Status;
  }

  Status = mGpioProtocol->SetFunction (BANK_ID_F, 3, 1, FUNCTION_2);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to configure GPIO pin for RST_N! Status = %r\n", Status));
    return Status;
  }

  /* ufs-vcc enable pini cikis yap (gpd0-2) */
  Status = mGpioProtocol->SetFunction (BANK_ID_D, 0, EXYNOS9820_UFS_VCC_PIN, FUNCTION_OUTPUT);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to configure GPD0-%d! Status = %r\n",
            EXYNOS9820_UFS_VCC_PIN, Status));
    return Status;
  }

  //
  // SYSREG IO coherency. DTB ufs-io-coherency node'u adresi ve maskeyi
  // dogrudan veriyor: reg = 0x13C10700, mask = bits = 0xC00000.
  // Warm/watchdog reset + DFD dump aktifse atlaniyor (9830 ile ayni mantik).
  //
  if (!((RstStat & (WARM_RESET | LITTLE_WDT_RESET)) && (DfdEn & EXYNOS9820_EDPCSR_DUMP_EN))) {
    Register  = MmioRead32 (EXYNOS9820_UFS_IO_COHERENCY);
    Register |= EXYNOS9820_IO_COHERENCY_BITS;
    MmioWrite32 (EXYNOS9820_UFS_IO_COHERENCY, Register);
  }

  return EFI_SUCCESS;
}
