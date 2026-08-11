#!/usr/bin/env python3
"""
UfsDxe.c'yi PWM Gear 1'e cekmek icin yamalar.

    python3 tools_ufs_pwm.py <Mu-Silicium-yolu>

---------------------------------------------------------------------------
NEDEN
---------------------------------------------------------------------------
Windows'un depolamayi hic gorememesinin sebebi storufs.sys'in Exynos UFS
linkini kuramamasi. storufs jenerik: denetleyiciyi HCE ile resetliyor ve
UniPro'yu standart DME_LINKSTARTUP ile ayaga kaldirmaya calisiyor. Exynos'un
vendor'a ozel kalibrasyonunu bilmiyor.

Ama UniPro spec'i sunu soyluyor: DME_LINKSTARTUP'tan sonra link HER ZAMAN
PWM Gear 1'de olur. Yuksek hiza (HS) gecis ayri bir pazarlik ve vendor
kalibrasyonu tam orada gerekiyor.

Su an UEFI linki HS Gear 3'e cikariyor. storufs devralinca resetliyor ve
kendi jenerik pazarligini yapmaya calisiyor - orada kaybediyoruz.

FIKIR: UEFI linki HIC HS'e cikarmasin, PWM Gear 1'de biraksin. O zaman
storufs'un yapacagi jenerik linkup zaten spec'in varsayilan durumu olur ve
vendor kalibrasyonuna ihtiyac duymaz.

Yavas olur - PWM G1 tek serit ~3 MB/s civari. Onemli degil: amac hiz degil,
Windows'un diski GORMESI. Gorursa hem depolama acilir hem de kendi acilis
kaydini yazacak bir yeri olur.

---------------------------------------------------------------------------
UC YERE DOKUNUYORUZ
---------------------------------------------------------------------------
1) Pmd->Gear / Pmd->Mode
   Gear 3 -> 1, Mode FAST(1) -> SLOW(2). CAL katmani Mode'a bakip PWM
   tablolarini seciyor (UfsCal.h: IS_PWR_MODE_PWM).

2) PA_PWRMode (0x1571)
   Upstream sabit UFS_RXTX_POWER_MODE kullaniyor ve o da UFS_POWER_MODE'dan
   derleme aninda uretiliyor. pmd->Mode'dan hesaplanan bir degiskene
   ceviriyoruz ki calisma aninda dogru degeri yazsin.

3) PA_TxTermination / PA_RxTermination (0x1569 / 0x1584)
   Upstream ikisine de 1 yaziyor. TERMINASYON HS'E OZELDIR; PWM modunda 1
   yazmak pazarligi reddettirir. Modun HS olup olmamasina gore 1 ya da 0.

Ayrica BoardInit.c'de Ufs->GearMode 1'e cekiliyor (ayri degisiklik):
PA_TxGear/PA_RxGear dogrudan oradan besleniyor ve MaxGear de ona gore
kirpiliyor.

---------------------------------------------------------------------------
GERI ALMAK
---------------------------------------------------------------------------
yap.sh her derlemede $MU icindeki UfsDxe.c'yi git'ten geri cagiriyor, yani
bu yamayi cagirmamak yeterli. Birikmeli hasar olmuyor - onceki UFS yamasinda
bu tuzaga dusmustuk.
"""

import re
import sys
import pathlib

if len(sys.argv) < 2:
    print("Kullanim: python3 tools_ufs_pwm.py <Mu-Silicium-yolu>")
    sys.exit(1)

MU = pathlib.Path(sys.argv[1])
SRC = MU / "Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.c"

if not SRC.is_file():
    print(f"UfsDxe.c bulunamadi: {SRC}")
    sys.exit(1)

s = SRC.read_text()
uygulanan = 0

