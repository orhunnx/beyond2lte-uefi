# Exynos 9820 UFS — EDK2 sürücü mimarisi

> ## ÖNEMLİ GÜNCELLEME — kapsam ciddi biçimde küçüldü
>
> İlk derleme denemesi beklenmedik bir şey ortaya çıkardı: **Mu-Silicium'da
> Exynos UFS altyapısı zaten var.**
>
> - `Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/` — genel Exynos UFS DXE sürücüsü
>   (~1200 satır), `SamsungPkg.dsc.inc` ile **her** Samsung platformunda derleniyor
> - `Silicon/Samsung/SamsungPkg/Library/UfsCalAdapterLib/` — CAL adaptör katmanı
> - `PlatformUfsLib` — SoC'a özel katman. **S5E9810** ve **S5E9830** için var,
>   **S5E9820 için yok.** `SamsungPkg.dsc.inc` bizi Null implementasyona düşürüyor.
>
> Yani bu belgede tarif ettiğim "dört primitif + tablolar" mimarisi doğruydu ama
> **onu sıfırdan yazmayacağız** — `S5E9830Pkg/Library/PlatformUfsLib/`
> (BoardInit.c 139 satır + UfsCal.c 907 satır) hazır şablon, ve S20'de
> depolama çalışıyor (Status.md: Internal Storage ✅).
>
> Yeni iş tanımı: **`S5E9820Pkg/Library/PlatformUfsLib` yaz**, 9830'unkini
> temel alıp bu belgedeki 9820 register adresleri ve CAL tablolarıyla doldur.
> Aşağıdaki analiz (adresler, tablo yapısı, mclk_rate, link akışı) aynen geçerli
> — sadece hedef dosya değişti.

Kaynak analizi: `LineageOS/android_kernel_samsung_exynos9820`, dal `lineage-23.2`,
`drivers/scsi/ufs/`. (Senin zaten derlediğin ağaç.)

## Ana bulgu

Beklediğimden çok daha iyi durumdayız. Samsung'un PHY init'i **elle yazılmış
mantık değil, veri tablosu**. `ufs-cal-9820.c` içinde 26 adet
`struct ufs_cal_phy_cfg[]` tablosu var ve tek bir yorumlayıcı fonksiyon
(`ufs_cal_config_uic`) bunları gezip dört primitiften birine yönlendiriyor.

```c
struct ufs_cal_phy_cfg {
    u32 addr;   // register / MIB adresi
    u32 val;    // yazılacak değer
    u32 flg;    // güç modu filtresi (FAST_MODE, SLOW_MODE...)
    u32 lyr;    // HANGİ primitifle yazılacağı — dispatch anahtarı
    u8  board;  // board filtresi
};
```

Yani EDK2 portu = **4 fonksiyon yaz + tabloları olduğu gibi taşı**.
Ters mühendislik yok, tahmin yok.

## Yazılması gereken dört primitif

Linux'taki karşılıkları `ufs-exynos.c`'de, hepsi tek satır:

| Primitif | Linux | EDK2 karşılığı |
|---|---|---|
| `UfsDmeSet(Addr, Val)` | `ufshcd_dme_set` | UICCMD register'ları — standart UFSHCI, `MdeModulePkg`'de zaten var |
| `UfsDmeGet(Addr, *Val)` | `ufshcd_dme_get` | aynı |
| `UfsPmaWrite(Val, Addr)` | `writel(val, reg_pma + addr)` | `MmioWrite32` |
| `UfsUniproWrite(Val, Addr)` | `writel(val, reg_unipro + addr)` | `MmioWrite32` |
| `UfsUdelay(Us)` | `udelay` | `MicroSecondDelay` |

`UfsPmaWrite`'ın tek incelik noktası: Linux'ta `phy_pma_writel` önce
`HCI_CLKSTOP_CTRL`'ü okuyup PMA clock'unu açıyor, yazıyor, geri kapatıyor.
Bunu atlarsan yazma sessizce yutulur.

## Register haritası — DTB'den doğrulandı

`ufs@0x13D60000` node'unun `reg` dizisi ile Linux'un `ioremap` sırası eşleştirildi:

