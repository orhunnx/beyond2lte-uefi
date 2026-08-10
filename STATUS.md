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
