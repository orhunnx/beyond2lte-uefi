# beyond2lte UEFI — durum

**Son güncelleme:** 10 Ağustos 2026 — UEFI ilk kez açıldı.

## Çalışan / çalışmayan

| Özellik | Durum | Not |
|---|---|---|
| UEFI boot | ✅ | Project Silicium ekranı geliyor |
| Display | ✅ | Framebuffer `0xCA000000` doğruymuş |
| Yan tuşlar | ✅ | Ses +/−, güç — `KeypadDeviceLib` GPIO'ları doğru |
| **Dahili depolama (UFS)** | **✅** | 5 LUN, DiskIO protokolü kurulu — `S5E9820Pkg/Library/PlatformUfsLib` yazıldı |
| USB | ❌ | Henüz sürücü yok |
| OS boot | ❌ | Depolama olmadan mümkün değil |

Karşılaştırma: upstream `beyond1lte` (S10) Status.md'de Display ✅ ve tuşlar ✅
diyor ama "Inactive" işaretli. **beyond2lte artık ondan ileride** — çünkü onun
memory map hatasını düzelttik (aşağıya bak).

## Bulunan ve düzeltilen iki hata

### 1. `UfsDxe.inf` — eksik `TimerLib`

`UfsDxe.c` `<Library/TimerLib.h>` include edip `MicroSecondDelay` çağırıyor ama
`.inf` `TimerLib`'i `[LibraryClasses]`'ta bildirmiyor.

9810/9830'da fark edilmiyor çünkü onların `PlatformUfsLib`'i `TimerLib`'i dolaylı
getiriyor. 9820'de o kütüphane olmadığından `SamsungPkg.dsc.inc` bizi
`PlatformUfsLibNull`'a düşürüyor, Null yalnızca `DebugLib` bildiriyor, zincir
kopuyor:

```
lld-link: error: undefined symbol: MicroSecondDelay
```

`beyond1lte` CI'ın aktif cihaz listesinde olmadığı için bu hata bugüne kadar
kimseye çarpmamış.

### 2. `beyond1lte` memory map çakışması — asıl suçlu

```
DXE Heap          0xC9240000 – 0xCCE40000   (60 MB)
Display Reserved  0xCA000000 – 0xCB400000   ← tamamen heap'in içinde
```

`SimpleFbDxe` framebuffer tabanını `MemoryMapLib`'deki `"Display Reserved"`
bölgesinden **isimle** alıyor. DXE heap aynı aralığa tahsis yapınca framebuffer
eziliyor ve ekranda bootloader'ın son karesi kalıyor — bizde tam olarak bu oldu.

Çalışan referans `x1s` (S20, Exynos 9830) yerleşimi tertemiz, çakışma yok.

Düzeltilmiş beyond2lte yerleşimi:

```
UEFI FD           0xC9000000 – 0xC9200000
UEFI Stack        0xC9200000 – 0xC9240000
Display Reserved  0xCA000000 – 0xCB400000
DXE Heap          0xCB400000 – 0xCF000000
```

Sonraki DTB rezervi `modem_spare` `0xD0000000` → 16 MB boşluk kalıyor.

> Bu iki düzeltme upstream'e gönderilmeye değer. İkincisi `beyond1lte`'yi de
> muhtemelen canlandırır.

### 3. `beyond1lte` DXE.inc / APRIORI.inc — depolama yığını hiç yok