| İndeks | Adres | Boyut | Linux değişkeni | Ne |
|---|---|---|---|---|
| 0 | `0x13D60000` | 0x200 | `hba->mmio_base` | **Standart UFSHCI** — JEDEC spec |
| 1 | `0x13D61100` | 0x200 | `reg_hci` | Samsung VS_HCI (vendor-specific) |
| 2 | `0x13D50000` | 0x8000 | `reg_unipro` | UNIPRO APB |
| 3 | `0x13D70000` | 0x100 | `reg_ufsp` | UFS Protector (FMP şifreleme) |

`ufs-phy` alt node'undan:

| Adres | Boyut | Ne |
|---|---|---|
| `0x13D64000` | 0x800 | **PHY PMA** — `UfsPmaWrite`'ın hedefi |
| `0x13D68000` | 0x804 | İkincil PHY bölgesi |
| `0x13C10700` | 0x4 | PMU PHY isolation kontrolü |

Adres hesabı:

```c
#define PHY_PMA_COMN_ADDR(reg)        (reg)
#define PHY_PMA_TRSV_ADDR(reg, lane)  ((reg) + (0x140 * (lane)))   // ufs-exynos.h
```

> **DÜZELTME.** Önce "`ufs-exynos.h` sonra include edildiği için 0x140 kazanır"
> yazmıştım — yanlıştı, include zincirini kontrol etmemiştim.
> `ufs-cal-9820.c` **`ufs-exynos.h`'i hiç include etmiyor**; yalnızca
> `ufs-cal-9820.h`'i alıyor. Dolayısıyla CAL katmanında geçerli olan değer
> kendi tanımı: **`0x400`**.
>
> `ufs-exynos.h`'teki `0x140` başka bir translation unit'e (`ufs-exynos.c`,
> debug dump kodu) ait ve CAL tablolarını ilgilendirmiyor.
>
> Karşılaştırma: Mu-Silicium'un 9830 portu `0x800` kullanıyor. Yani bu çarpan
> SoC'a özel — kopyalanacak bir sabit değil, her SoC için kendi CAL dosyasından
> alınmalı. **9820 → `0x400`.**

## Link startup akışı

```
UFSHCI HCE=1  (controller enable)
      │
      ├─ PRE_CHANGE ─────────────────────────────
      │    exynos_ufs_config_intr()  ×3          ← DL/N/T katman hata maskeleri
      │    exynos_ufs_ctrl_clk(true)
      │    exynos_ufs_select_refclk(true)
      │    exynos_ufs_gate_clk(false)
      │    exynos_ufs_set_hwacg_control(false)   ← HW auto-clock-gating KAPAT
      │    DME_GET(PA_AVAILRXDATALANES/TXDATALANES)
      │    mclk_rate = clk_get_rate(clk_unipro)  ← tablo hesaplarında kullanılıyor
      │    ufs_cal_pre_link()  →  init_cfg_evt1[38]
      │
DME_LINKSTARTUP  ← UIC komutu, standart
      │
      └─ POST_CHANGE ────────────────────────────
           ufs_cal_post_link() → post_init_cfg_evt1_g3[46]
           (lane 1 kullanılmıyorsa lane1_sq_off tablosu)
```

Sonra ayrı bir aşama olarak power mode change (`ufs_cal_pre_pmc` / `post_pmc`)
HS-Gear 3'e çıkıyor. **İlk hedefte buna gerek yok** — PWM/slow modda da okuma
yapılabilir, sadece yavaş olur. Boot için yeterli.

### Windows açısından kritik sınır

