# beyond2lte (SM-G975F) — Donanım Haritası

Kaynak: stock firmware `G975FXXSGHWC1` (Android 12), `AP_*.tar.md5` içindeki
`dt.img` + `dtbo.img`. Android DT table formatı (`0xd7b7ab1e`), `dtc` ile decompile edildi.

## Cihaz kimliği

| Alan | Değer |
|---|---|
| SoC | Samsung Exynos 9820 (`samsung,exynos9820`, Mu-Silicium'da `S5E9820Pkg`) |
| Base DTB | `dt.img` → 1 entry, 239.680 byte |
| Board overlay | `dtbo.img` → 10 entry, hepsi `BEYOND2LTE EUR OPEN` |
| Board rev'ler | 0x04, 0x10, 0x11, 0x12, 0x13, 0x14, 0x17, 0x18, 0x19, 0x1A |
| Ekran | 1440 × 3040, 32bpp (S10 ile aynı) |

**Bu cihazın board rev'i: 26 (0x1A)** → `dtbo` entry 9, `dtbo-hw_rev_end = 0xff`
(yani en son revizyon, catch-all).

Rev 17 ile rev 26 arasındaki 2497 satırlık farkın **tamamı** batarya termistör
kalibrasyon tabloları, şarj eşikleri ve panel DDI ID'si. UFS, DECON, DSIM, GPIO,
PMIC register adreslerinde **hiç fark yok** — yani UEFI portu açısından hangi
revizyon olduğu önemsiz.

## Register haritası — doğrulanmış

Bu adresler `beyond1lte`'nin `MemoryMapLib.c`'sinden alındı ve beyond2lte DTB'sine
karşı **birebir doğrulandı**. Değiştirmeye gerek yok.

| Blok | Adres | Boyut | DTB node |
|---|---|---|---|
| Chip ID | `0x10000000` | 0x100 | `chipid@10000000` |
| GICD | `0x10101000` | 0x1000 | `interrupt-controller@10100000` |
| GICC | `0x10102000` | 0x1000 | aynı node |
| PMU | `0x15860000` | 0x10000 | `system-controller@15860000` |
| DECON_F | `0x19030000` | 0x10000 | `decon_f@0x19030000` |
| CMU (clock) | `0x1A240000` | 0x8000 | `clock-controller@0x1a240000` |
| Pinctrl | `0x15850000` | 0x1000 | `pinctrl@15850000` |

## Henüz portta olmayan bloklar

Aşağıdakiler UEFI portunda **yok** — asıl iş burada.

### UFS (dahili depolama) — en kritik eksik

```
ufs@0x13D60000
  compatible = "samsung,exynos-ufs"
  reg = 0x13D60000 (0x200)   ← HCI (standart UFSHCI register seti)
        0x13D61100 (0x200)   ← Samsung VS_HCI (vendor-specific)
        0x13D50000 (0x8000)  ← UNIPRO / MPHY
        0x13D70000 (0x100)   ← PHY control
  interrupts = SPI 249 (0xF9)
  ufs,pmd-attr-lane = 2      ← 2 lane
  ufs,pmd-attr-gear = 3      ← HS-Gear 3
  hw-rev = 6
  dma-coherent
```

Windows/Linux boot bunsuz mümkün değil. Referans implementasyon:
Samsung Android kernel `drivers/scsi/ufs/ufs-exynos.c` + `ufs-exynos-dbg.c`.
EDK2 tarafında `UfsPassThruDxe` (MdeModulePkg) standart HCI kısmını halleder;
yazılması gereken şey Exynos'a özel **PHY/UNIPRO init** ve **link startup** sekansı.

### Ekran (DSIM/MIPI)

```
dsim@0x19080000
  compatible = "samsung,exynos9-dsim"
  reg = 0x19080000 (0x300) / 0x19161000 (0x800) / 0x19160000 (0x100)
  interrupts = SPI 190 (0xBE)

decon_f@0x19030000
  interrupts = SPI 183, 184, 189, 407
  psr_mode = 2 (command mode), max_win = 6
```

Mevcut port **simple framebuffer** kullanıyor — bootloader'ın kurduğu ekranı
devralıyor, kendi init'ini yapmıyor. Bu yüzden "Display ✅". Yeterli.

### PMIC

| Çip | Rol |
|---|---|
| `samsung,s2mps19mfd` | Ana PMIC |
| `samsung,s2mps20mfd` | İkincil PMIC |
| `samsung,s2mpb03pmic` | Kamera/sensör rail'leri |
| `maxim,max77705` | Şarj / USB-PD |

UFS'in `vcc-supply`'ı fixed regulator — yani UFS güç rail'i bootloader tarafından
zaten açık bırakılıyor. **İyi haber:** UFS sürücüsü için PMIC yazmamıza gerek yok.

### Tuşlar (mevcut portta var, aynen çalışmalı)

| Tuş | GPIO |
|---|---|
| Volume Up | bank 0, `gpa0`, pin 3 |
| Volume Down | bank 0, `gpa0`, pin 4 |
| Power | bank 2, `gpa2`, pin 4 |

## ACPI durumu

`Include/ACPI.inc` dosyasının **tamamı yorum satırı**. Yani beyond1lte portunda
ACPI tabloları hiç derlenmiyor. Windows ACPI olmadan boot etmez — Status.md'deki
"Windows Boot ❌" bunun doğrudan sonucu.

Sıralama önemli: ACPI'yi yazmak UFS'ten sonra anlamlı, çünkü DSDT'de tanımlayacağın
depolama cihazının arkasında çalışan bir sürücü olması gerekiyor.

## Bellek yerleşimi (beyond1lte'den, değiştirilmedi)

```
0x87000000  EL2 Code           2 MB
0xC9000000  UEFI FD            2 MB
0xC9200000  UEFI Stack       256 KB
0xC9240000  DXE Heap          60 MB
0xCA000000  Display Reserved  20 MB
```

DDR base `0x80000000`. S10+ 8/12 GB varyantlarında bu düşük bölge değişmiyor,
dokunmaya gerek yok.

## Download mode ekranından doğrulananlar

Odin/download mode ekranı birkaç şeyi tahminden ölçüme çevirdi.

| Alan | Değer | Neden önemli |
|---|---|---|
| `EVT` | **1.1** | Çip revizyonu. UFS CAL tabloları EVT'ye göre filtreleniyor (`UfsInitCal` → `CalParam->EvtVer = MajorChipRev`). Açılışta `UFS EVT version` satırı **1** göstermeli |
| `LPDDR4 size` | **8gb** | Bellek haritasındaki 8 GB varsayımı doğrulandı. `truncatememory` denemelerinde bu tahmindi |
| `RP SWREV` | **B:16 K:16 S:16** | Anti-rollback. Stok firmware'e dönerken 16'dan eski binary'ler reddedilir — yanlış sürüm cihazı kurtarılamaz hale getirebilir |
| `PMIC_ID` | 12 (c4), trimmed | PMIC revizyonu; güç yolu ya da PMIC watchdog gündeme gelirse gerekir |
| `OEM LOCK` | OFF(U) | Beklenen, yeni bilgi değil |

**Kaydedilmeyenler:** `DID` ve `CHIP_ID` cihaza özel benzersiz kimlikler.
`ORTAM.md`'deki kural gereği depoya yazılmıyor.