# --- 1) hedef guc modu sabitleri --------------------------------------------
eski = "STATIC\nEFI_STATUS\nUfsPmcCommon ("
yeni = """//
// B2L: PWM Gear 1 hedefi. Ayrintili gerekce tools_ufs_pwm.py basinda.
//
// UniPro'da guc modu degerleri: 1 FAST, 2 SLOW, 4 FASTAUTO, 5 SLOWAUTO.
// SLOW = PWM. CAL katmani (UfsCal.h, IS_PWR_MODE_PWM) buna bakip PWM
// tablolarini seciyor.
//
#define B2L_UFS_GEAR 1
#define B2L_UFS_MODE 2

STATIC
EFI_STATUS
UfsPmcCommon ("""
if eski in s and "B2L_UFS_MODE" not in s:
    s = s.replace(eski, yeni, 1)
    uygulanan += 1

# --- 2) PA_PWRMode: sabit yerine pmd->Mode'dan hesapla ----------------------
eski = """  UINT32 Register;
  struct UfsUicCmd pmc = {UIC_CMD_DME_SET, (0x1571U << 16), 0, UFS_RXTX_POWER_MODE};"""
yeni = """  UINT32 Register;
  //
  // B2L: upstream burada UFS_RXTX_POWER_MODE sabitini kullaniyordu ve o da
  // UFS_POWER_MODE'dan DERLEME ANINDA uretiliyor. PWM'e gecebilmek icin
  // degeri calisma aninda pmd->Mode'dan hesapliyoruz.
  //
  UINT32 B2LRxTxMode = (((UINT32)pmd->Mode) << 4) | ((UINT32)pmd->Mode);
  struct UfsUicCmd pmc = {UIC_CMD_DME_SET, (0x1571U << 16), 0, B2LRxTxMode};"""
if eski in s:
    s = s.replace(eski, yeni, 1)
    uygulanan += 1

# --- 3) terminasyon: HS'e ozel --------------------------------------------
eski = """    {UIC_CMD_DME_SET, (0x1584U << 16), 0, 1}, /* PA_RxTermination*/
    {UIC_CMD_DME_SET, (0x1569U << 16), 0, 1}, /* PA_TxTermination*/"""
yeni = """    //
    // B2L: TERMINASYON HS'E OZELDIR. Upstream ikisine de kosulsuz 1
    // yaziyordu; PWM modunda 1 yazmak pazarligi reddettiriyor.
    //
    {UIC_CMD_DME_SET, (0x1584U << 16), 0, (((pmd->Mode) == 1 || (pmd->Mode) == 4) ? 1U : 0U)}, /* PA_RxTermination*/
    {UIC_CMD_DME_SET, (0x1569U << 16), 0, (((pmd->Mode) == 1 || (pmd->Mode) == 4) ? 1U : 0U)}, /* PA_TxTermination*/"""
if eski in s:
    s = s.replace(eski, yeni, 1)
    uygulanan += 1

# --- 4) hedef modu ata ------------------------------------------------------
eski = """  Pmd->Gear = Ufs->GearMode;
  Pmd->Mode = UFS_POWER_MODE;
  Pmd->HsSeries = UFS_RATE;"""
yeni = """  //
  // B2L: HS yerine PWM Gear 1. Ufs->GearMode'u da esitliyoruz cunku
  // PA_TxGear/PA_RxGear ve CalParam->MaxGear dogrudan ondan besleniyor.
  //
  Ufs->GearMode = B2L_UFS_GEAR;
  Pmd->Gear     = B2L_UFS_GEAR;
  Pmd->Mode     = B2L_UFS_MODE;
  Pmd->HsSeries = UFS_RATE;
  DEBUG ((EFI_D_ERROR, "B2L UFS: hedef PWM G%d mod=%d\\n", B2L_UFS_GEAR, B2L_UFS_MODE));"""
if eski in s:
    s = s.replace(eski, yeni, 1)
    uygulanan += 1

if uygulanan < 4:
    print(f"HATA: {uygulanan}/4 yama uygulandi. Upstream degismis olabilir;")
    print("yamayi kor kor birakmiyoruz, derleme yanlis kodla gecmesin.")
    sys.exit(1)

SRC.write_text(s)
print(f"UfsDxe.c PWM G1'e yamalandi ({uygulanan}/4)")