Bu akış yalnızca Linux'a özgü bir yorum değildir. Aynı 9820 türevindeki
[`ufs-exynos.c`](https://github.com/ianmacd/d2s/blob/master/drivers/scsi/ufs/ufs-exynos.c),
`link_startup_notify(PRE_CHANGE)` içinde önce hata katmanlarını, refclk'i ve
clock-gating'i ayarlıyor, sonra `ufs_pre_link()` ile `ufs_cal_pre_link()`i
çağırıyor. `POST_CHANGE` tarafında da `ufs_post_link()` ile
`ufs_cal_post_link()` çalışıyor. UEFI portunun CAL dizileri bu sebeple gerekli
oldu ve UEFI UFS okumasının çalışmasını açıklıyor.

Windows'un kutudan çıkan `storufs` sürücüsü bu Samsung varyant çağrılarını
bilmez. HCE/LinkStartup sonrasında bu kalibrasyon yeniden gerekirse, yalnız
ACPI `_CRS`/`_CLS` nesneleri bunu yerine getiremez; çözüm kendi Exynos StorPort
miniportumuz olur.

### WinPE kanıtı — eşleşme geçti, denetleyici başlatma başarısız

İlk WinPE denemesinde `ACPI\SMSN9820\0` için Code 28 görülmüştü. Bunun nedeni,
`_CLS={01,09,01}`in `ACPI\CC_010901` değil
`ACPI\CLS_0001&SUBCLS_0009&PI_01` yayınlamasıydı. Vendor `_HID` korunarak
eklenen standart `_CID="PCI\CC_010901"` bu engeli kaldırdı. Sonraki gerçek
cihaz raporu şunları gösterdi:

- `Driver Name: storufs.inf`, `Matching Device ID: PCI\CC_010901`,
  `Service: storufs`;
- aygıt sınıfı `SCSIAdapter`, sürücü durumu `Best Ranked / Installed`;
- ardından `Problem Code 10 (CM_PROB_FAILED_START)`, durum
  `0xC0000182`.

Dolayısıyla Windows artık UFS denetleyicisini tanıyor ve inbox sürücüsünün
başlatma yolunu gerçekten çalıştırıyor. Code 10, bir UFS diski veya LUN'un
oluştuğunu **göstermez**: aynı turda DiskPart fiziksel disk/birim göstermedi.
`X:` de dahili depolama değildir; WinPE'nin `boot.wim` için kurduğu RAM
diskidir.

Bu sonuç hata eksenini ACPI kimliğinden denetleyici başlatmaya taşır. En güçlü
mevcut açıklama, jenerik HCE resetinden sonra Exynos'un vendor saat/refclk,
clock-gating ve CAL zincirinin yeniden kurulmamış olmasıdır; fakat bunu henüz
kanıtlanmış gerçek olarak değil, sonraki kontrollü testin hipotezi olarak
tutarız. Bu noktadan sonra UFS'ye yazma yapılmaz; yalnız PnP ve DiskPart
salt-okunur tanılarıyla her değişken ayrı ölçülür.

## `mclk_rate` — ÇÖZÜLDÜ

Tablolardaki bazı değerler sabit değil, `mclk_rate`'ten türetiliyor.
Bu, portun tek bilinmeyeniydi. **Değer kaynakta sabit:**

`drivers/soc/samsung/cal-if/exynos9820/cmucal-vclk.c`:

```c
struct vclk_lut cmucal_vclk_clkcmu_fsys1_ufs_embd_lut[] = {
    {177667, clkcmu_fsys1_ufs_embd_uud_lut_params},
};
```

Tek girdi — yani UFS UNIPRO clock'u **sabit**, DVFS yok. Birim kHz
(aynı dosyadaki CPU/PLL değerleri 2.85M ve 4.26M; Hz olsa 2.85 MHz'lik bir
CPU olurdu). EVT0 ve EVT1 tablolarında değer aynı, silikon revizyonu fark etmiyor.

**`mclk_rate` = 166 000 000 Hz** — DONANIMDAN DOGRULANDI

> Once bu LUT'a bakip 177.667 MHz yazmistik. **Yanlisti.** Cihaz
> `UFS: 1US_TO_CNT bootloader=166 beklenen=177` diyerek soyledi:
> sboot 166 programlamis. LUT'taki 177667 bolucu oncesi CMU cikisi
> olmali. 9830 portu da 166000000 kullaniyor.
>
> Bu satiri tam da bu ihtimal icin koymustuk ve isini gordu.

Türetilen sabitler:

| Makro | Değer | Not |
|---|---|---|
| `UNIPRO_MCLK_PERIOD` | **6** | `1e9 / mclk` tam bölme (177 varsayımıyla 5 çıkıyordu) |
| `UNIPRO_MCLK_PERIOD_ROUND_OFF` | **6** | `DIV_ROUND_CLOSEST` |
| `UNIPRO18_MCLK_PERIOD` | **96** | `16e9 / mclk` (177 ile 90) |
| `HCI_1US_TO_CNT_VAL` | **166** (0xA6) | maske 0x3FF — sboot'un yazdığı değer |

> İkisinin farklı olması (5 vs 6) hata değil, kasıtlı — farklı register'lar
> kırpılmış ve yuvarlanmış değer istiyor. Tabloları taşırken hangi girdinin
> hangisini kullandığına dikkat et, `lyr` alanı söylüyor
> (`PHY_PCS_*_PRD` → 5, `PHY_PCS_*_PRD_ROUND_OFF` → 6).

### Cihazdan okuma neden gerekmedi

`/dev/mem` LineageOS kernel'inde kapalı (`CONFIG_DEVMEM=n`), clk debugfs de yok.
Ama gerek kalmadı — değer kaynakta sabitti.

Yine de UEFI sürücüsünde **çalışma anı doğrulaması** olarak kullanışlı:
`0x13D6110C` okunup `& 0x3FF` sonucu **166** çıkmalı. Bu kontrol işini gördü:
177 beklerken 166 okuduk ve varsayımın yanlış olduğunu link kurulmadan önce
öğrendik.

## Bize gerekmeyenler

Linux sürücüsünün büyük kısmı boot için gereksiz — atlanacaklar:

- **FMP / UFSP** (`0x13D70000`) — inline şifreleme. Windows/Linux boot'u için gereksiz.
- **Hibern8 / power management** — UEFI'de suspend yok.
- **Error recovery, dbg dump** (`ufs-exynos-dbg.c`, 28 KB) — ilk sürümde gereksiz.
- **Clock scaling, devfreq** — gereksiz.
- **Card (HOST_CARD) tabloları** — bizde `HOST_EMBD` (dahili UFS).
- **EVT0 tabloları** — 9820 EVT1 silikon. `evt_ver` kontrolünden EVT1 dalını al.
- **GEAR_4 tabloları** — DTB `pmd-attr-gear = 3` diyor, G3 yeterli.

Bu eleme sonrası taşınacak tablo sayısı 26'dan yaklaşık **6'ya** düşüyor.

## Önerilen dosya yapısı

```
Silicon/Samsung/S5E9820Pkg/Drivers/UfsExynosDxe/
  UfsExynosDxe.c        — DXE entry, MMIO base'leri kaydet, protokol yayınla
  UfsExynosDxe.inf
  UfsExynosPhy.c        — 4 primitif + ufs_cal_config_uic karşılığı
  UfsExynosPhy.h
  UfsCalTables9820.c    — kernel'den birebir taşınan tablolar
  UfsCalTables9820.h
```

EDK2 tarafında `MdeModulePkg/Bus/Ufs/UfsPassThruDxe` standart UFSHCI'yi zaten
biliyor — TRB kuyruğu, SCSI komutları, `EFI_EXT_SCSI_PASS_THRU_PROTOCOL`.
Bizim sürücümüz onun **altında** duruyor: controller'ı ve PHY'ı ayağa kaldırıp
link'i kuruyor, sonra sahneyi `UfsPassThruDxe`'ye bırakıyor.

Bağlantı noktası: Mu-Silicium/Qualcomm portlarında bunun için bir platform
protokolü var (`UfsHcPlatform` benzeri). `Silicon/Qualcomm/*/Drivers/`
altındaki UFS sürücülerine bakıp aynı sözleşmeyi kullanmak,
tekerleği yeniden icat etmekten iyi.

## Sıradaki somut adımlar

1. **`mclk_rate` değerini cihazdan oku** (yukarıdaki `clk_summary` komutu) —
   tablolardaki tek bilinmeyen bu.
2. `ufs-cal-9820.c`'den EVT1 + HOST_EMBD + G3 tablolarını süz, C dizisi olarak çıkar.
3. Mu-Silicium'daki Qualcomm UFS sürücüsünü incele, protokol sözleşmesini kopyala.
4. Dört primitifi yaz.
5. Derle, flash et, `HCI_SYS_STATUS`'a bak.

Adım 1 sende, 2-3 bende. Paralel gidebiliriz.