DEBUG log'unda `UfsDxe.efi` sürücü listesinde **hiç görünmedi**. Sebep:
`beyond1ltePkg/Include/DXE.inc` ve `APRIORI.inc` dosyalarında depolama yığını
tamamen eksik. `UfsDxe` derleniyor (çünkü `SamsungPkg.dsc.inc` `[Components]`'ta),
ama firmware volume'a hiç konmuyor.

Çalışan `x1s` ile diff'i tam olarak şunu gösteriyor:

```
+ INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
+ INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
+ INF SamsungPkg/Drivers/UfsDxe/UfsDxe.inf
+ INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
+ INF FatPkg/EnhancedFatDxe/Fat.inf
```

Beşi de beyond2lte'ye eklendi. `PlatformUfsLib` yazılsa bile bunlar olmadan
dosya sistemi görünmezdi.

## Log'daki diğer mesajlar — zararsız olanlar

| Mesaj | Yorum |
|---|---|
| `Image ... start failed: Unsupported` (HsI2cDxe) | `BusCount == 0` → bu platformda I2C bus tanımlı değil. 9820'de PMIC **Speedy** üzerinden konuşuyor, `SpeedyDxe` sorunsuz yüklendi. Zararsız. |
| `Image ... start failed: Aborted` (AcpiPlatformDxe) | `ACPI.inc`'in tamamı yorum satırı, kurulacak tablo yok. Beklenen. |
| `Unable to locate any 964E5B22-...` | Bu GUID `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`. Depolama + FAT sürücüsü yoktu. |
| `Failed to Map RAM Range: 0xC9240000 - 0xDC0000` | Memory map düzeltmemin bıraktığı 14 MB'lık boşluk (Stack sonu ↔ Display başı). Kozmetik, 14 MB kullanılmıyor. İleride kapatılabilir. |

Doğrulanan: `Memory Allocation 0x00000007 0xCB400000 - 0xCEFFFFFF` — taşınan DXE
Heap tam istediğimiz yerde. SMBIOS de doğru: *"Project Silicium for Samsung
Galaxy S10+"*.

## UFS'i ayağa kaldırırken çıkan dört hata

Kayda değer, çünkü hepsi "9830'dan kopyala" refleksinin tuzağıydı.

1. **`UniProAddr`** — 9830'da `UFS_BASE + 0x8000`. 9820'de değil, ayrı tabanda
   (`0x13D50000`). Formülü kopyalasak link hiç kurulmazdı.
2. **GPIO bankı** — 9830 `gpf2`, 9820 `gpf3` (DTB `ufs-rst-n` / `ufs-refclk-out`).
3. **`UFSHCI_VS_*` offsetleri `IoAddr`'a göredir**, `VsAddr`'a göre değil
   (`0x110C` zaten `0x1100 + 0x0C`). `VsAddr` kullanınca 0x1100 iki kez eklenip
   `0x13D6228C` çıktı ve `Data abort: Translation fault` alındı. `FAR` register'ı
   hatayı tam adresiyle verdi.
4. **`MemoryMapLib`'de UFS bölgeleri yoktu** — `beyond1lte`'de hiç tanımlı değil,
   çalışan `x1s`'te var. Eklendi.

Ve bir de benim yaptığım gerileme: GPIO controller'larını `MemoryMapLib`'e
eklemek `GpioDxe`'yi bozdu. O sürücü bölgeleri **kendi** `MapMemoryRegion()` ile
map ediyor; önden eklenince çakışıp `Access Denied` veriyor ve zincirleme olarak
UsiDxe / PmicDxe / UfsDxe / KeypadDxe hepsi düşüyor. **GPIO blokları memory
map'e eklenmez.**

## İzlenecek: `UFS OCS=0x01`

`UFS OCS=0x01 response=0x00 type=0x20` satırı LUN'lar kurulmadan önce bir kez
geçiyor. UFSHCI'de OCS 0x00 = SUCCESS, 0x01 = INVALID_COMMAND_TABLE_ATTRIBUTES.
Beş LUN sorunsuz sayıldığı için engelleyici değil, ama enumerasyon sırasında
geçici bir hata olduğunu gösteriyor. Okuma/yazma testinde sorun çıkarsa ilk
buraya bakılmalı.

## Sıradaki iş

UFS bitti. Bundan sonrası iki ayrı yol:

**A. Bir şey boot ettirmek.** UEFI depolamayı görüyor ama cihazda EFI System
Partition yok — LineageOS klasik Android bölümleri kullanıyor, EFI bootloader'ı
yok. En hızlı doğrulama: USB-C flash belleğe EFI Shell ya da bir Linux ISO'su
koyup denemek. Ama USB sürücüsü de yok, yani önce onu ya da UFS üzerinde bir
ESP oluşturmayı çözmek gerekiyor.

**B. ACPI.** Windows'a giden yol. `Include/ACPI.inc`'in tamamı hâlâ yorum
satırı; `Silicium-ACPI` altında 9820 için tablo üretmek gerekiyor. UFS artık
çalıştığı için DSDT'de tanımlanacak depolama cihazının arkasında gerçek bir
sürücü var — bu sıralama baştan beri böyle olmalıydı.

## Tamamlanan: `S5E9820Pkg/Library/PlatformUfsLib`

Altyapı hazır: `SamsungPkg/Drivers/UfsDxe` (genel Exynos UFS sürücüsü) ve
`UfsCalAdapterLib` zaten var, her Samsung platformunda derleniyor. Eksik olan tek
şey SoC katmanı. Şablon: `S5E9830Pkg/Library/PlatformUfsLib/`
(BoardInit.c 139 satır + UfsCal.c 907 satır), ve o SoC'ta depolama çalışıyor.

Gereken 9820 verisi `UFS_DRIVER.md`'de hazır: register adresleri, CAL tablo
yapısı, `mclk_rate` = 177.667 MHz ve türetilen sabitler, link startup akışı.

## Debug kanalı

Mu-Silicium'da Exynos donanım UART sürücüsü **yok**; `SerialPortLib`
`FrameBufferSerialPortLib`'e çözümleniyor, yani debug çıktısı **ekrana** yazılıyor.
Display çalıştığı için artık gerçek bir debug kanalımız var:

```bash
python3 build_uefi.py -d beyond2lte -r DEBUG
```

UART jig kablosu ancak biri Exynos `SerialPortLib`'i yazarsa anlamlı olur
(UART base'leri DTB'de: uart0 `0x10440000`). Şimdilik gerek yok.

---

# EFI Shell çalışıyor — doğrulanan zincir

`startup.nsh` çıktısı (10 Ağustos 2026):

```
FS0: ... VenHw(5F2C8E91-...,00)/HD(28,GPT,52444E41-494F-2044-6361-636865000000)
Directory of: fs0:\
  EFI  <DIR>      startup.nsh  762 bytes
```

**FS0 = CACHE bölümü.** Samsung GPT tip GUID'i bölüm adını ASCII gömüyor:
`...-6361-636865000000` → `cache`.

| Katman | Durum |
|---|---|
| UFS link (G3, 2 lane) | ✅ |
| 5 LUN → BlockIo | ✅ |
| GPT → 31 bölüm | ✅ |
| PartitionDxe → HD(1..31) | ✅ |
| Fat.efi → FS0 (cache) | ✅ |
| Shell + startup.nsh | ✅ |

`dh -p SimpleFileSystem` tek handle döndürüyor (3C = cache). Diğer 30 bölüm
Android formatlarında olduğu için bağlanmıyor — beklenen.

`ver`: UEFI Interactive Shell v2.2 / EDK II / UEFI v2.70 (Project Silicium)

## ESP'yi çalıştırmak için gereken üç kısıt

Her biri bir tur kaybettirdi, üçü birden sağlanmadan olmuyor:

1. **`-S 4096`** — UFS mantıksal blok boyutu 4096. FAT BPB'sindeki sektör
   boyutu `BlockIo` ile eşleşmeli.
2. **`-F 16`** — FAT32 değil. `FatPkg/EnhancedFatDxe/Init.c`:
   `if (MaxCluster < FAT_MAX_FAT16_CLUSTER) return EFI_VOLUME_CORRUPTED`.
   65525 eşiği; CACHE 75 MB'ta 19200 cluster veriyor. 4K sektörle FAT32 için
   en az 256 MB gerekiyor.
3. **sparse** — Odin ham FAT imajını reddediyor, `img2simg` ile Android sparse
   formatına çevrilmeli. (`dd` yolu ham imaj istiyor, ikisi de üretiliyor.)

## Bölüm haritası — cihazdan doğrulandı

`map -r` çıktısındaki GPT GUID'lerinden çözüldü:

| HD# | Bölüm | HD# | Bölüm | HD# | Bölüm |
|---|---|---|---|---|---|
| 1 | bota0 | 12 | dtb | 22 | dqmdbg |
| 2 | bota1 | 13 | dtbo | 23 | vbmeta |
| 3 | efs | 14 | **boot** | 24 | bota3 |
| 4 | param | 15 | **recovery** | 25 | **system** (712 MB) |
| 5 | up_param | 16 | radio | 26 | vendor |
| 6 | keystorage | 17 | nad_refer | 27 | product |
| 7 | sec_efs | 18 | persistent | 28 | **cache** → FS0 |
| 8 | bota2 | 19 | misc | 29 | omr |
| 9 | steady | 20 | keyrefuge | 30 | cp_debug |
| 10 | uh | 21 | keydata | 31 | **userdata** (kalan ~120 GB) |
| 11 | uhcfg | | | | |

## ~~KRİTİK: UEFI'den UFS'e yazma çalışmıyor~~ — TEŞHİS YANLIŞTI

`startup.nsh` içindeki `>a fs0:\LOG_00_test.txt` yönlendirmesi cihazda
**"Unable to redirect file."** verdi ve Shell script'i "Invalid Parameter"
ile durdurdu.

Bilinenler:

- `UfsDxe.c` içinde `UfsWriteBlocks` **var**, `Media.ReadOnly = FALSE`
  atanıyor, `UfsWrite` düzgün WRITE_10 CDB'si kuruyor. Yani sürücüde eksik
  bir şey görünmüyor.
- Aynı bölüme Android tarafından `dd` ile yazılabiliyor, donanım yazılabilir.
- Okuma tarafı sorunsuz: FAT bağlanıyor, dosyalar okunuyor, Shell açılıyor.

### İkinci, bağımsız kanıt

Windows denemesinden sonra cache bölümü geri okundu: imajda **sadece bizim
koyduğumuz dosyalar** var. `bootmgr` normalde her açılışın durumunu
`bootstat.dat` olarak ESP'ye yazar — yazamamış. `WINLOG.TXT` de yok.

Yani iki bağımsız yazıcı (EFI Shell yönlendirmesi ve `bootmgr`) aynı yerde
başarısız oluyor. Sorun bizim `UfsDxe` yazma yolumuzda.

### Bu neden kritik

Dosya tabanlı **tek** tanı kanalımız buydu. Seri port yok, USB yok; ekran da
`winload` sonrası susuyor. Yazma çalışmadığı sürece Windows'un ne yaptığını
öğrenmenin yolu kalmıyor — son turlar bunu gösterdi.

Ayrıca Windows'u kalıcı kurmak da yazma gerektiriyor.

### Şüpheliler

1. **Önbellek bakımı eksik.** `UfsDxe.c`'de DMA öncesi/sonrası hiçbir cache
   işlemi yok. Okuma çalışıp yazmanın çalışmaması bununla tutarlı: aygıt
   DRAM'e yazınca CPU taze veriyi görüyor, ama CPU'nun yazdığı veri
   önbellekte kalıyorsa aygıt bayat DRAM okuyor. `BoardInit.c`'de SYSREG
   koherans bitlerini set ediyoruz; ya etkili olmuyorlar ya da tek yönlü.
   **En güçlü aday.**
2. LUN write protect (`fPowerOnWPEn` / `bLUWriteProtect`) — Android aynı
   bölüme yazabildiği için zayıf ama elenmiş değil.
3. FatPkg'in 4096 baytlık sektörle yazma yolu.

Ders: yönlendirme hatası script'i komple öldürüyor, o yüzden dosyaya yazmaya
bel bağlanamaz. `startup.nsh` artık önce ekrana döküyor, yazma denemesi en
sonda.

## acpiview sonucu (ilk gerçek doğrulama)

`acpiview -r 2`: önce **12 hata**, düzeltmelerden sonra **3 hata** — kalan üçü
de bilerek bırakıldı. Tablolar spec'e temiz.

| Tur | Hata | Ne değişti |
|---|---|---|
| İlk ölçüm | 12 | 8 TRBE + 4 eksik SBBR tablosu |
| TRBE sıfırlandı, PPTT eklendi | 3 | MCFG, SPCR, DBG2 — üçü de kasıtlı |

Hepsi hesaba oturuyor: 8 + 4.

### 8 hata: TRBE Interrupt (DÜZELTİLDİ)

Her GICC girdisinde `TRBE Interrupt` alanı çöp değer taşıyordu (7 tanesi
`0x500B`, biri `0x180C` — düzenli olsalar hepsi aynı olurdu). Bu alan bir PPI
kimliği olmak zorunda: 16-31 ya da 1056-1119.

Değerler 9830'un AML'inden taşınmış. Exynos 9820 ARMv8.2; TRBE bir ARMv9
özelliği, yani bu SoC'de yok. Doğru değer **0** (desteklenmiyor). Sekiz
girdinin hepsi sıfırlandı.

### 4 hata: eksik SBBR tabloları (kasıtlı olarak bırakıldı)

SBBR v1.2 zorunlu diyor ama SBBR sunucu profilidir; Windows'un açılması için
hiçbiri şart değil:

| Tablo | Ne işe yarar | Karar |
|---|---|---|
| MCFG | PCIe yapılandırma alanı | Telefonda PCIe yok. Gerekmiyor. |
| SPCR | Seri konsol yönlendirme | UART sürücümüz yok. Gerekmiyor. |
| DBG2 | Hata ayıklama portu | Açılış için şart değil. Sonra. |
| PPTT | İşlemci topolojisi | **Eklenmeli.** Windows CPU topolojisini buradan okur. |

### Doğru çıkan şeyler

- **GIC**: DTB `arm,cortex-a15-gic` diyor = GICv2. MADT'deki `Version : 02`,
  GICR base 0 ve GICD/GICC/GICH/GICV adresleri DTB'nin `reg` alanıyla birebir.
- **MPIDR**: 0-3 / 4-5 / 0x100-0x101. Önce 9830'dan kalma sandım, DTB
  doğruladı: `cpu@0000..0003` (ananke/A55), `cpu@0004..0005`
  (prometheus/A75), `cpu@0100..0101` (cheetah/M4). Doğruymuş.
- MADT, GTDT, FACP başka hata vermedi.

### Efficiency Class ters çevrildi

ACPI 6.4 §5.2.12.14: düşük değer daha verimli demek. Tabloda A55'lere 02,
M4'lere 00 verilmişti — tam tersi. Düzeltildi: A55 -> 00, A75 -> 01,
M4 -> 02. Açılışı engellemez, Windows'un heterojen zamanlayıcısını etkiler.

## Linux ACPI ile açıldı (ilk uçtan uca test)

Alpine 6.10 LTS, `acpi=force`, DTB verilmeden. Sonuçlar:

### Çalışan

- Bütün tablolar yüklendi: RSDP, XSDT, FACP, DSDT, APIC, SSDT, GTDT, **PPTT**, BGRT
- `ACPI: PPTT ... 00013C` — üreticimizin çıkardığı 316 baytın tam olarak kendisi
- `ACPI: Interpreter enabled`, `Using GIC for interrupt routing`
- 8 CPU'nun hepsi `hot-added` (MADT doğru okunuyor)
- `arch_timer: cp15 timer running at 26.00MHz` (GTDT doğru)
- `DMI: Samsung Galaxy S10+/Galaxy S10+` (SMBIOS doğru)
- initramfs açıldı, `Run /init as init process`'e kadar gelindi

### Bozuk: PSCI

```
psci: PSCIv33792.0 detected in firmware.
psci: SMC Calling Convention v32760.0
psci: failed to boot CPU1..CPU7 (-22)
SMP: Total of 1 processors activated.
```

33792 = **0x8400**. Bu bir sürüm değil, `PSCI_VERSION` çağrısının fonksiyon
kimliği (0x84000000). Yani çağrı yapıldı, kimse cevaplamadı, `x0` olduğu gibi
döndü.

Sebep FADT'de: `ARM Flags : 0003` = PSCI_COMPLIANT + **PSCI_USE_HVC**. Linux
EL1'de çalışıyor (`All CPU(s) started at EL1`), HVC EL2'ye düşüyor, oradaki
Mu-Silicium saplaması PSCI uygulamıyor. Samsung'un EL3 firmware'i ise
uyguluyor — Android sekiz çekirdeği onunla açıyor.

**Düzeltme: `ARM Flags : 0001`** (SMC). **Doğrulandı:**

```
psci: SMC Calling Convention v1.0
smp: Bringing up secondary CPUs ...
CPU1..CPU7: Booted secondary processor
SMP: Total of 8 processors activated.
```

Üstelik MIDR'lar PPTT topolojimizi birebir doğruladı:
CPU0-3 `0x411fd050` (A55), CPU4-5 `0x412fd0a1` (A75),
CPU6-7 `0x531f0030` (Mongoose M4).

Not: `PSCI_VERSION` hâlâ 0xFFFFFFFF (NOT_SUPPORTED) dönüyor ama `CPU_ON`
çalışıyor — Samsung'un EL3'ü sürüm sorgusunu uygulamamış. İşlevsel sorun yok.

Bu doğrudan Windows'u da ilgilendiriyor; Windows da ikincil çekirdekleri
PSCI ile açar.

### Bozuk: initramfs /init (bizim regresyonumuz)

`Kernel panic: Attempted to kill init! exitcode=0x00007f00` → 127, "komut
bulunamadı", ve script'ten tek satır çıktı yok.

Alpine'ın initramfs'inde busybox applet sembolik bağları **önceden yok**;
Alpine'ın kendi `init`'i `#!/bin/busybox sh` ile başlayıp `busybox --install -s`
çalıştırıyor. İlk sürümümüzde ikisi de vardı, yeniden yazarken düştü.
Geri kondu, ayrıca `init` artık asla çıkmıyor (sonsuz bekleme) — çıkarsa panik
ekranı yazdıklarımızı bastırıyor.

## Windows'a giden yol

ACPI aşaması bitti. Doğrulananlar:

| Ne | Kanıt |
|---|---|
| Tablolar spec'e temiz | `acpiview -r 2` → 3 hata, üçü kasıtlı |
| MADT + PSCI | `SMP: Total of 8 processors activated` |
| PPTT | Linux `cluster=76/96/136` — üreticinin düğüm offsetleri |
| GTDT | `arch_timer: cp15 timer running at 26.00MHz` |
| DSDT | `SMSN9820:00` `/sys/bus/acpi/devices` altında |

### storufs bağlanması — ÇÖZÜLDÜ

`storufs.inf` (Win11 25H2 ARM64, `windows/storufs.inf`) şunu içeriyor:

```
[Generic.NTarm64]
%ACPI\CC_010901.DeviceDesc%=UfsInstall, ACPI\CC_010901
```

**Jenerik bir ARM64 girdisi var**, Qualcomm'a özel değil. `CC_010901` satıcı
kimliği değil, PCI sınıf kodu: 01 yığın depolama / 09 UFS / 01 UFSHCI.

Bu kimlik `_HID`'den türeyemez — ACPI kimlikleri 4 harf + 4 hane olmak zorunda,
`CC_010901` dokuz karakter. Windows bunu ACPI 6.0'la gelen **`_CLS`**
nesnesinden üretiyor. DSDT'ye eklendi:

```asl
Name (_CLS, Package (0x03) { 0x01, 0x09, 0x01 })
```

INF'ten çıkan diğer notlar: jenerik `[UfsInstall]` bölümünde hiç `HW.AddReg`
yok, yani `FeatureFlags` / `HSSeries` / `NumLanes` varsayılanda kalıyor.
Qualcomm girdisi `FeatureFlags = 0x40000000`, `HSSeries = 0x2` koyuyor;
Intel girdileri `NumLanes = 2`, `HighSpeedGearMode`, `CommandTimeoutInMS`
gibi ayarlar veriyor. Bizim donanım 2 lane / Gear 3 / Rate B — gerekirse bu
değerler çevrimdışı SYSTEM hive'ına yazılabilir.

### Kalan bilinmez: HCE reset

`storufs.sys` başlarken UFSHCI spec'ine göre denetleyiciyi resetler (`HCE=1`).
Exynos'ta reset sonrası PHY'nin **yeniden kalibre edilmesi** gerekiyor ve
jenerik sürücü Exynos CAL tablolarını bilmiyor. UEFI'nin kurduğu link
büyük ihtimalle bu noktada kopar.

Umut verici işaret: Qualcomm girdisinin `FeatureFlags = 0x40000000` değeri.
Qualcomm WoA'da da PHY init'i UEFI yapıyor, yani sürücüde "firmware zaten
başlattı" anlamına gelen bir davranış olabilir. Denenmeden bilinmez.

### Yerleşim

Yeni bölüm oluşturmaya gerek yok, PIT'teki mevcutlar yeter:
`CACHE` (600 MB) → ESP, `USERDATA` (~120 GB) → Windows. GPT'ye dokunulmuyor.
USERDATA'nın biçimlendirilmesi Android verisini siler ama geri dönüşü var.

### Giriş aygıtı yok

USB sürücüsü olmadığı için kurulum sihirbazı çalıştırılamaz. Yöntem: WIM'i
WSL'de `wimlib-imagex` ile doğrudan bölüme uygulamak, kurulumu atlamak.

## Windows Boot Manager çalıştı

İlk Windows denemesi, hiçbir şey silmeden. ISO'nun `bootaa64.efi`'si ESP'ye
konup `startup.nsh`'tan başlatıldı.

**Sonuç: Windows kurtarma ekranı çizildi.**

```
File: \EFI\Microsoft\Boot\BCD
Error Code: 0xc000000f
```

Hata bir başarısızlık değil, tam tersi. Bu ekranın görünebilmesi için Windows
Boot Manager'ın şunları yapmış olması gerekiyordu:

| Adım | Ne kanıtlıyor |
|---|---|
| Yüklendi ve çalıştı | PE imajımızı yükleyen UEFI yükleyicisi doğru |
| Kendi grafik arayüzünü çizdi | GOP'umuz Windows'a yetiyor |
| ESP'den dosya okudu | FAT + DiskIo + **UfsDxe** Windows tarafında çalışıyor |
| BCD yolunu aradı | Dosya sistemi yolunu çözebiliyor |
| ACPI'yi ayrıştırdı | Tablolarımız kabul edildi (aksi halde daha erken çökerdi) |

`0xc000000f` + `File: ...\BCD` = BCD eksik ya da bozuk. Sebep bizim
tarafımızda: `tools_winboot.sh` BCD'yi `|| true` ile çıkarmaya çalışıyordu ve
sessizce başarısız olmuştu. Script artık yolları ISO listesinden buluyor
(büyük/küçük harf ISO'dan ISO'ya değişiyor), dosyanın `regf` ile başladığını
doğruluyor ve eksikse duruyor.

### Sıradaki sınır: boot.wim sığmıyor

ISO'nun BCD'si WinPE'yi RAM diskten açmak üzere kurulu ve `\sources\boot.wim`
arıyor — 606 MB. CACHE bölümü 600 MB. Yani doğru BCD ile bu sefer
`0xc0000225` (boot device bulunamadı) beklenir; o da BCD'nin ayrıştırıldığını
kanıtlar ve bir sonraki adıma geçeriz.

## Windows: yükleyici zinciri baştan sona çalışıyor

Hata mesajlarının sırası, her adımın ayrı ayrı doğrulandığını gösteriyor:

| Deneme | Hata | Ne öğrendik |
|---|---|---|
| Sadece bootmgfw | `0xc000000f` + `File: ...\BCD` | Boot Manager çalıştı, ekran çizdi, ESP'den **okudu** |
| BCD eklendi | `0xc000000f` "required device" | BCD ayrıştırıldı; ramdisk kurulamadı |
| boot.wim eklendi | aynı | eksik olan WIM değilmiş |
| **boot.sdi eklendi** | `0xc0000487` + `File: ...\winload.efi` | **RAM diski kuruldu, WIM bağlandı, WIM'in içindeki dosyaya erişildi** |

Yani `bootmgfw -> BCD -> boot.sdi -> ramdisk -> boot.wim -> winload.efi`
zincirinin tamamı işliyor. Kalan tek adım `winload.efi`'nin kendisi.

### `0xc0000487` — ÇÖZÜLDÜ: `STATUS_WIM_NOT_BOOTABLE`

Kod herkese açık kaynaklarda belgelenmiyor. İki hipotez var:

1. ~~**WIM sıkıştırması**~~ **ELENDİ.** `delete 2` + `optimize` ile orijinal
   sıkıştırılmış veri hiç dokunulmadan korunarak denendi (570 MB, `verify`
   temiz). Aynı hata. Yani `bootmgr` WIM'i doğru okuyor.

2. ~~**ARMv8.1 şartı**~~ **ELENDİ.** 24H2 sonrası minimum ARMv8.1; bizim
   çekirdekler ARMv8.2 ve Linux `LSE atomic instructions` tespit ediyor.

3. **Çekirdekler arası özellik farkı — açık.** Linux logunda:

   ```
   SANITY CHECK: Unexpected variation in SYS_ID_AA64ISAR0_EL1
   Boot CPU: 0x...10305400   CPU6: 0x...10305410
   CPU features: Unsupported CPU feature variation detected.
   ```

   Farklı nibble `[7:4]` = **AES** alanı. M4 çekirdeklerinde kripto uzantıları
   var, **açılışın yapıldığı A55'te yok**. winload CPU özelliklerini boot
   işlemcisinden okuyorsa aday sebep bu. Çıkarım, kanıt değil.
   **Test:** `linux_test/init` artık `/proc/cpuinfo` Features satırını
   cpu0 ve cpu7 için ayrı ayrı ekrana basıyor.

4. **Windows sürümü — açık.** 25H2 yerine 23H2 ya da Windows 10 ARM64
   denenebilir. Büyük indirme, ama kesin sonuç verir.

**Cevap:** `certutil -error 0xc0000487` (Windows'ta, tek satır):

```
0xc0000487 (NT: 0xc0000487 STATUS_WIM_NOT_BOOTABLE)
Belirtilen Windows Görüntüsü (WIM) önyüklenebilir olarak işaretli değil.
```

WIM başlığında **`Boot Index`** alanı var ve hangi imajın açılabilir olduğunu
o söylüyor. Windows ISO'sunda bu değer **2** — yani Setup imajı. Biz de tam
olarak 2 numaralı indeksi sildik, geriye işaretli imaj kalmadı. İlk denemede
de sadece indeks 1'i dışarı aktarmıştık; export de işareti taşımıyor.

Yani iki denemenin ikisi de aynı sebepten düştü ve sıkıştırmayla hiç ilgisi
yoktu. Hata adı ne olduğunu birebir söylüyordu; ben "bozuk dosya" diye
okudum ve iki tur harcadım.

**Düzeltme:** `wimlib-imagex info boot.wim 1 --boot`

**Ders:** NTSTATUS kodunu tahmin etmeye çalışmak yerine `certutil -error`
ile sormak. Arama motorları bu kodu bulamadı, Windows'un kendisi bir
saniyede söyledi.

## WinPE yükleniyor, `winload` sessizce sıfırlıyor

`Boot Index: 1` işaretlendikten sonra cihazda **"Loading files" ve Windows'un
ilerleme çubuğu** çıktı — `bootmgr` `boot.wim`'i RAM diske tamamen yükledi.
Ardından Project Silicium logosu göründü, yani cihaz sıfırlanıp UEFI'den
baştan başladı.

Yani zincir şuraya kadar sağlam:

```
bootmgfw -> BCD -> boot.sdi -> ramdisk -> boot.wim (yüklendi) -> winload.efi -> ???
```

`winload` **hiçbir çıktı üretmeden** sıfırlıyor. `sos On` açıktı, tek satır
görülmedi. `bootmgr` aynı ekrana yazabildiğine göre GOP çalışıyor; demek ki
çökme herhangi bir çıktıdan önce.

### Eleme planı

Serial/KDNET olmadan çökme noktasını doğrudan göremiyoruz, o yüzden BCD
varyantlarıyla eleyeceğiz. `windows/bcd_olustur.cmd [varyant]`:

| Varyant | Ayar | Hangi hipotezi test eder |
|---|---|---|
| `tani` | — | temel; sadece tanı seçenekleri |
| `tekcpu` | `numproc 1` | Çökme ikincil çekirdek açılırken mi? Bizim MADT/PSCI ile en ilgili varyant. |
| `azram` | `truncatememory 4 GB` | Çökme yüksek bellek adreslerinden mi? 8 GB'lik haritanın üst kısmı şüpheli olabilir. |

Aynı anda ikisi denenmemeli; hangisinin işe yaradığı belirsiz kalır.

## `numproc 1` çekirdeği başlattı — çökme SMP'de

`tekcpu` varyantı (BCD `numproc 1`) ile davranış değişti:

| Varyant | Görülen |
|---|---|
| `tani` (8 CPU) | "Loading files" → **hiçbir çıktı yok** → sıfırlama |
| `tekcpu` (1 CPU) | "Loading files" → **Windows'un dönen dairesi** → sıfırlama |

Dönen daireyi `ntoskrnl` çiziyor. Yani tek çekirdekle **çekirdek başlıyor**;
sekiz çekirdekle hiç başlayamıyordu.

**Sonuç: ilk çökme ikincil çekirdek açılırken oluyor.** MADT/PSCI yolumuz
Linux'u tatmin ediyor ama Windows'u etmiyor. Bu artık tahmin değil, ölçülmüş
bir fark.

İki iş kaldı, sırayla:

1. Tek çekirdekle ilerleyip **ikinci çökmeyi okumak**. Bunun için WinPE
   registry'sinde `CrashControl\AutoReboot = 0` yapıldı (WIM içindeki SYSTEM
   hive'ı `hivexregedit` ile düzenleniyor), böylece mavi ekran ekranda kalıyor
   ve stop kodu okunabiliyor.
2. SMP sorununu çözmek. Windows'un PSCI `CPU_ON` çağrısı Linux'unkinden farklı
   parametreler kullanıyor olabilir; MADT'deki `Parking Protocol Version` ve
   GICC alanları gözden geçirilecek.

## Ekran sönme animasyonu = çökme değil, düzgün kapanma

`AutoReboot=0` yazıldıktan sonra cihaz hâlâ yeniden başlıyor **ama dönen daire
bir ekran sönme animasyonuyla kayboluyor.** Mavi ekran olsaydı artık ekranda
kalırdı; animasyon Windows'un kendi düzgün yeniden başlatma yolu.

WinPE'de bunun tek anlamı var: **kabuk sonlandı.** WinPE'nin kabuğu `cmd.exe`
ve çıktığı anda makine yeniden başlar — tasarım böyle.

Yani `ntoskrnl` ayakta, WinPE başlıyor, oturum açılıp kapanıyor. Çökme yok.

**Çözüm:** kendi `\Windows\System32\startnet.cmd`'imiz (`windows/startnet.cmd`),
WIM'e enjekte ediliyor. Asla çıkmıyor, `diskpart` ile birim/disk listesini hem
ekrana hem ESP'ye yazıyor. Depolama görünüyorsa `WINLOG.TXT` ESP'ye düşüyor ve
recovery'den okunabiliyor — Linux'ta initramfs ile yaptığımızın aynısı.

Bu aynı zamanda `_CLS` ile eklediğimiz UFS düğümünün Windows tarafında işe
yarayıp yaramadığını gösterecek: `list disk` çıktısında disk görünürse
`storufs.sys` bağlanmış demektir.

## Düzeltme: UFS yazma ÇALIŞIYOR, sorun Shell'in yönlendirmesinde

`UfsDxe.c` enstrümante edilip (bkz. `tools_ufs_patch.py`) DEBUG derlemesiyle
denendi. Cihaz şunu bastı:

```
FSOpen: Open '\YAZTEST.TXT' Success
B2L UFS: WriteBlocks LUN=0 LBA=1983528 boyut=4096 blok=4096 ro=0
B2L UFS: WriteBlocks sonuc = Success
FSOpen: Created new directory entry 'YAZTEST.TXT'
```

Yani `Media.ReadOnly = 0`, `WRITE_10` başarılı, FatPkg dizin girdisini
oluşturuyor. **Blok yazma, dosya sistemi yazma, dizin güncelleme: hepsi
çalışıyor.**

Başarısız olan tek şey EFI Shell'in yönlendirme mekanizması:

```
Unable to redirect file.
Script Error Status: Invalid Parameter (line number 20)
```

20. satır `>a` operatörünü kullanan satır. Dosya açılıyor ve yazılıyor, ama
Shell yönlendirmeyi kuramayıp `EFI_INVALID_PARAMETER` dönüyor ve script'i
durduruyor.

### Nerede yanıldım

`bootstat.dat`'ın olmamasını "yazma kırık" diye yorumladım ve iki bağımsız
kanıt olduğunu söyledim. İkisi de aynı yanlış çıkarımdı: Shell'in yönlendirme
hatası yazma hatası sanıldı, `bootmgr`'ın dosya yazmamasının başka bir sebebi
olabilir (`bootstatuspolicy`, ya da o turda hatagoster imajının flaşlanmamış
olması).

Enstrümantasyon olmasa bu yanlış teşhisle devam edecektik. **Ders: "çalışmıyor"
demeden önce ölçüm koy.**

### Bunun Windows için anlamı

Dosya kanalı aslında AÇIK. WinPE depolamayı görebilirse `WINLOG.TXT` yazılabilir
ve recovery'den okunabilir — kör eleme bitebilir.

## EFI Shell yönlendirmesi bu platformda çalışmıyor (engel değil)

`>` ve `>a` operatörlerinin **ikisi de** `Unable to redirect file /
Invalid Parameter` verip script'i öldürüyor. Ama enstrümantasyon gösterdi ki
dosya oluşuyor ve içine yazılıyor:

```
FSOpen: Created new directory entry 'T1.TXT'
B2L UFS: WriteBlocks sonuc = Success
```

EDK2'nin yönlendirme kodu (`Shell.c`, `UpdateStdInStdOutStdErr`) hedef dosyayı
açıp **siliyor** ve yeniden oluşturuyor. Silme yolunda takılıyor olması en
olası açıklama.

**Engel değil:** Shell yönlendirmesine yalnızca kendi ACPI dökümlerimiz için
ihtiyacımız vardı, onlar alındı. Windows'un logları Shell'den geçmiyor.

**Asıl kazanım:** blok yazma, FAT yazma, dizin oluşturma çalışıyor. Yani WinPE
depolamayı görürse `WINLOG.TXT` yazılabilir ve recovery'den okunabilir.

## `_CLS` kapatıldı: 5-6 sn → **12 sn**

`storufs.sys`'in UFS0'a bağlanmasını sağlayan `_CLS` nesnesi DSDT'de yorum
satırına alındı. Cihazın dayanma süresi **ikiye katlandı**.

Bu ölçülebilir bir fark ve şüpheliyi doğruluyor: `storufs.sys` bağlanıyor,
UFSHCI spec'ine göre denetleyiciyi resetliyor (`HCE`) ve jenerik kodla link
kurmaya çalışıyor — Exynos'un PHY kalibrasyonunu bilmeden. Zaman aşımına
düşüp çekirdeği götürüyor.

**Windows için sonuç:** `storufs.sys` bu SoC'de olduğu gibi kullanılamaz.
Ya UEFI'nin kurduğu linki bozmaması sağlanmalı (`FeatureFlags`, Qualcomm
girdisindeki `0x40000000` gibi), ya da Exynos için ayrı bir sürücü gerekir.
Kalıcı Windows kurulumunun önündeki gerçek engel bu.

### 12 saniyenin ikinci anlamı

`startnet.cmd`'nin ilk beklemesi 20 saniyeydi; ona ulaşsaydık daha uzun
dayanırdık. Muhtemelen `timeout` anında dönüyor (WinPE'de ağ yokken oluyor)
ve hemen `wpeinit`'e geçiliyor — `wpeinit`'in aygıt taraması tam olarak bu
kadar sürer.

Bir sonraki test `wpeinit`'i hiç çağırmayan bir `startnet.cmd`. Cihaz artık
yeniden başlamazsa **WinPE bu telefonda ayakta** demektir.

## Windows: ulaşılan son nokta ve duvar

`wpeinit`'siz, hiçbir dış komut çağırmayan, saf batch sonsuz döngüsünden ibaret
bir `startnet.cmd` ile de cihaz ~12 saniyede yeniden başlıyor.

**Sonuç: `startnet.cmd` hiç çalışmıyor.** Çökme, çekirdek başladıktan sonra
ama kabuk çalışmadan önce — `smss.exe` → `wininit.exe` → `winpeshl.exe`
zincirinde.

### Windows tarafında doğrulananlar

| Aşama | Durum |
|---|---|
| `bootmgfw.efi` yükleniyor, ekran çiziyor | ✅ |
| ESP'den dosya okuyor (FAT + DiskIo + UfsDxe) | ✅ |
| BCD ayrıştırılıyor | ✅ |
| `boot.sdi` + RAM disk kuruluyor | ✅ |
| `boot.wim` yükleniyor ("Loading files") | ✅ |
| `winload.efi` çalışıyor | ✅ |
| `ntoskrnl` başlıyor (dönen daire çiziliyor) | ✅ |
| Erken kullanıcı modu (smss/wininit/winpeshl) | ❌ ~12 sn sonra sessiz reset |

### Elenen ve doğrulanan hipotezler

| Hipotez | Sonuç |
|---|---|
| WIM sıkıştırması | Elendi (orijinal veriyle de aynı) |
| ARMv8.1 şartı | Elendi (çekirdekler ARMv8.2, LSE var) |
| `STATUS_WIM_NOT_BOOTABLE` | **Çözüldü** (Boot Index 1) |
| SMP / ikincil çekirdek | **Doğrulandı** (`numproc 1` çekirdeği başlattı) |
| `storufs.sys` + `_CLS` | **Doğrulandı** (5-6 sn → 12 sn) |
| Zamanlayıcı (dynamic tick) | Elendi (süre değişmedi) |
| UEFI'den UFS'e yazma | Elendi (çalışıyor, ölçüldü) |

### Neden burada duruyoruz

Kalan tek şüpheli erken kullanıcı modunda ve oraya bakmak için Windows'un
kendi hata ayıklama kanallarından biri gerekiyor: seri port (DBG2 + gerçek
UART), KDNET (ağ kartı) veya USB 3.0 debug kablosu. Üçü de donanım istiyor.

Yazılımla açılabilecek her kanal denendi ve kapalı çıktı: `sos` çıktı vermiyor,
`bootstat.dat` yazılmıyor, `startnet.cmd` sıraya gelmiyor, ekran `winload`
sonrası susuyor.

## Donanımsız iki tanı kanalı daha

UART yokken de bakabileceğimiz iki yer kaldı; ikisi de Samsung'un kendi
donanımını kullanıyor.

### 1. PMU `RST_STAT` — sıfırlamanın kaynağı (uygulandı)

Exynos'un PMU bloğundaki `RST_STAT` (0x15860404) son sıfırlamanın **kaynağını**
tutuyor ve sıfırlamayı aşıyor. `BoardInit.c` bunu zaten okuyordu ama
yazdırmıyorduk.

Windows çöküp cihaz sıfırlandıktan sonra, bir sonraki açılışta bu register
neden sıfırlandığını söylüyor:

| Bit | Anlamı | Ne yapmalı |
|---|---|---|
| `WARM_RESET` (28) | Yazılım sıcak sıfırlama istedi — Windows'un `ResetSystem` çağrısı | Kontrollü çökme, bugcheck yolu |
| `LITTLE_WDT_RESET` (24) | Watchdog attı, yazılım takılmış | Kilitlenme; timer/kesme tarafı |
| İkisi de 0 | Soğuk açılış | Çok daha sert bir hata |

Bu ayrım önemli: Windows'un kendi istediği bir reset ile donanımın zorla attığı
bir reset tamamen farklı yerlere bakmayı gerektiriyor.

Not: Linux ACPI ile 120 saniye sorunsuz çalıştı, yani ortada sürekli çalışan
bir watchdog yok. Bu, `WARM_RESET` beklentisini güçlendiriyor.

### 2. UEFI Runtime `ResetSystem` kancası (sıradaki)

`ResetSystem` bir **runtime servisi** — `ExitBootServices`'ten sonra da bizim
firmware'imizde kalıyor ve Windows yeniden başlatmak için onu çağırıyor.

Kancalarsak:
- Sıfırlamak yerine sonsuz döngüye girebiliriz → cihaz yeniden başlamaz,
  bu tek başına "Windows mu istedi" sorusunu cevaplar
- `ResetStatus` parametresini okuyabiliriz (bir `EFI_STATUS`)
- `ResetData` bazı durumlarda **sebep metni** içeriyor

Zorluk: `SetVirtualAddressMap` sonrası işaretçilerin `EfiConvertPointer` ile
çevrilmesi gerekiyor ve framebuffer'ın runtime bölgesi olarak işaretlenmesi
lazım. Yapılabilir; UART'a alternatif gerçek bir kanal.

### RST_STAT: ölçülen referans ve yöntem düzeltmesi (2026-08)

İlk okuma alındı. Kurtarma menüsünden yeniden başlatılmış bir cihazda,
EFI Shell'den `dmem 0x15860404 4`:

```
RST_STAT  = 0x20000000     (bit 29)
SEQUENCER = 0x00000600
```

İki sonuç:

1. **Bit numaralandırması 9830'unkinden farklı.** Bu açılış tanım gereği bir
   yazılım sıfırlamasıydı, ama beklenen `WARM_RESET` (bit 28) değil **bit 29**
   yanıyor. Yukarıdaki tabloda kullanılan sabitler Mu-Silicium'un 9830
   kodundan geliyor ve S5E9820 için doğrulanmamış. Artık mutlak bit isimlerine
   değil, **referansa göre farka** bakıyoruz: `0x20000000` = normal yazılım
   yeniden başlatması.

2. **Üç adımlı ölçüm planı geçersizdi.** "Okuyucuyu flaşla → Windows'u flaşla →
   okuyucuyu tekrar flaşla" dizisi işe yaramaz, çünkü `RST_STAT` yalnızca *son*
   sıfırlamayı tutuyor ve Odin'in flaş sonrası yeniden başlatması da bir
   sıfırlama. Windows'un bıraktığı iz, tam da onu okumaya giderken siliniyordu.

   Düzeltme: tek imaj. `esp/startup.nsh` önce `RST_STAT`'ı döküyor, `pause` ile
   bekliyor, sonra `bootmgfw.efi`'yi çağırıyor. Windows çöküp cihaz
   sıfırlandığında aynı script yeniden açılıyor ve bu kez **o çöküşün** sebebini
   gösteriyor. Yeniden flaşlama yok, döngü kapalı.

   İlk açılış Odin'in yeniden başlatmasını gösterir (`0x20000000`); ikinci
   açılıştaki değer aranan cevaptır.

### UFS yazma: düzeltilmiş sonuç — LBA 0 aygıt tarafında reddediliyor

Windows açılırken ekrana düşen satırlar (`FSOpen: 'EFI' / 'Microsoft' / 'Boot'`
başarılı olduktan hemen sonra, üç kez tekrarlanarak):

```
B2L UFS: WriteBlocks LUN=2 LBA=0 boyut=4096 blok=4096 ro=0
UFS OCS=0x07 response=0x01 type=0x21
B2L UFS: WRITE_10 basarisiz LBA=0 blok=1 -> Device Error
B2L UFS: WriteBlocks sonuc = Device Error
```

`UfsDxe.h`'deki tabloda `OCS_FATAL_ERROR = 0x7`; `response=0x01` SCSI hedef
hatası. İstek denetleyiciye ulaşıyor, **UFS aygıtı reddediyor**. Sürücü
tarafında `ro=0`, yani `Media.ReadOnly` yanlış bir güven veriyor —
`UfsDxe.c` bu alanı koşulsuz `FALSE` yazıyor, aygıtın `bLUWriteProtect`
niteliğine hiç bakmıyor.

Belirleyici ayrıntı **LBA = 0**. Daha önce başarılı ölçtüğümüz yazmalar
(dosya verisi, `YAZTEST.TXT` dizin girdisi) sıfırdan büyük adreslerdeydi.
Başarısız olanların hepsi sıfırıncı blok. Yani "UFS'e yazabiliyoruz" doğru
ama eksikti: **ilk blok hariç** yazabiliyoruz.

Sonuçları:

- `bootstat.dat` neden hiç oluşmadı, Shell'in `>` / `>a` yönlendirmesi neden
  öldü — tek sebep. `bootmgfw` açılış durumunu kalıcılaştıramıyor.
- Windows'tan yazılım yoluyla log almanın son kanalı da kapandı. Geriye
  `RST_STAT` kalıyor.

Açık kalan: LUN 2 ESP'mizin bulunduğu LU mu, yoksa başka bir LU mu?
`startup.nsh` artık `map -r` çalıştırıyor; fs0'ın aygıt yolunda LUN numarası
ve bölüm (`HD(...)`) olup olmadığı görülecek. Bölüm yoksa LBA 0 = FAT
önyükleme sektörü; varsa LBA 0 = LU'nun başı, yani bizim dosya sistemimiz
bile değil ve yazan başka biri.

## Sıfırlamayı yazılım istiyor — `ResetSystem` kancası takıldı

İki ölçüm, aynı yöntemle, aynı cihazda:

| Ne zaman | `RST_STAT` |
|---|---|
| Kurtarma menüsünden yeniden başlatma | `0x20000000` |
| Windows çöküp cihaz sıfırlandıktan sonra | `0x20000000` |

Değişmedi. Watchdog biti (`0x01000000`) yok, soğuk sıfırlama biti yok.
Yalnızca yazılım sıfırlaması biti. **Sıfırlamayı donanım zorlamıyor, yazılım
istiyor.**

ARM64 UEFI'de yazılımın sıfırlama isteyebileceği tek arayüz var:
`EFI_RUNTIME_SERVICES.ResetSystem`. Ve o bizim kodumuz — `ExitBootServices`
sonrasında da bizde kalıyor, Windows onu çağırmak zorunda.

`B2LResetHookDxe` bunu kancalıyor:

- `DXE_RUNTIME_DRIVER`, yani imaj runtime bölgede kalıyor ve
  `SetVirtualAddressMap`'te yeniden konumlandırılıyor
- Kanca `ResetType` + `ResetStatus` + `ResetData`'yı `0xCF000000`'a yazıyor
- Ekrana yazmıyor — kanca `ExitBootServices` sonrası çalışıyor, `ConOut` ve
  `DEBUG` yok. Bunun yerine **DRAM sıcak sıfırlamayı atlatıyor**; Android'in
  `ramoops`'u tam olarak buna dayanıyor ve bu SoC'de çalıştığı biliniyor
- Sonraki açılışta EFI Shell `dmem 0xCF000000 192` ile okuyor — yeni bir
  kanal icat etmiyoruz, zaten çalışanı kullanıyoruz

Bellek bölgesi `MemoryMapLib.c`'ye `RtData` + `UNCACHED_UNBUFFERED` olarak
eklendi. `RtData` şart: `MemoryInitPei` bunu özel olarak ele alıp
`EFI_MEMORY_RUNTIME` işaretliyor, böylece Windows bölgeye sanal adres veriyor.
`Reserv` olsaydı haritalanmaz, kanca yazmaya çalışınca fault olurdu.
`UNCACHED_UNBUFFERED` de şart: kanca yazdıktan hemen sonra cihaz sıfırlanıyor,
veri önbellekte kalırsa kaybolur.

İki olası sonuç, ikisi de bilgi:

- `+0x80`'de `42 32 4C 52` (`B2LR`) → Windows gerçekten `ResetSystem` çağırdı.
  `+0x90`'daki `ResetStatus` bugcheck'i doğrudan söylüyor.
- İmza yok → kanca hiç çağrılmadı. Sıfırlama `ResetSystem`'den geçmiyor;
  istisna, doğrudan PSCI SMC, ya da güç devresi. O zaman istisna vektörlerine
  bakarız.

### `RST_STAT` belirsizliği kapandı: register canlı

Önceki iki ölçüm de `0x20000000` çıkmıştı ve bu belirsizdi — "Windows da
yazılım sıfırlaması yapıyor" ile "register hiç güncellenmiyor" arasında karar
veremiyorduk. Üçüncü ölçüm farklı bir değer verdi:

| Açılış yolu | `RST_STAT` |
|---|---|
| Odin flaşından sonraki ilk açılış | `0x40010000` |
| Kurtarma menüsünden kasıtlı yeniden başlatma | `0x20000000` |
| **Windows çöktükten sonra** | **`0x20000000`** |

Register açılış yoluna göre değişiyor, yani stuck değil. Ve Windows'un çöküşü
kasıtlı bir yeniden başlatmayla aynı değeri üretiyor. **Sıfırlamayı yazılım
istiyor.**

`dmem` boyutu onaltılık okuyor — `dmem <adr> 192` 402 bayt döküyor ve ekranı
taşırıyor. Kanca kaydı bu yüzden görülemedi; dökümler `0x10`/`0x40` olarak
kısaltıldı.

### Linux'un cevaplayabileceği, hiç bakmadığımız sorular

Linux bizim ACPI'mizle açılıyor, Windows açılmıyor. Fark, Linux'un hoşgörüp
Windows'un hoşgörmediği şeylerde olmak zorunda. `linux_test/init` artık
bunları raporluyor:

| Ne | Neden önemli |
|---|---|
| **Exception Level** | Windows ARM64 çekirdeği EL1'de çalışır. Bellek haritamızda `EL2 Code` diye 2 MB'lık bir bölge var (`0x87000000`), yani ortada bir EL2 stub'ı dolaşıyor. Linux `started at EL2` derse bu, sessiz çöküşün en güçlü adayı. |
| **`arch_timer` frekansı** | GTDT'miz 9830'dan uyarlandı, hiç doğrulanmadı. Windows zamanlayıcı konusunda Linux'tan çok daha katı. Çöküşün her seferinde ~12 saniyede olması sabit bir zaman aşımına benziyor; yanlış ölçeklenmiş bir 10 saniye tam olarak böyle görünür. |
| **PSCI sürümü/conduit** | FACP düzeltmesi Linux'u 1 çekirdekten 8'e çıkarmıştı; kayda geçiyor. |
| **GIC sürümü** | `acpiview` MADT'de GICR uyarıları vermişti. |
| **`0xCF000000` `devmem` ile** | Kanca kaydının bağımsız doğrulaması. |

## Kanca sonucu: Windows `ResetSystem`'i çağırmıyor

`dmem 0xCF000040` / `0xCF000080`, arka arkaya iki açılışta:

```
CF000040: 42 32 4C 49 03 04 00 00 ...   -> "B2LI", sayaç 0x403
CF000040: 42 32 4C 49 04 04 00 00 ...   -> "B2LI", sayaç 0x404
CF000080: FF FF FF FF ...  (her ikisinde de)
```

Sayaç iki açılış arasında tam bir arttı. Bu iki şeyi birden kanıtlıyor:
sürücü her açılışta çalışıyor **ve** `0xCF000000` bölgesi sıfırlamayı
atlatıyor. Yani `+0x80`'in boş olması bir ölçüm hatası değil — **kanca hiç
çağrılmadı.**

`RST_STAT` hâlâ yazılım sıfırlaması diyor. Birleştirince: sıfırlamayı yazılım
istiyor ama UEFI runtime servisini kullanmadan. En olası yol, FADT'deki
`PSCI_COMPLIANT` bayrağını gören Windows'un doğrudan `SMC` ile
`PSCI SYSTEM_RESET` çağırması. Bu çağrı EL1'den EL3'e gidiyor; UEFI'nin
haberi bile olmuyor.

### Kalan tek belirsizlik ve onu kesen test

Kanca çağrılmış ama `0xCF000000`'a yazarken fault vermiş olabilir
(`SetVirtualAddressMap` sonrası işaretçi çevrimi tutmadıysa). Ekranda ikisi
de aynı görünür.

`B2L_HOOK_HANG` bunu ayırıyor: kanca hiçbir belleğe dokunmadan, en başta
sonsuz döngüye giriyor.

| Gözlem | Sonuç |
|---|---|
| Cihaz 12. saniyede **donuyor** | Kanca çağrılıyormuş; sorun bizim yazmada, haritalama düzeltilir |
| Cihaz yine **yeniden başlıyor** | Windows `ResetSystem`'i gerçekten atlıyor; UEFI seviyesinde görüş bitti |

İkincisi çıkarsa geriye tek gözlem noktası kalıyor: **EL2**. Bellek
haritamızda `EL2 Code` (`0x87000000`, 2 MB) bölgesi var, yani UEFI EL2'de
çalışıyor ve Windows'u EL1'e bırakıyor olabilir. Öyleyse `HCR_EL2.TSC = 1`
ile EL1'den gelen her `SMC` EL2'ye tuzaklanır ve PSCI çağrısını fonksiyon
numarasıyla birlikte görürüz. Bunun ön koşulu Linux testinin cevaplayacağı
soru: hangi exception level'dayız?

### `echo` ikinci kez ısırdı

EFI Shell'in `echo`'su kelime başındaki `/` karakterini de bayrak sanıyor
(`Unknown flag - '/'`), tıpkı `-` gibi. Metinlerden ikisi de çıkarıldı.

### Linux dalı geri geldi

`cache_esp.sh` bayraksız çalıştırıldı (Linux modu) ama `startup.nsh`'te Linux
dalı kalmamıştı; cihaz Windows'u arayıp bulamadı. Script artık iki modu da
destekliyor, Linux'a önce bakıyor.

## Linux'un cevapları ve yeni ana hipotez: PSCI

`linux_test/init`'in Windows bölümü üç şey söyledi, üçü de bakmadığımız yerdi.

```
CPU: All CPU(s) started at EL1
arch_timer: cp15 timer(s) running at 26.00MHz (virt)
psci: PSCIv65535.65535 detected in firmware.
psci: Using standard PSCI v0.2 function IDs
```

**EL1.** EL2'de değiliz. `HCR_EL2.TSC` ile `SMC` tuzaklama planı geçersiz —
o ayrıcalık bizde yok. Bellek haritasındaki `EL2 Code` bölgesi sadece bir
geçiş kodu; çalışma anında EL2'yi tutan biz değiliz.

**Zamanlayıcı doğrulandı, hipotez kapandı.** GTDT'yi stock DTB'ye karşı
kontrol ettim:

| | GTDT | DTB (`arm,armv8-timer`) |
|---|---|---|
| Secure EL1 | GSIV 29 | PPI 13 → 29 |
| Non-secure EL1 | GSIV 30 | PPI 14 → 30 |
| Virtual | GSIV 27 | PPI 11 → 27 |
| Non-secure EL2 | GSIV 26 | PPI 10 → 26 |
| Frekans | — | `clock-frequency` 0x18CBA80 = 26 MHz |

Hepsi tutuyor, bayraklar da (level, active-low). Linux'un ölçtüğü 26.00 MHz
DTB ile aynı. GTDT'de düzeltilecek bir şey yok.

**PSCI kırık.** `PSCIv65535.65535` = `0xFFFFFFFF`, yani `PSCI_VERSION`
çağrısı "bu fonksiyon yok" dönüyor. Ama `CPU_ON` çalışıyor — sekiz çekirdek
açılıyor. Samsung'un EL3 monitörü PSCI'yi **kısmen** uygulamış: çekirdek
açma var, sürüm sorgusu yok. Linux bunu görüp omuz silkiyor ve standart v0.2
numaralarıyla devam ediyor. Windows omuz silkmez.

### Yeni ana hipotez

Windows FADT'deki `PSCI_COMPLIANT` bayrağını görüyor ve sıfırlama için
doğrudan `SMC` ile `PSCI SYSTEM_RESET` çağırıyor — UEFI'nin `ResetSystem`'ine
hiç uğramadan. Kancanın boş kalmasının sebebi bu.

**Test: bayrağı sıfırla.** `FACP.dsl` → `ARM Flags 0001` → `0000`. Windows'un
elinde sıfırlamak için tek yol kalır, o da bizim `ResetSystem`'imiz.

Maliyeti yok: `numproc 1` ile zaten tek çekirdek kullanıyoruz, `CPU_ON`'a
ihtiyacımız yok.

| Gözlem | Sonuç |
|---|---|
| Cihaz donuyor | Kanca ateşledi. Kapat–aç, `+0x80`'de `ResetStatus` duruyor |
| Yine sıfırlıyor | Sıfırlama Windows yazılımının dışından geliyor |

`B2L_HOOK_HANG` artık kaydı **yazdıktan sonra** kilitleniyor. Sıra önemli:
"hiçbir şeye dokunmadan hemen kilitlen" turu koşuldu ve cihaz yine yeniden
başladı — bu, "kanca çağrıldı ama yazarken fault verdi" ihtimalini eledi.
Geriye kancanın hiç çağrılmaması kalmıştı.

### Elenen hipotezler (güncel)

| Hipotez | Durum |
|---|---|
| WIM sıkıştırma / `WIM_NOT_BOOTABLE` | Çözüldü |
| ARMv8.1 gereksinimi | Elendi |
| Zamanlayıcı / dynamic tick | Elendi (GTDT doğrulandı) |
| UEFI → UFS yazma | Çalışıyor (LBA 0 hariç, o da başka LU) |
| `storufs.sys` UFS'i bozuyor | Doğrulandı, `_CLS` kapalı (5-6 s → 12 s) |
| Watchdog / donanım sıfırlaması | Elendi (`RST_STAT`) |
| Windows `ResetSystem` çağırıyor | **Elendi** (kanca hiç ateşlemedi) |
| EL2'de açılıyoruz | Elendi (EL1) |
| **Windows doğrudan PSCI SMC ile sıfırlıyor** | **Test ediliyor** |

## PSCI hipotezi de elendi — artık ikiye bölüyoruz

`PSCI_COMPLIANT = 0` ile Windows'un elinde sıfırlamak için tek yol bizim
`ResetSystem`'imiz kalmıştı. Kanca kaydı yazdıktan sonra kilitlenecek şekilde
kuruldu. Sonuç: cihaz yine yeniden başladı, kanca yine ateşlemedi.

Sıfırlama ne `ResetSystem`'den ne de Windows'un PSCI çağrısından geliyor.
FADT bayrağı eski haline (`0001`) döndürüldü.

### Bir şeyi yanlış kaydetmişim

"Ekran ölü" diye yazmıştım. Değil — **dönen daire çiziliyor**, yani Windows'un
`bootvid` yolu çalışıyor ve framebuffer'ımıza yazabiliyor. Bir bugcheck olsaydı
mavi ekranı aynı yoldan çizerdi. Çizmiyor. Ayrıca WinPE hive'ında
`AutoReboot = 0` yamalı; bugcheck olsaydı cihaz yeniden başlamak yerine ekranda
kalırdı. **Bu bir bugcheck değil.**

### Tükenen tanı kanalları

| Kanal | Sonuç |
|---|---|
| `sos` / `bootstatuspolicy` | Sessiz |
| `bootstat.dat` | Yazılamıyor |
| `ResetSystem` kancası | Hiç çağrılmıyor (kilitlenme moduyla doğrulandı) |
| `PSCI_COMPLIANT = 0` | Değiştirmedi |
| `RST_STAT` | Yazılım sıfırlaması, watchdog değil |
| GTDT | DTB ile birebir doğru |
| Exception level | EL1 |

Geriye tek ölçüm kaldı: **süre.** Ve bir kez gerçekten hareket etti — DSDT'den
`_CLS` kaldırılınca 5-6 saniyeden 12 saniyeye çıktı. Windows'a verdiğimiz ACPI
içeriği süreyi değiştiriyor. Artık tahmin etmiyoruz, ikiye bölüyoruz.

### İlk bölme: MADT'de tek çekirdek

Projedeki en güçlü ama hiç takip edilmemiş anomali: **`numproc 1` olmadan
Windows çekirdeği hiç başlamıyor.** İkincil çekirdek açılışı Windows'u anında
öldürüyor. Ama `numproc 1` Windows'un kaç çekirdek *kullanacağını* sınırlıyor,
MADT'de kaç çekirdek *gördüğünü* değil — Windows hâlâ sekiz işlemcilik yapı
kuruyor ve onları açmaya çalışıp zaman aşımı bekliyor.

`APIC.dsl`'de UID 1..7 → `Processor Enabled : 0`. Tek değişken bu.

| Gözlem | Sonuç |
|---|---|
| Süre değişiyor | Sorun çekirdek açılışında |
| 12 saniye aynı | Çekirdekler masum, başka yere bakarız |

### LUN 2 sorusu

`LUN=2 LBA=0` yazma hataları bizim dosyalarımızla ilgili değil. `OCS=0x07` +
`response=0x01` aygıtın reddi — Samsung boot LU'larını `bLUWriteProtect` ile
kilitler, genelde kalıcı olarak. `map -r` GPT bölümlerinin LUN 0 ve LUN 3'te
olduğunu gösterdi; LUN 2'de bölüm yok. `bootmgfw` o hataların ardından yoluna
devam edip Windows'u yüklüyor. `startup.nsh` artık `map fs0` ile fs0'ın LUN'unu
tek satırda basıyor, soru kalıcı olarak kapansın.

## Eksen değişikliği: Windows tarafına geçiliyor

ACPI'nin her parçası artık stock DTB'ye karşı doğrulandı:

| Tablo | Doğrulama |
|---|---|
| MADT GICC | MPIDR `0,1,2,3,4,5,0x100,0x101` — M4'ler cluster 1'de, DTB `cpu@0100/0101` ile birebir |
| MADT GICD | GICv2, `0x10101000`; DTB `arm,cortex-a15-gic` |
| GICC/GICH/GICV | `0x10102000` / `0x10104000` / `0x10106000` — DTB `reg` ile birebir |
| Virtual GIC Interrupt | GSIV 25 = PPI 9, DTB `<1 9 0xf04>` |
| GTDT | GSIV 29/30/27/26, 26 MHz — DTB ile birebir |
| FADT | PSCI compliant, SMC conduit (Linux 8 çekirdek açıyor) |
| PPTT | Linux `cluster=76/96/136` ile doğrulandı |

Düzeltilecek bir şey kalmadı ve Linux aynı tablolarla çalışıyor. Sorun artık
"tablomuzda hata var" değil. On beş turdur Windows tarafına hiç dokunmadık.

### ADK'nın çıplak WinPE'si

ISO'nun `boot.wim`'i 570 MB ve içinde Setup sihirbazı, sürücü deposu, dil
kaynakları var. ADK'nın WinPE'si ~250 MB ve gerçekten çıplak.

Windows'ta:

```
copype arm64 C:\WinPE_arm64
```

WSL'de:

```
bash tools_winpe_adk.sh /mnt/c/WinPE_arm64/media
bash cache_esp.sh --win
```

| Gözlem | Sonuç |
|---|---|
| Aynı ~12 saniye | Sorun platformda, Windows sürümünde değil — büyük eleme |
| Daha uzun dayanıyor | Üstüne inşa edilecek çalışan bir taban |

`tools_winpe_adk.sh` ISO yolundan ayrı tutuldu; `copype` zaten doğru ağacı
üretiyor, çıkarım/budama gerekmiyor.

### storufs'a ikinci şans — ve gerçek çözüm

Sıra: **önce** çıplak WinPE `_CLS` kapalıyken, **sonra** aynı WinPE `_CLS`
açıkken. Aynı turda ikisini değiştirmek hangisinin etki ettiğini gizler.

`storufs.sys` denetleyiciyi HCE ile resetliyor ve UniPro linkini
`DME_LINKSTARTUP` ile yeniden kuruyor. HCE PHY'yi (PMA, `0x13D64000`)
resetlemiyor — bozulan **UniPro linki**. Exynos'ta link kalibrasyonu vendor'a
özel DME nitelikleriyle yapılıyor; `UfsCal.c` tam olarak bunu yapıyor, jenerik
sürücü bilmiyor.

**Gerçek çözüm:** UEFI kontrolü bırakmadan önce PHY'yi muhafazakâr ve standart
bir moda çekerse — PWM Gear 1, tek şerit — jenerik `DME_LINKSTARTUP` çalışır.
Yavaş olur ama ilk açılış için fazlasıyla yeterli. Jenerik sürücüleri vendor
donanımında çalıştırmanın bilinen yolu bu.

Şimdiki ölçüm onun ön koşulu: hafif WinPE ile `_CLS` açıkken süre yine 5-6
saniyede kalıyorsa link kopması doğrulanır ve PWM-G1 işine girmeye değer.

## storufs'a ikinci şans: `_CLS` açık + `_CRS` sadeleştirildi

Sadece `_CLS`'i geri açmak muhtemelen yine 5-6 saniye verir ve yeni bir şey
öğretmez. Bu turda ona gerçek bir şans veriliyor.

Önceki `_CRS` dört bellek aralığı bildiriyordu:

```
0x13D60000 / 0x200     HCI
0x13D61100 / 0x200     VS_HCI  (Samsung vendor)
0x13D50000 / 0x8000    UNIPRO
0x13D64000 / 0x800     PHY PMA
```

Jenerik bir sürücü `_CRS` kaynaklarını sırayla haritalar ve `storufs.sys` ilk
bellek kaynağının **standart UFSHCI register bloğu** olmasını bekler.
Arkasından gelen üç aralık onun sözlüğünde yok — en iyi ihtimalle yok sayılır,
en kötüsünde kaynak eşlemesini kaydırır. Üstelik ilk aralığın uzunluğu `0x200`
idi, standart register seti için sınırda. Qualcomm'un ACPI'si tek aralık
bildiriyor.

Yeni hali tek aralık, `0x1000`. VS_HCI `0x13D61100`'de başlıyor, yani `0x1000`
onu kapsamıyor — kasıtlı; vendor bloğuna jenerik sürücünün işi yok, oraya UEFI
tarafında `UfsDxe` erişiyor ve `_CRS`'ten etkilenmiyor.

Önceki `_CLS` açık ölçümü (5-6 s) dört aralıklıydı, dolayısıyla süre değişirse
`_CRS`'in etkisi olduğunu bileceğiz.

### Sonraki adım: PWM Gear 1

`storufs.sys` denetleyiciyi HCE ile resetliyor ve UniPro linkini
`DME_LINKSTARTUP` ile yeniden kuruyor. HCE PHY'yi (PMA, `0x13D64000`)
resetlemiyor — bozulan **UniPro linki**. Exynos'ta link kalibrasyonu vendor'a
özel DME nitelikleriyle yapılıyor; `UfsCal.c` tam olarak bunu yapıyor, jenerik
sürücü bilmiyor.

Gerçek çözüm: UEFI kontrolü bırakmadan önce PHY'yi muhafazakâr ve standart bir
moda çekerse — PWM Gear 1, tek şerit — jenerik `DME_LINKSTARTUP` çalışır. Yavaş
olur ama ilk açılış için fazlasıyla yeterli. Jenerik sürücüleri vendor
donanımında çalıştırmanın bilinen yolu bu.

### Beklenen okuma

| Süre | Sonuç |
|---|---|
| Yine 5-6 s | `_CRS` etkisiz, link kopması doğrulandı → PWM-G1 işine değer |
| 12 s civarı | storufs artık bağlanmıyor ya da zarar vermiyor — `_CRS` önemliymiş |
| Daha uzun | Gerçek ilerleme |

### Ayrıca hazır: ADK'nın çıplak WinPE'si

`tools_winpe_adk.sh` yazıldı ama henüz kullanılmıyor. Windows'ta
`copype arm64 C:\WinPE_arm64`, sonra
`bash tools_winpe_adk.sh /mnt/c/WinPE_arm64/media`. ISO'nun 570 MB'lık
`boot.wim`'i yerine ~250 MB'lık çıplak WinPE. storufs turundan sonra sıra onda.

### Sonuç: `_CRS` gerçekten önemliymiş, storufs artık zararsız

| Yapılandırma | Süre |
|---|---|
| `_CLS` açık, `_CRS` 4 aralık | 5-6 s |
| `_CLS` kapalı (storufs hiç bağlanmıyor) | 12 s |
| **`_CLS` açık, `_CRS` tek aralık** | **12 s** |

storufs bağlanabilir durumdayken bile artık altı saniyeye mal olmuyor. Dört
aralıklı `_CRS` aktif olarak zarar veriyormuş — jenerik sürücü kaynakları
sırayla haritalayıp anlamadığı bloklara yazıyordu.

Ayıramadığımız şey: storufs şimdi düzgün mü çalışıyor, yoksa sessizce pes mi
ediyor? İkisi de aynı süreyi verir. Ama pratikte fark etmiyor — `_CLS` açık
kalıyor, depolama için zaten gerekli, ve artık bedeli yok.

**12 saniye duvarı yerinde duruyor ve artık depolamayla ilgili olmadığı
kesin.** Çekirdek sayısıyla da neredeyse ilgisiz (MADT tek çekirdekte çok az
uzadı). Duvar ACPI içeriğinden bağımsız hale geldi.

### Sıradaki: ADK'nın çıplak WinPE'si

Diğer eksen. On altı turdur Windows tarafını hiç değiştirmedik.

## Desen değişti: bu bir zaman aşımı

ADK'nın çıplak WinPE'si (Windows 11 ADK, yani WinPE 11 — dönen çember ondan)
şunu gösterdi: **daire çıkana kadar geçen süre değişiyor, daire çıktıktan
sonraki süre değişmiyor.**

Ve o ikinci süreyi şimdiye kadar hiçbir şey oynatmadı:

| Değiştirilen | Daire sonrası süre |
|---|---|
| WIM içeriği (570 MB → 250 MB) | ~12 s |
| WinPE derlemesi (ISO → ADK) | ~12 s |
| storufs açık / kapalı | ~12 s |
| MADT'de 1 / 8 çekirdek | ~12 s |
| PSCI açık / kapalı | ~12 s |

Sabit bir noktadan itibaren sabit süre. Bu "iş yapıp çöküyor" deseni değil —
**bekliyor ve süresi doluyor.**

### FADT'de 9830'dan miras kalmış bayrak

`Flags = 0x00300000`: bit 20 Hardware Reduced + **bit 21 Low Power S0 Idle
Capable**.

İkincisi Windows'a "bu platform Modern Standby destekliyor" diyor. Windows
bunu görünce güç yönetimini S3 yerine S0ix üzerine kuruyor ve platformdan
destek bekliyor: bir PEP (Power Engine Plugin) sürücüsü, işlemciler için
`_LPI` ya da `_CST`, aygıtlar için `_DSM` güç metotları. **Bizde bunların
hiçbiri yok** — DSDT'de sekiz işlemci ve UFS0 var, tek bir güç metodu bile
tanımlı değil.

Linux bu bayrağı yalnızca "s2idle uygun mu" sorusunda kullanıyor, yoksa da
normal çalışıyor. Windows için ise güç altyapısının temeli. Tam olarak
aradığımız sınıfta bir fark: **Linux hoşgörüyor, Windows hoşgörmüyor.**

Yeni değer `0x00100000` — Hardware Reduced kalıyor (ARM64'te zorunlu), Low
Power S0 Idle gidiyor.

## Hata ekranına neden hiç düşemiyoruz: UEFI değişkenleri kalıcı değil

`SiliciumPkg.dsc.inc`:

```
gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable | TRUE
```

Değişken deposu **RAM'de emüle ediliyor**, hiçbir bloğa yazılmıyor. `SetVariable`
başarılı dönüyor, veri bellekte duruyor, sıfırlamada buharlaşıyor. Platformda
FVB ya da FaultTolerantWrite sürücüsü de yok.

Windows Boot Manager önceki açılışın başarısızlığını UEFI değişkenlerine
kaydediyor. Bir sonraki açılışta okumaya gittiğinde ortada hiçbir şey yok —
**"Windows failed to start" ekranına bu yüzden hiç düşemiyoruz.** Mekanizma
bozuk değil, hafızası yok.

Bu, gerçek bir Windows kurulumu için de er geç düzeltilmesi gereken bir eksik:
kalıcı NV değişken deposu olmadan `bcdedit`, Secure Boot durumu, `BootOrder`
gibi şeylerin hiçbiri açılışlar arasında yaşamıyor. Çözümü bir bölüm üzerinde
FVB + FTW kurmak.

## `map fs0`: UFSP teorisi çürütüldü

```
FS0: VenHw(5F2C8E91-...,00)/HD(28,GPT,52444E41-494F-2044-6361-636865000000)
```

`VenHw(...,00)` = **LUN 0**, `HD(28,GPT,...)` = 28. bölüm, GUID'in ASCII
karşılığı `"ANDROID "` + `"cache"`. Dosya sistemimiz LUN 0'daki cache
bölümünde, düzgün bir GPT bölümünün içinde.

Yani `LUN=2 LBA=0` yazma hataları bizim dosya sistemimizle ilgili değil, UFSP
teorisi de yanlış yerdeydi. Değişken deposu da olamaz (emüle). Yazanın kim
olduğu hâlâ bilinmiyor ama `bootmgfw` o hataların ardından yoluna devam
ediyor — gürültü, sebep değil. Bu madde kapandı.

## Sıradaki: `bootstat.dat`'ı doğrudan oku

Windows açılış durumunu bir de **dosyaya** yazıyor: ESP'de `bootstat.dat`.
Ekrana ihtiyaç yok. En son çekirdek hiç başlamazken aramıştık, yazacak bir şey
yoktu; artık çekirdek başlıyor.

Çöküşten sonra **Odin'e girmeden**, doğrudan recovery'den:

```bash
adb shell "dd if=/dev/block/by-name/cache of=/tmp/c.img bs=4096 count=153600"
adb pull /tmp/c.img cache_read.img
bash tools_bootstat.sh cache_read.img
```
