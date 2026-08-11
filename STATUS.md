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

## Upstream'in belgelenmiş kısıtı: 23H2 ve altı

`bootstat.dat` da çıkmadı — cache imajında Windows'un yazdığı **hiçbir şey**
yok, sadece bizim koyduklarımız. İki kanal da boş: değişkenler emüle olduğu
için buharlaşıyor, dosya hiç yazılmıyor. Windows başarısızlığı kaydedecek
noktaya ulaşmıyor.

Kör bayrak denemek yerine upstream'e bakıldı. Mu-Silicium'un `Status.md`
dosyası, **Exynos 9820** bölümünün başında:

> **Only 23H2 Windows Builds and Lower are able to Boot.**

Aynı uyarı Exynos 9830 bölümünde de var. Biz `Win11_25H2` kullanıyoruz, ADK'nın
WinPE'si de aynı nesil.

Bu, Linux'un çok önce verdiği bir ipucuyla örtüşüyor:

```
SANITY CHECK: Unexpected variation in SYS_ID_AA64ISAR0_EL1
Boot CPU: 0x10305400   CPU6: 0x10305410
```

Açılışın yapıldığı A55 ile M4 çekirdekleri farklı özellik kümesi bildiriyor.
Windows özellikleri açılış işlemcisinden okuyup tüm sisteme uyguluyor;
çekirdekler arasında tutarsızlık olan bir makinede yeni Windows'un kırılması
beklenen davranış.

### Referans portun neresindeyiz

Kardeş cihaz `beyond1lte`'nin upstream durumu:

| | Upstream `beyond1lte` | Bizim `beyond2lte` |
|---|---|---|
| Dahili depolama | ❌ | ✅ UEFI'de çalışıyor |
| Linux açılışı | ❌ | ✅ 8 çekirdek, kendi ACPI'mizle, DTB'siz |
| Windows açılışı | ❌ | `ntoskrnl` başlıyor, ~12 s |

Exynos'ta Windows'u açan kimse yok — durum tablosunda 9810/9820/9830/2400'ün
hepsinde `Windows Boot ❌`. Project Silicium'un `Guides` deposunda da
"Exynos Guides: No Guides yet."

Kopyalanacak çalışan bir ACPI yok. Ama net bir sonraki adım var:
**Windows 11 23H2 ARM64.**

### 23H2'nin `boot.wim`'i sığmıyor → LZX yeniden sıkıştırma açıldı

23H2 ISO'sundan çıkan `boot.wim` 745 MB, CACHE bölümü 600 MB. `delete 2` +
`optimize` yetmiyor çünkü Setup ile WinPE dosyaların çoğunu **paylaşıyor**;
ikinci indeksi silmek az yer kazandırıyor.

Yeniden sıkıştırmadan kaçınmam yanlış bir çıkarıma dayanıyordu. Bir tur
`export --compress=LZX` yapmıştım ve cihaz `winload.efi ... 0xc0000487`
vermişti; suçu sıkıştırmaya atmıştım. `certutil -error` o kodun
`STATUS_WIM_NOT_BOOTABLE` olduğunu söyledi — Boot Index işaretiydi. LZX'in
hiçbir suçu yoktu ve `bootmgr`'ın WIM okuyucusu XPRESS ile LZX'i sorunsuz
açıyor (açamadığı LZMS ve solid arşivler, onları kullanmıyoruz).

`tools_winboot.sh` artık 500 MB'ı aşan bir `boot.wim` görürse `export 1
--compress=LZX` ile yeniden üretiyor. `export` yalnızca 1. indeksin gerçekten
kullandığı dosyaları yazıyor, yani silinen Setup indeksinden artakalan
paylaşılmayan veri de gidiyor.

### LZX kazanç vermedi → ESP SYSTEM bölümüne taşındı

```
boot.wim 742 MB -> LZX -> 742 MB
Archiving file data: 1650 MiB of 1650 MiB
```

ISO'nun `boot.wim`'i zaten LZX ve 23H2 WinPE'sinin açılmış içeriği 1650 MiB.
Sıkıştırmayla kazanılacak bir şey yok; 600 MiB'lik CACHE'e sığmıyor.

`cache_esp.sh --system`: 5.57 GB'lık **SYSTEM** bölümünü hedefleyen 2 GiB'lik
FAT32 imajı üretiyor.

- **Odin hedefi dosya adıyla belirleniyor** — tar içindeki `cache.img` CACHE'e,
  `system.img` SYSTEM'e yazılıyor.
- **FAT32'ye geçildi.** `FatPkg/Init.c` FAT32 için en az 65525 küme istiyor;
  600 MiB'de 4K kümeyle bu sağlanamadığı için orada FAT16 + 32 KiB küme
  kullanmıştık. 2 GiB'de 4K kümeyle 524288 küme çıkıyor, fazlasıyla geçerli.
- **Bölümün tamamı doldurulmuyor.** 2 GiB'lik dosya sistemi 5.57 GB'lık bölümün
  başına yazılıyor; `FatPkg` birim boyutunu BPB'den okuyor, bölüm tablosundan
  değil. Sparse imaj olduğu için aktarılan veri yine küçük.

### `startup.nsh` artık birime bağımlı değil

SYSTEM'e yazınca cihazda iki FAT birimi oluyor — eski cache ve yeni system.
Shell `startup.nsh`'ı hangisinde bulursa onu çalıştırır ve büyük ihtimalle
eskisini bulur. Script artık sabit `fs0` yazmıyor, `fs0..fs3` taranıyor.

Windows dalı `bootmgfw.efi` yerine **`sources\boot.wim`** varlığına bakıyor:
eski cache biriminde `bootmgfw` kalmış ama WIM silinmiş olabilir, o birimi
seçersek "device not accessible" ile geri döneriz.

`for` döngüsü bilerek kullanılmadı — bu Shell sürümünün script içindeki
davranışı test edilmedi ve her deneme bir Odin flaşına mal oluyor.

## Gerileme: meşgul bekleme döngüsü

Kullanıcı hatırlattı: **en başlarda daire bir ekran sönme animasyonuyla
kayboluyordu.** Kayıtta duruyor — commit `43ef06c`, "Ekran sönme animasyonu =
çökme değil, düzgün kapanma" başlığı altında doğru yorumlanmış:

> WinPE'de bunun tek anlamı var: **kabuk sonlandı.** WinPE'nin kabuğu
> `cmd.exe` ve çıktığı anda makine yeniden başlar — tasarım böyle.
> Yani `ntoskrnl` ayakta, WinPE başlıyor, oturum açılıp kapanıyor. Çökme yok.

O an WinPE **tamamen çalışıyordu**. Sonra "kabuk çıkmasın" diye kendi
`startnet.cmd`'imiz WIM'e enjekte edildi ve şöyle bitiyordu:

```
:bekle
goto bekle
```

Tek çekirdekte (`numproc 1`) bu, `cmd.exe`'yi %100 CPU ile sonsuza kadar
döndüren bir meşgul bekleme — telefonun tamamı boş yere dönüyor. Enjeksiyondan
sonra animasyon kayboldu, yerine sert sıfırlama geldi.

**Yani en ileri noktamız on beş tur önceydi ve oraya bizim müdahalemiz zarar
verdi.** ACPI'de aradığımız hatanın bir kısmı hiç orada olmayabilir.

Yeni `startnet.cmd` hiç döngü kurmuyor: birimleri tarıyor, `WINLOG.TXT`
bırakmayı deniyor, ekrana basıyor, tek bir `pause` ile bekliyor. Girdi varsa
orada durur; yoksa düşer ve kabuk çıkar — o da animasyonlu sönme, yani başarı
işareti.

| Gözlem | Sonuç |
|---|---|
| Animasyonlu sönme | WinPE ayakta, iş bitti, kabuk çıktı |
| Sert sıfırlama | Gerçek bir çökme var |

Bu turda ayrıca `_CLS` açık ve `_CRS` sadeleştirilmiş durumda, ESP de SYSTEM
bölümünde. `WINLOG.TXT` bir birime düşerse `storufs.sys` gerçekten bağlanmış
demektir — o da uzun süredir açık olan sorunun cevabı.

### `--stok`: kendi `startnet.cmd`'imizi tamamen devre dışı bırak

Döngüsüz sürüm de sert sıfırlama verdi (16 s, ilki 5 s tekerleksiz, animasyon
yok). Yani meşgul bekleme tek suçlu değil.

Sıradaki adım en temizi: **hiç dokunmamak.** Stock WinPE'nin `startnet.cmd`'i
tek satır — `wpeinit`. Animasyonlu sönmeyi gördüğümüz an tam olarak o
haldeydik, henüz hiçbir şey enjekte etmemiştik.

Enjeksiyonu atlamak yetmiyor: `esp/sources/boot.wim`'in içinde bizim dosyamız
zaten duruyor ve atlamak onu silmiyor. `--stok` bayrağı stock içeriği geri
yazıyor, böylece WIM'i baştan üretmeye (LZX, dakikalar) gerek kalmıyor.

```bash
bash tools_winboot.sh ~/Win11_23H2_Arm64.iso --stok
bash cache_esp.sh --win --system
```

| Gözlem | Sonuç |
|---|---|
| Animasyonlu sönme geri geldi | Suçlu bizim script; WinPE sağlam, oradan ilerlenir |
| Yine sert sıfırlama | Gerileme başka bir değişiklikte — `_CLS`, `numproc`, ESP konumu ya da 23H2 |

## Bilinen iyi noktaya dönüş (`43ef06c`)

"O zaman neyi farklı yapmıştık?" sorusunun cevabı: **yedi sekiz şeyi.**
`git diff 43ef06c HEAD` ile çıkarıldı.

| Değişiklik | Animasyon anında | Şimdi (geri alındı) |
|---|---|---|
| FADT `Low Power S0 Idle` | 1 | 1 |
| MADT UID 1..7 | Etkin | Etkin |
| DSDT `_CLS` | Var | Var |
| DSDT `_CRS` | 4 aralık | 4 aralık |
| `B2LResetHookDxe` | Yok | Yok |
| Bellek haritasında `B2L Log` (`0xCF000000`, RtData) | Yok | Yok |

Son iki satır önemli: kanca sürücüsü Windows'un gördüğü **bellek haritasına
yeni bir çalışma zamanı bölgesi** ekliyordu. Kanca zaten hiç ateşlemedi, yani
bize hiçbir şey kazandırmadan Windows'a yeni bir şey gösteriyordu.

`PcdFrameBufferDelay|0` bilerek korundu — Windows'un gördüğü hiçbir şeyi
değiştirmiyor, sadece açılışı hızlandırıyor.

Windows tarafında da geri dönülecekler var: o gün **25H2** vardı ve ESP
**CACHE**'te FAT16'ydı; şu an 23H2 ve SYSTEM'de FAT32. `startnet.cmd` ise
`--stok` ile devre dışı.

## Doğru hedef `43ef06c` değil, `43ef06c^`

`43ef06c` animasyonu **gördüğümüz** commit değil, ona **tepki verdiğimiz**
commit — `startnet.cmd` enjeksiyonu tam orada eklendi. Animasyonu üreten
yapılandırma bir öncesi.

```
git diff --stat 43ef06c^ 43ef06c
 windows/startnet.cmd | 64 ++++++++++
```

Aradaki tek fark o dosyanın eklenmesi; ACPI ve firmware bire bir aynı. Yani
`--stok` ile dosyayı devre dışı bıraktığımız için ACPI tarafında zaten doğru
yerdeydik. Ama o noktada olmayan üç şey daha uygulanıyordu:

| Şey | Ne yapıyordu | Durum |
|---|---|---|
| `tools_ufs_patch.py` | `UfsDxe.c`'ye önbellek bakımı + `B2L UFS:` çıktısı | `yap.sh`'te kapatıldı |
| `BoardInit.c` | `B2L RESET:` DEBUG satırları | Geri alındı |
| `UfsDxe.inf` | `CacheMaintenanceLib` eklentisi | Geri alındı |

İlki önemli: UEFI tarafını değiştiriyor, yani Windows devraldığında UFS
denetleyicisinin **bulunduğu durumu** değiştirebilir.

`PcdFrameBufferDelay|0` bilerek korundu — Windows'un gördüğü hiçbir şeyi
değiştirmiyor, sadece DXE günlüğünün ekrana akış hızını etkiliyor.

BCD tarafında da o gün `tekcpu` varyantı vardı (`13f67c1`: "numproc 1 çekirdeği
başlattı; BCD legacy boot + bootux kapalı"), `hatagoster` değil.

## Hazır ama bağlanmadı: `EbsTest`

`Applications/EbsTest/EbsTest.c` yazıldı, henüz `.dsc`'ye eklenmedi.

`ExitBootServices` çağırıp **hiçbir şey kurmayan** bir kalp atışı: boot
servisleri kapandıktan sonra geriye yalnızca framebuffer'a yazan bir döngü
kalıyor. Ne zamanlayıcı, ne kesme, ne sürücü. Her tik ekrana bir kare
çiziyor, yani kendi kendini zamanlıyor — cihaz sıfırlanınca kareleri saymak
yetiyor.

Linux `ExitBootServices` çağırıp 120 saniye yaşıyor ama her şeyi kuruyor.
Windows çağırıp 15 saniyede ölüyor. Aradaki üçüncü durum hiç denenmedi:

| Gözlem | Sonuç |
|---|---|
| ~15 saniyede sıfırlıyor | Sorun Windows'ta **değil**; platform servis edilmeyen bir şeye takılıyor ve Linux onu tesadüfen tatmin ediyor |
| Dakikalarca yaşıyor | Sıfırlama gerçekten Windows'a özel |

Geri dönüş de tutmazsa sıradaki adım bu.

### Geri dönüş eksikti: `UfsDxe.c` yamalı kalmıştı

Cihazda `B2L UFS:` satırları çıkmaya devam etti. Sebep: `tools_ufs_patch.py`
yamayı `$MU` içindeki `UfsDxe.c`'ye **kalıcı olarak** uyguluyor. Scripti
çağırmayı bırakmak yamayı geri almıyor — dosya orada yamalı duruyor. Overlay
o dosyayı kopyalamadığı için (sadece `.inf` var) tek yol upstream sürümü
git'ten geri çağırmak.

`yap.sh` artık overlay kopyalamadan önce şunu yapıyor:

```bash
git -C "$MU" checkout -- Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.c
```

Bu, "yamayı kapattım" demenin yeterli olmadığı ikinci durum. İlki `yap.sh`'in
`ACPI.inc`'i depoda kirletmesiydi.

### `EbsTest` derlemeye bağlandı

Sadece `.dsc`'de, **FDF'ye eklenmedi** — firmware'e gömülmediği için normal
açılış yolunu hiç etkilemiyor. `yap.sh` üretilen `.efi`'yi `out/` altına
kopyalıyor; test etmek isteyince `esp/` içine elle konuyor.

```bash
cp out/EbsTest.efi esp/EBSTEST.EFI && bash cache_esp.sh --win   # test
rm esp/EBSTEST.EFI                                              # geri al
```

`startup.nsh` her şeyden önce `EBSTEST.EFI`'ye bakıyor; dosya yoksa atlıyor.

### `EbsTest` ilk deneme: hiç kare görülmedi — iki hata

**Bekleme döngüsü çok kısaydı.** 8 milyon `volatile` artırma bu çekirdekte
saniye değil ~10 ms sürer; kareler göz açıp kapayana kadar ekranı doldurup
biterdi. 500 milyona çıkarıldı.

**Daha önemlisi: nerede durduğunu ayırt edemiyorduk.** `ExitBootServices`'ten
önce mi sonra mı öldüğü belli değildi ve `Print()` işe yaramıyor — EBS'ten
sonra konsol yok.

Çözüm: her aşama için ekranda sabit yerde bir renk şeridi. Konsola hiç bağlı
değil; ekranda ne kaldıysa uygulamanın tam olarak nereye kadar geldiğini
söylüyor.

| Ekranda | Anlamı |
|---|---|
| Kırmızı yok | Uygulama hiç çalışmadı ya da GOP bulunamadı |
| Kırmızı var, mavi yok | `GetMemoryMap` patladı |
| Mavi var, sarı yok | `ExitBootServices` **başarısız** (konsol hâlâ var, hata metni de olmalı) |
| Sarı var, kare yok | EBS geçti ama hemen sonrasında ölüyoruz |
| Sarı + kareler | EBS geçti, döngü dönüyor — **kareleri say** |

Sarı şeridin sağ ucunda yanıp sönen bir işaret de var: kareler ekrandan
taştıktan sonra döngünün hâlâ döndüğü görülebilsin diye.

### Ayrıca doğrulandı: LBA 0 yazma denemesi upstream'den geliyor

Yama temizlendikten sonra `B2L UFS:` satırları kayboldu ama
`UFS OCS=0x07 response=0x01 type=0x21` üç kez çıkmaya devam etti. O satır
`UfsDxe.c`'nin kendi uyarısı. Yani LUN 2 LBA 0'a yazma denemesi bizim
enstrümantasyonumuzdan değil, upstream UEFI'nin kendi davranışından geliyor.

### `EBSTEST.EFI` çalışmadı: iki kaynak çelişiyor

`cache_esp.sh` dosyayı kopyaladığını bildirdi, ama cihazda
`if exist fs0:\EBSTEST.EFI` yanlış döndü — uygulama hiç çalışmadı ve akış
doğrudan Windows'a geçti ("Loading files").

`startup.nsh`'in kendisi doğru: `EBSTEST.EFI` bloğu `pause`'dan hemen sonra,
Linux ve Windows dallarından önce. Aynı dosyadaki `if exist fs0:\sources\boot.wim`
çalıştığına göre ne `fs0` yanlış ne `if exist` bozuk.

Tahmin etmek yerine cihaza sorduruyoruz: `ls fs0:\`. Dosya listede varsa sorun
ifadede, yoksa imaja hiç girmemiş demektir.

Aynı turda `KANCA` dökümü kaldırıldı — `B2LResetHookDxe` geri alındığı için
`0xCF000000` artık bellek haritasında yok, o adresi okumanın anlamı kalmadı.
`map` de `map fs0` oldu; tam liste ekranı taşırıyordu.

## Araştırma: Exynos'ta Windows'u açan kimse yok

- `sonic011gamer/Mu-Samsung` — tam bu iş için kurulmuş ("Project Mu UEFI
  Implementation for Samsung Exynos Devices"), geliştirme Mu-Silicium'a
  taşınmış.
- Mu-Silicium `Status.md`: Exynos 9810 / 9820 / 9830 / 2400 ailelerinin
  **hepsinde** `Windows Boot ❌`.
- `Project-Silicium/Guides`: "Exynos Guides: No Guides yet."
- Basındaki "Samsung'a Windows kuruldu" haberlerinin tamamı **Snapdragon**
  modelleri (S8 ve diğer 835'liler).

Kopyalanacak çalışan bir referans yok. Bulduğumuz her hata yeni.

### RKP hipotezi: kuruldu ve aynı turda elendi

Astarasikov'un 9820 tersine mühendislik yazısı RKP hipervizörünün bu yongada
var olduğunu doğruluyor ve PIT'te `UH` (2 MB) / `UHCFG` (4 MB) bölümleri
duruyor. EL2'de bir hipervizörün sayfa tablosu ihlalinde cihazı sıfırlaması,
gözlemlerimizin hepsiyle örtüşüyordu.

Ama Impalabs'in RKP incelemesi bunu kapatıyor:

> "SBOOT boots the Linux kernel in **EL1**, with a backdoor in TrustZone
> implemented to load and execute custom code in EL2. Samsung's kernel uses
> `init/vmm.c` to load the proprietary RKP hypervisor **through an SMC call
> interface**."

RKP kendiliğinden yüklenmiyor — **Samsung'un çekirdeği onu kendisi
yüklüyor.** Bizim UEFI'miz yüklemiyor, Windows da yüklemiyor. Linux'un
"started at EL1" demesinin sebebi RKP değil, S-Boot'un zaten EL1'de teslim
etmesi.

Mu-Silicium'un kendi kodu da bunu destekliyor:
`Sec/AArch64/ModuleEntryPoint.S` `EL1_OR_EL2` makrosuyla bir seviye
dayatmıyor, S-Boot hangi seviyede bırakırsa ona göre vektör kuruyor.
Bellek haritasındaki `EL2 Code` bölgesi diğer portlardan miras bir rezerv.

**Sonuç: ortada çalışan bir hipervizör yok, EL2 boş. RKP sıfırlamayı yapmıyor.**

`EbsTest` yine de `CurrentEL`'i hem konsola hem ekrana basıyor (kırmızı şeridin
solunda EL sayısı kadar beyaz kare) — akıl yürütmeyi ölçümle kapatmak için.

### Yan kazanç: EL2'ye çıkmanın belgelenmiş bir yolu var

Aynı kaynak, TrustZone'da EL2'ye kod yükleyen bir arka kapı olduğunu ve
Samsung'un bunu SMC arayüzüyle kullandığını söylüyor.
`sleirsgoevy/exynos-kvm-patch` aynı yolu Exynos telefonlarda KVM açmak için
kullanıyor — yani çalışan örnek kod mevcut.

Bu, daha önce "EL1'deyiz, yapamayız" diye kapattığım planı geri açıyor: EL2'ye
kendi kodumuzu koyabilirsek `HCR_EL2.TSC` ile Windows'un her `SMC`'sini ve
istisnasını yakalayabiliriz. Büyük iş ama artık imkânsız değil.

### Diğer 9820 projeleri (donanım referansı olarak değerli)

| Proje | Ne işe yarar |
|---|---|
| `exynos9820-mainline/linux` | Mainline Linux portu — UFS, PHY, saat sürücüleri için referans |
| `exynos9820-mainline/uniLoader` (ivoszbg) | Alternatif önyükleyici |
| `exynos9820-mainline/kexec-boot-tools` | kexec ile açılış |
| `ExtremeXT/android_kernel_samsung_exynos9820` | Stock çekirdek kaynağı |
| `sleirsgoevy/exynos-kvm-patch` | **EL2 erişimi**, TrustZone arka kapısı |

## Beş turluk kafa karışıklığının sebebi: iki ESP

Depo temiz, commit push'lu, `esp/startup.nsh` doğru sürüm
(`sha1=a0e16fd69435`, içinde `KOKU` ve `EBSTEST` var). Dosya cihaza gidiyor.
Ama cihaz **başka bir `startup.nsh` çalıştırıyor.**

Bir noktada ESP'yi SYSTEM bölümüne flaşlamıştık — 23H2'nin 742 MB'lık WIM'i
600 MiB'lik CACHE'e sığmıyordu. Sonra CACHE'e geri döndük, ama **SYSTEM'de
bıraktığımız tam ESP orada durmaya devam etti**, içindeki eski `startup.nsh`
ile birlikte.

EFI Shell `startup.nsh`'ı hangi FAT biriminde önce bulursa onu çalıştırır. İki
birim vardı ve eskisini buldu.

Bu tek şey son beş turun hepsini açıklıyor:

| Gözlem | Gerçek sebep |
|---|---|
| Eski `map` tam tablosu, `fs0 KOKU` yok | Eski script çalışıyor |
| `B2L UFS` satırları kaybolmuyor | Yeni firmware test edilmedi |
| `EBSTEST.EFI` bulunamıyor | Farklı birimde arıyor |
| Süre hep 15 saniye | Hiçbir değişikliğimiz devrede değil |

**Yani `43ef06c^`'ya geri dönüş, `_CRS` sadeleştirmesi, `Low Power S0 Idle`,
MADT tek çekirdek — hiçbirinin gerçekten test edilip edilmediği belirsiz.**
Değişkenlerin bir kısmını yeniden ölçmek gerekecek.

`cache_esp.sh --system --bos`: bölümün başına 16 MiB sıfır yazıyor. FAT
önyükleme sektörü gidince birim hiç tanınmıyor ve ortada tek bir
`startup.nsh` kalıyor.

## Güvenilir referans: 15 saniye

Doğrulanmış hatla (tek ESP, `fs0 KOKU` ekranda, `EBSTEST.EFI` bulunup
çalışıyor) Windows ölçümü **15 saniye**. Eski ölçümlerle aynı — yani o turlar
tamamen boşa gitmemiş, ama artık sayı gerçek.

### Mantık sıkıştı: bu kontrollü bir kapanış olmak zorunda

| Bilgi | Nasıl biliyoruz |
|---|---|
| Platform EBS sonrası bir dakikadan uzun yaşıyor | `EbsTest`, 63 kare |
| Cihazı dışarıdan sıfırlayan bir şey yok | Aynı ölçüm |
| Sıfırlamayı Windows çalıştırıyor | Yukarıdakinin doğrudan sonucu |
| `ResetSystem`'den geçmiyor | Kanca hiç ateşlemedi; sürücünün çalıştığı `B2LI` sayacının artmasıyla doğrulandı |

Geriye tek yol kalıyor: **HAL'in doğrudan `SMC` ile PSCI çağırması.** Bu da
şu demek — çöküş kontrollü bir kapanış, yani büyük ihtimalle bir **bugcheck**.

### Kapatılmamış boşluk: `AutoReboot=0` hiç doğrulanmadı

"Mavi ekran hiç görmüyoruz" gözleminin tamamı bu ayara dayanıyor. Uygulanmamış
olsaydı bugcheck zaten cihazı yeniden başlatırdı ve görmememiz normal olurdu —
yani "çöküş bugcheck değil" çıkarımı temelsiz kalırdı.

Bir turda çıktıda `hivexregedit yok` uyarısı geçti ve o WIM'in yamalanıp
yamalanmadığı hiç doğrulanmadı.

`tools_winboot.sh` artık:

- Yazdıktan sonra hive'ı WIM'den **geri okuyup** `CrashControl\AutoReboot`
  değerini ekrana basıyor
- `hivexregedit` yoksa sessiz geçmiyor, çerçeveli bir uyarı basıyor

## Script hatası: WIM içerik yamaları hiç çalışmıyordu

`tools_winboot.sh` çıktısında hiçbir doğrulama satırı çıkmadı. Sebep yapısal:

`hivexregedit` hive yaması ve `startnet.cmd` enjeksiyonu, **WIM'i yeniden üreten
dalın içindeydi**. `--wim-yenile` verilmediğinde script "boot.wim hazir,
atlaniyor" deyip o dalın tamamını geçiyordu. Sonuç:

- `AutoReboot=0` hive yaması **hiç uygulanmıyordu**
- `--stok` bayrağı **hiçbir iş yapmıyordu** — stock `startnet.cmd` hiç yazılmadı
- Doğrulama satırları basılmıyordu, ekranda "hiçbir şey yazmıyor"

WIM'i yeniden üretmek ile içeriğine dokunmak bağımsız işler; ikisini aynı koşula
bağlamak birkaç turluk ölçümü şüpheli hale getirdi. Blok dışarı taşındı, artık
`boot.wim` varsa her çalıştırmada uygulanıyor.

Bu, aynı sınıftan **üçüncü** hata:

| Hata | Ortak yanı |
|---|---|
| `yap.sh` yamayı kapattı ama `$MU/UfsDxe.c` yamalı kaldı | "Kapattım" demek yetmedi |
| SYSTEM'deki eski ESP silinmedi, Shell onu çalıştırdı | Eski durum sessizce hayatta kaldı |
| `--stok` ve hive yaması atlanan dalın içindeydi | Bayrak vardı ama etkisi yoktu |

Üçünde de araç "yaptım" diyordu, cihaz eskisini kullanıyordu. Bundan sonra her
yamanın **geri okunarak doğrulanması** kural.

Taşıma sırasında bir tuzak: blok içinde `<<'REG'` here-doc var; girintisi
değişirse kabuk sonlandırıcıyı göremiyor (`unexpected end of file`). Girinti
korunarak taşındı.

### `hivexregedit` `libhivex-bin`'de değil

`sudo apt install libhivex-bin` yapıldı ama script yine "hivexregedit yok"
dedi. Çünkü `hivexregedit` o pakette değil — bir Perl scripti ve
**`libwin-hivex-perl`** paketiyle geliyor. `libhivex-bin` sadece `hivexsh`,
`hivexget` ve `hivexml` veriyor.

`tools_winboot.sh` artık ikisiyle de çalışıyor:

| Araç | Ne zaman | Nasıl |
|---|---|---|
| `hivexregedit` | Varsa (tercih edilen) | `.reg` birleştirme |
| `hivexsh` | Yoksa | `setval` ile aynı değerler |
| `hivexget` | Doğrulama | `libhivex-bin`'de, her durumda var |

Doğrulama artık `.reg` dışa aktarımı yerine `hivexget` ile yapılıyor ve iki
değeri de basıyor:

```
WIM'den geri okundu: AutoReboot=0  DisplayParameters=1
DOGRULANDI: bugcheck olursa cihaz ekranda kalacak
```

## Bugcheck de elendi — gözlem kanalları tükendi

`AutoReboot=0` doğrulanarak uygulandı (WIM'den geri okundu) ve cihaz yine
sıfırlıyor. Bugcheck olsaydı ekranda kalırdı. **Çöküş bir bugcheck değil.**

### Ölçülmüş tablo

| Bilgi | Kanıt |
|---|---|
| Platform EBS sonrası >60 s sağlam | `EbsTest`, 63 kare |
| Sıfırlamayı Windows çalıştırıyor | Yukarıdakinin doğrudan sonucu |
| `ResetSystem`'den geçmiyor | Kanca ateşlemedi; sürücünün çalıştığı `B2LI` sayacıyla doğrulandı |
| PSCI bayrağı kapalıyken de değişmiyor | FADT `PSCI_COMPLIANT = 0` testi |
| Bugcheck değil | `AutoReboot=0` doğrulandı, yine sıfırlıyor |
| ACPI'nin tamamı doğru | MADT/GTDT/FADT/PPTT, stock DTB'ye karşı |
| Hiçbir yere kayıt düşmüyor | `bootstat.dat` yok, NV değişkenler emüle |
| WinPE sürümü/içeriği fark etmiyor | ISO 25H2, ISO 23H2, ADK WinPE — hepsi ~15 s |
| Çekirdek sayısı fark etmiyor | MADT 1 ve 8 çekirdek |
| storufs fark etmiyor | `_CLS` açık/kapalı, `_CRS` sade/geniş |

Windows `ntoskrnl` başladıktan ~12 saniye sonra, sabit bir noktada, sessizce,
elimizdeki her kayıt ve kanca mekanizmasını atlayarak cihazı yeniden
başlatıyor.

### Neden buradan sonrası farklı bir iş

Ne yaptığını göremediğimiz sürece kör bayrak deniyoruz. Son yirmi turun büyük
kısmı buydu ve **üçü araç hatası çıktı** (yamalı `UfsDxe.c`, SYSTEM'deki eski
ESP, atlanan WIM içerik yamaları). Gözlem olmadan ilerleme rastlantıya kalıyor.

Geriye üç gerçek yol kalıyor, üçü de tek turluk deneme değil:

1. **EL2 + `HCR_EL2.TSC`** — TrustZone arka kapısıyla EL2'ye kendi kodumuzu
   koyup Windows'un her `SMC`'sini ve istisnasını yakalamak.
   `sleirsgoevy/exynos-kvm-patch` bunun bu SoC ailesinde yapılabildiğini
   gösteriyor. Tek gerçek gözlem aracı.
2. **Kalıcı NV değişkenler (FVB + FTW)** — Windows'un kendi açılış durumu
   raporlamasını geri getirir. Gerçek bir Windows kurulumu için zaten gerekli.
3. **UFS'i Windows altında çalıştırmak** — PHY'yi PWM Gear 1'e çekip
   `storufs.sys`'i gerçekten bağlamak. Depolama olmadan telefon zaten
   kullanılamaz.

## EL2'ye çıkış: TrustZone arka kapısı

Gözlem kanalları tükendiği için EL2'ye çıkıp Windows'un `SMC`'lerini
tuzaklamaya karar verildi. Arayüz `sleirsgoevy/exynos-kvm-patch`'te
belgelenmiş ve Samsung'un kendi çekirdeği RKP'yi bununla yüklüyor
(`init/vmm.c`):

```
x0 = 0xC2000400   VMM_64BIT_SMC_CALL_MAGIC
x1 = entry_point  EL2'de çalışacak rutinin FİZİKSEL adresi
x2 = 4096         VMM_STACK_OFFSET
x3 = 1            VMM_MODE_AARCH64
x4, x5            rutine x0/x1 olarak geçer
smc #0
```

Rutin EL2'de çalışır, `smc #0` ile `x0=0xC2000401, x1=status` diyerek döner.
**`status` sıfır olmak zorunda** — değilse firmware HYP durumunu varsayılana
sıfırlıyor.

Üç belgelenmiş kısıt, tasarımı belirledi:

1. Init yalnızca açılış çekirdeğinde çalışıyor; diğerleri EL2 durumunu
   kopyalıyor, tek fark `sp = bootcore.sp + 4096 * çekirdek_no`. Yığın bölgesi
   sekiz çekirdeklik ayrıldı.
2. Firmware `vbar_el2`'yi çıkışta kaydedip rastgele noktalarda geri yüklüyor —
   yani vektör sonradan değiştirilemez ama **koyduğumuz yerde kalıyor**, ki
   bize gereken tam olarak bu.
3. EL2'de MMU kapalı, adresler fiziksel. Kod ve tampon sabit fiziksel
   adreslerde; Windows'un bu bölgeyi haritalamasına gerek yok, sadece
   dokunmaması gerekiyor → `Reserv`, `RtData` değil.

### Yerleşim (`0xCF000000`, 256 KB)

| Adres | İçerik |
|---|---|
| `0xCF000000` | İstisna vektörü (VBAR_EL2, 2 KB hizalı) |
| `0xCF000800` | Init rutini — SMC'ye `entry_point` olarak bu veriliyor |
| `0xCF010000` | Yığın tepesi; çekirdek N → `+4096*N` |
| `0xCF020000` | Bu açılışın SMC kaydı |
| `0xCF030000` | Önceki açılışın kaydı — Shell burayı okuyor |

Vektör tablosu dosyanın başında: `0xCF000000` zaten 2 KB hizalı olduğu için
hizalama numarasına gerek kalmıyor ve yük tek parça kopyalanabiliyor.

### Kayıt biçimi

```
+0x00  u32  'B2L2'  (dmem'de 42 32 4C 32)
+0x04  u32  yakalanan SMC sayısı
+0x10  kayıtlar, her biri 32 bayt: x0, x1, x2, ELR_EL2
```

Aradığımız: `x0 = 0x84000009` (PSCI `SYSTEM_RESET`). Çıkarsa Windows cihazı
kendi isteğiyle sıfırlıyor demektir ve `ELR_EL2` çağrıyı yapan kodun adresini
verir.

### İki ince nokta

**`ELR_EL2` ilerletilmeli.** Tuzaklanan SMC'de `ELR_EL2` komutun kendisini
gösterir; ilerletmezsek aynı komuta dönüp sonsuz tuzağa gireriz.

**EL2'den yapılan SMC tuzaklanmaz.** `HCR_EL2.TSC` yalnızca EL1'i bağlıyor, o
yüzden çağrıyı EL3'e iletirken sonsuz döngü oluşmuyor.

## Windows zincirini azaltmak: açılış sürücüleri

Soru: "boot zincirindeki Windows aşamalarını tek tek azaltsak sorunu neyin
çıkardığına gitmez miyiz?"

`bootmgfw → winload → ntoskrnl` üçlüsü atlanamaz; biri olmadan diğeri
çalışmıyor. Ama `winload`'un `ntoskrnl`'i başlatırken yaptığı bir iş var ve tam
şüpheli pencereye denk geliyor: **açılış sürücülerini yüklemek.**

`SYSTEM` hive'ında `ControlSet001\Services` altında her sürücünün `Start`
değeri var:

| Değer | Anlamı |
|---|---|
| 0 | BOOT — `winload` yüklüyor, çekirdekten önce |
| 1 | SYSTEM — çekirdek açılırken |
| 2 | AUTO | 
| 3 | DEMAND |
| 4 | DISABLED |

Sıfır olanları kapatarak ikiye bölebiliriz. Desen bunu destekliyor: daire
çıktıktan sonraki ~12 saniye hiçbir şeyle değişmiyor — WIM içeriği, WinPE
sürümü, çekirdek sayısı, PSCI, storufs. Yani sabit bir iş yapılıyor ve o işin
içinde bir şey oluyor. PnP'nin aygıtları tarayıp sürücü bağladığı pencere tam
orası.

`tools_winpe_surucu.sh`:

```bash
bash tools_winpe_surucu.sh liste              # önce bu
bash tools_winpe_surucu.sh kapat a b c        # ikiye böl
bash tools_winpe_surucu.sh ac    a b c        # geri al
```

Korumalı liste var (`ACPI`, `Wdf01000`, `volmgr`, `partmgr`, `disk`, `CNG`...)
— onlarsız çekirdek hiç başlamaz ve elimizde "açılmadı"dan başka bilgi kalmaz.
`kapat` bunlara dokunmuyor.

Her yama WIM'e konduktan sonra **geri okunup doğrulanıyor**; bu projede üç kez
"araç yaptım dedi, cihaz eskisini kullandı" yaşandı.

### `startup.nsh`: EL2 dökümü `pause`'un önüne alındı

EL2 kaydı ekranın en üstündeydi ve üstteki bölümler taşırdığı için
okunamıyordu. Artık `pause`'dan hemen önce.

## TrustZone sürücüleri elendi

`WindowsTrustedRT`, `WindowsTrustedRTProxy`, `TPM`, `QcTrEE_i`, `qcgpio_i`
kapatıldı — süre yine 15 saniye.

Bu, "doğrudan `SMC`" hipotezini de **zayıflatıyor**. Mantık zinciri
sıfırlamanın EL3'e giden bir `SMC`'den gelmesi gerektiğini söylüyordu ve
Windows'ta `SMC` atan açılış sürücüleri bunlardı. Kapatınca hiçbir şey
değişmedi.

### Sıradaki: hepsini birden kapat

Grupları ayrı ayrı denemek her biri için bir Odin flaşı. `hepsi-kapat`
korumalı olmayan otuz dokuz sürücünün tamamını devre dışı bırakıyor:

| Gözlem | Sonuç |
|---|---|
| Süre değişiyor | Suçlu açılış sürücülerinin arasında; ikiye bölerek birkaç flaşta tek isme inilir |
| 15 saniye aynı | **Açılış sürücüleri masum.** Çöküş `winload`'un sürücü yükleme aşamasında değil, `ntoskrnl`'in kendi iç işleyişinde. Bütün kategori tek flaşla kapanır |

Korumalı liste dışarıda: `ACPI`, `acpiex`, `CNG`, `Wdf01000`, `FltMgr`,
`volmgr`, `partmgr`, `disk`, `mountmgr`, `pdc`, `vdrvroot`, `spaceport`,
`volume`, `volsnap`. Onlarsız çekirdek hiç başlamaz ve elimizde "açılmadı"dan
başka bilgi kalmaz.

## Sayı ilk kez oynadı: 15 → 5 saniye

`hepsi-kapat` ile korumalı olmayan 39 açılış sürücüsü kapatıldı ve süre
**15 saniyeden 5 saniyeye düştü.** Yirmi beş turdur sabit duran sayı ilk kez
değişti — sürücü kümesi gerçekten etkili, mekanizma çalışıyor.

Ama yön yanlış: daha erken ölüyor, daha geç değil. Sebep korumalı listenin
eksik olması. **WinPE RAM diskteki bir WIM'den çalışıyor** ve o WIM'i bağlayan
sürücüleri de kapatmışız:

| Sürücü | İşi |
|---|---|
| `WimFsf` | WIM dosya sistemi filtresi — açılış birimi bununla görülüyor |
| `FBWF` | File-Based Write Filter, WinPE'nin RAM üzeri yazma katmanı |
| `Wof` | Windows Overlay Filter |
| `Fs_Rec` | Dosya sistemi tanımlayıcı |
| `Mup` | G/Ç yığınının üst katmanı |
| `FileInfo` | Filtre yöneticisi bağlantısı |
| `CLFS` | Çekirdeğin günlük altyapısı |
| `KSecDD` / `KSecPkg` | Çekirdek güvenlik sürücüleri |
| `pcw` | Performans sayaçları, çekirdek erken istiyor |

Bunlar korumalıya eklendi. Geriye **sadece donanım sürücüleri** kalıyor —
`DXGKrnl`, `pci`, `Tcpip`, `NDIS`, `storahci`, `stornvme`, `storufs`, `TPM`,
`vmbus`, `vpci` gibi. Asıl şüpheliler zaten onlar; küme hem küçüldü hem
anlamlandı.

Not: `0xCF030000`'daki EL2 kaydında `B2L2` imzası yok, döküm çöp. EL2 arka
kapısı henüz çalışmıyor — ayrı bir iş olarak duruyor.

### Araç körlüğü: `liste` sadece açık olanları gösteriyordu

`hepsi-kapat` ikinci kez çalıştırılınca `hedef:` boş çıktı. Sebep: donanım
sürücüleri zaten `Start=4` idi ve `liste` yalnızca `Start=0` olanları
basıyordu — kapattıklarımız hiç görünmüyordu. Neyin kapalı olduğunu görmeden
ikiye bölme yapılamaz.

`liste` artık `Start` değeri 0 **ya da** 4 olan her sürücüyü `AÇIK` / `KAPALI`
etiketiyle gösteriyor ve sayıları basıyor.

Bu arada WIM istenen durumda: donanım sürücüleri kapalı, WinPE'nin dosya
sistemi katmanı (`WimFsf`, `FBWF`, `Wof`, ...) açık. Ölçüm yapılabilir.

### `sadece-kapat`: doğru yöndeki bisection

"Hepsini kapat" yöntemi bir tuzağa düştü. O yöntem **"hangi sürücü
çökertiyor"u** değil, **"hangisi olmadan Windows daha erken pes ediyor"u**
buluyor:

| Yapılandırma | Süre |
|---|---|
| Hepsi açık (temel) | 15 s |
| Hepsi kapalı (WinPE katmanı dahil) | 5 s |
| Hepsi kapalı (WinPE katmanı açık) | 6 s |
| TrustZone beşlisi kapalı | 15 s |

**15 saniyeyi geçen hiçbir yapılandırma yok.** Sadece daha kötüsünü bulduk.

Doğru yön tersi: tam çalışan kümeden **tek bir sürücüyü** çıkarmak. Süre
uzarsa suçlu odur; değişmezse masumdur ve temiz elenir.

```bash
bash tools_winpe_surucu.sh sadece-kapat DXGKrnl   # hepsi açık, sadece bu kapalı
bash tools_winpe_surucu.sh hepsi-ac               # temel duruma dön
```

Elle yapmak otuz üç isim yazmak demekti; her deneme için tekrar, hata payı
yüksek.

## Windows ilk kez konuştu — ve bu bir araç hatasından çıktı

```
File:   \windows\System32\Drivers\ws2ifsl.sys
Status: 0xc0000428          (STATUS_INVALID_IMAGE_HASH)
Info:   the digital signature of a file or one of its dependencies
        couldn't be verified
```

`sadece-kapat`'ın ilk sürümü "korumalı olmayan ve `Start` değeri 0 **ya da 4**
olan her şeyi aç" diyordu. Ama `cdfs`, `udfs` ve `ws2ifsl` WinPE'de
**Microsoft tarafından zaten kapalı** gelir — hiç açılış sürücüsü değiller.
Açınca `ws2ifsl.sys` imza zinciri doğrulanamadı ve `winload` açılışı durdurdu.

### Asıl kazanç

Otuz turdur "Windows sessizce yeniden başlıyor, hiçbir şey söylemiyor"
diyorduk. **Hata bildirme yolu bozuk değilmiş.** `winload` aşamasında bir
sorun olduğunda ekranı gayet düzgün basıyor: dosya adı, NTSTATUS, açıklama.

Bu şunu kanıtlıyor: sessiz 15 saniyelik çöküş, `winload`'un hata yakalama
alanının **dışında** — yani `ntoskrnl` çalışmaya başladıktan sonra, artık
`winload`'un rapor edemeyeceği bir noktada. Aramayı orada daraltmak gerekiyor.

### Araç düzeltildi

- `TEMEL_ACIK`: ilk `liste` çıktısındaki 54 isim sabit olarak kaydedildi.
  Artık yalnızca bu listeye dokunuluyor; listede olmayan hiçbir servisin
  `Start` değeri değiştirilmiyor.
- `cdfs`, `udfs`, `ws2ifsl` her çalıştırmada açıkça `Start=4`'e çekiliyor.

## Kritik araç hatası: `hivexsh setval` diğer değerleri siliyordu

`hivexsh`'in `setval` komutu bir düğümde değer atarken **o düğümdeki diğer
bütün değerleri siler.** Yani her `kapat`/`ac` çağrısında ilgili servisin
`ImagePath`, `Type`, `Group`, `ErrorControl` alanlarını yok etmişiz; geriye
yalnızca `Start` kalmış.

Kapattığımız sürücüler için zararsız — zaten yüklenmiyorlar. Ama
**açtıklarımız için felaket**: `ImagePath`'i olmayan bir sürücüyü Windows
yükleyemez.

İki ölçümün aynı çıkması bunu doğruluyordu ve fark etmemiştim:

| Yapılandırma | Süre |
|---|---|
| Hepsi kapalı | 6 s |
| Sadece `DXGKrnl` kapalı (30 sürücü "açık") | 6 s |

Aynı hasar, aynı sonuç. **Sürücü ekseninde yapılan ölçümlerin hiçbiri
geçerli değil** — 15 saniyelik temel dışında, o da hive'a hiç dokunulmadan
alınmıştı.

`STARTYAZ()` yardımcısı eklendi: `hivexregedit --merge` ile yalnızca `Start`
yazılıyor, diğer değerlere dokunulmuyor. `hivexregedit` artık zorunlu ve
eksikse script çerçeveli bir uyarıyla duruyor — paket adı `libwin-hivex-perl`,
`libhivex-bin` değil.

Bu, aynı sınıftan **dördüncü** hata: araç "yaptım" diyor, sonuç başka çıkıyor.
Öncekiler yamalı `UfsDxe.c`, SYSTEM'deki eski ESP ve atlanan WIM içerik
yamalarıydı.

## `DXGKrnl` masum — sürücü ekseninde ilk geçerli ölçüm

Temiz WIM (ISO'dan yeniden üretildi) + onarılmış araç + sadece `DXGKrnl`
kapalı:

```
toplam 16 s   /   daire öncesi 5 s   /   daire ~11 s
```

Temel değerle aynı. `DXGKrnl` elendi.

Aynı ölçüm hattın onarıldığını da doğruluyor: temel değer yeniden üretildi,
yani `hivexregedit --merge` yolu sağlam ve bundan sonraki ölçümler geçerli.

`hepsi-kapat` eski tarama mantığından kurtarıldı; artık `TEMEL_ACIK`
listesinden korumalı olmayanları çıkarıp aynı `sadece-kapat` yoluna veriyor —
tek kod, tek doğrulama.

## UFS'i Windows altında çalıştırma: PWM Gear 1

Sürücü ekseni kapandıktan sonra depolamaya geçildi. Gerekçe iki katlı:
Windows'un kendi kaydını yazabilmesi için bir yere ihtiyacı var, ve depolama
olmadan telefon zaten kullanılamaz.

### Neden PWM Gear 1

`storufs.sys` jenerik: denetleyiciyi `HCE` ile resetliyor ve UniPro'yu
standart `DME_LINKSTARTUP` ile ayağa kaldırmaya çalışıyor. Exynos'un vendor'a
özel kalibrasyonunu bilmiyor.

Ama UniPro spec'i şunu söylüyor: **`DME_LINKSTARTUP`'tan sonra link her zaman
PWM Gear 1'de olur.** Yüksek hıza geçiş ayrı bir pazarlık ve vendor
kalibrasyonu tam orada gerekiyor.

Şu an UEFI linki HS Gear 3'e çıkarıyor. `storufs` devralınca resetleyip kendi
jenerik pazarlığını yapmaya çalışıyor — kaybettiğimiz yer orası. Link PWM
G1'de bırakılırsa `storufs`'un yapacağı şey zaten spec'in varsayılan durumu
oluyor.

Yavaş olacak — PWM G1 tek şerit ~3 MB/s. Önemli değil; amaç hız değil,
Windows'un diski **görmesi**.

### Dokunulan üç yer

| Yer | Değişiklik | Neden |
|---|---|---|
| `BoardInit.c` | `Ufs->GearMode` 3 → 1 | `PA_TxGear`/`PA_RxGear` ve `CalParam->MaxGear` buradan besleniyor |
| `PA_PWRMode` (0x1571) | Sabit `UFS_RXTX_POWER_MODE` → `pmd->Mode`'dan hesaplanıyor | Upstream değeri derleme anında üretiyordu, PWM'e geçilemiyordu |
| `PA_Rx/TxTermination` | Koşulsuz `1` → HS ise 1, değilse 0 | **Terminasyon HS'e özeldir**; PWM'de 1 yazmak pazarlığı reddettirir |

`Pmd->Mode` FAST(1) → SLOW(2); CAL katmanı `IS_PWR_MODE_PWM` ile buna bakıp
PWM tablolarını seçiyor.

`tools_ufs_pwm.py` dört yamanın hepsi uygulanmazsa **durup hata veriyor** —
upstream değişmişse derleme yanlış kodla geçmesin. Ayrıca `yap.sh` her
derlemede `UfsDxe.c`'yi git'ten geri çağırdığı için yama daima temiz dosyaya
uygulanıyor; önceki UFS yamasında düştüğümüz birikmeli hasar tuzağı yok.

## Siyah ekran = ayakta duran WinPE

Bizim `startnet.cmd` enjekte edilince ne yazı çıktı ne animasyon. Stock sürüm
(tek satır `wpeinit`) ile animasyonlu kapanma geliyordu.

Fark scriptin çalışıp çalışmaması değil, **çıktısının görünmemesi.**

WinPE'de konsol çıktısını ekrana çizen şey `win32k` ve **görüntü sürücüsü**.
Bizde çalışan bir görüntü sürücüsü yok — `bootvid`'in daireyi çizdiği
framebuffer'ı Windows'un konsolu kullanamıyor. Yani `echo` satırları boşluğa
gidiyor, sonra `pause`'a gelip sonsuza kadar bekliyor. Kabuk çıkmadığı için
cihaz kapanmıyor ve animasyon görünmüyor.

**O siyah ekran ayakta duran bir WinPE.** Sadece konuşamıyor.

### Yeni `startnet.cmd`: ekrana değil dosyaya

Ekrana hiç yazmıyor, hiç beklemiyor. `wpeinit` çalıştırıp her sürücü harfine
bir iz dosyası bırakıyor ve çıkıyor.

| Gözlem | Anlamı |
|---|---|
| Çıkışta animasyonlu sönme | Script sonuna kadar çalıştı |
| `WINLOG.TXT` ESP'de bulunur | **`storufs` bağlı, depolama çalışıyor** |

`if exist` kullanılmıyor: olmayan sürücü harfinde bekletebiliyor ve zaten
yazma denemesinin kendisi testin ta kendisi.

## Kısır döngü: iki çıktı kanalı da kapalı

`WINLOG.TXT` hiçbir yere düşmedi, ve `startnet.cmd`'ye **dokunmak** cihazı
anında karartıp sıfırlatıyor — animasyon bile gelmiyor. Stock içerik (tek
satır `wpeinit`) ise düzgün çıkıp animasyonla kapanıyor.

Durum şu:

| Kanal | Neden kapalı |
|---|---|
| Dosyaya yazma | Depolama yok (`storufs` bağlanmıyor) |
| Ekrana yazma | Görüntü sürücüsü yok, konsol çizilemiyor |

Depolamayı test etmek için dosya yazmamız gerekiyor, dosya yazmak için
depolama gerekiyor. Klasik kısır döngü.

### Çözüm: cevabı SÜREYE kodla

Elimizde gözlem olarak iki şey var — animasyonun gelip gelmediği ve **ne
kadar sonra** geldiği. İkincisi bir bit taşıyabilir.

Yeni `startnet.cmd` hiçbir şey yazmıyor. `wpeinit`'ten sonra `C:`–`G:`
arasında bir birim arıyor:

| Bulursa | Bulamazsa |
|---|---|
| ~30 saniye bekleyip çıkıyor | ~5 saniye bekleyip çıkıyor |

Yani **animasyonun ne zaman geldiği doğrudan cevabı veriyor.** Uzun bekleme
görürsek `storufs` bağlanmış demektir.

Script ayrıca minimum tutuldu: `@echo off` yok, `::` yorum yok, parantezli
`for` bloğu yok, yönlendirme yok. Önceki sürüm bunların hepsini içeriyordu ve
anında ölüyordu; hangisinin öldürdüğünü bilmiyoruz, o yüzden hepsini birden
çıkardık. Yalnızca `if exist`, `goto` ve `ping` kaldı — üçü de `wpeinit`
sonrasında güvenli.

## Diskten açılış: sessizliği görünür hataya çevirmek

Fikir kullanıcıdan geldi ve ben önce yanlış eledim:

> "Diske erişip erişemediğini tek tek test ediyoruz. Diske yazsak ve sürücü
> başarısız olsa açılmaz ve çalışmadığını anlarız."

İtiraz doğru — **başarısızlığın kendisi sinyal.** RAM diskten açarken
`storufs`'un bağlanıp bağlanmaması hiçbir şeyi değiştirmiyor; Windows yine
açılıyor, sadece depolama görmüyor ve bunu bize söyleyemiyor. Otuz turdur bu
sessizlikle boğuşuyoruz.

Diskten açarken bağlanmazsa `winload` açılış birimini bulamaz ve **ekrana hata
basar.** Bunun çalıştığını biliyoruz: `ws2ifsl.sys` denemesinde
`0xc0000428 / digital signature couldn't be verified` ekranını gördük.
`winload`'un hata yolu sağlam; eksik olan ona ulaşacak bir hataydı.

### Üç parça

`tools_windisk.sh` — 3 GiB NTFS imajı üretip `boot.wim` index 1'i içine
açıyor, SYSTEM bölümü için sparse tar veriyor.

- NTFS sektör ve küme boyutu **4096**: UFS'in mantıksal blok boyutu bu ve
  `bootmgr` blok G/Ç üzerinden okuyacak.
- `wimlib-imagex apply` doğrudan NTFS imajına yazıyor; klasöre açıp
  kopyalamak güvenlik tanımlayıcılarını ve yeniden ayırma noktalarını
  kaybettirir, Windows açılmaz.
- UEFI'de NTFS sürücüsü olmaması sorun değil — `bootmgr`'ın kendi NTFS
  okuyucusu var.

`bcd_olustur.cmd disk` — `ramdisk=` yerine
`locate=\Windows\System32\winload.efi`. `bootmgr` birimleri kendisi tarayıp
yolu buluyor, GPT bölüm GUID'ini elle yazmak gerekmiyor.

`tools_winboot.sh --disk` — ESP'den `boot.wim` ve `boot.sdi`'yi çıkarıyor;
disk modunda gereksizler ve 600 MiB'lik CACHE'e sığmıyorlar.

### Beklenen okuma

| Sonuç | Anlamı |
|---|---|
| Windows açılıyor | `storufs` bağlı, depolama çalışıyor |
| "Inaccessible boot device" | `storufs` bağlanmıyor — ama **artık görünür** |
| Başka bir `0xc0000...` | `winload` neyi bulamadığını tam olarak söylüyor |

Üçü de sessizlikten iyi.

## Diskten açılış: ilk okuma — 1-2 saniye

`tools_windisk.sh` ile `boot.wim` içeriği 3 GiB'lık bir NTFS imajına açıldı
(24932 dosya, 1381 MiB) ve SYSTEM bölümüne flaşlandı. ESP `--disk` ile CACHE'e
döndü; `boot.wim`/`boot.sdi` çıkarıldığı için artık 600 MiB'a rahat sığıyor.

İlk denemede `startup.nsh` "Acilacak bir sey bulunamadi" dedi: tetikleyici
`sources\boot.wim` idi ve o dosyayı bilerek silmiştik. `EFI\Microsoft\Boot\BCD`
ile değiştirildi — bootmgfw'in tek zorunlu yoldaşı, iki varyantta da var.

İkinci denemede bootmgfw yüklendi, çember döndü, **1-2 saniyede** sıfırladı.
RAM diskle bu süre 16 saniyeydi. Aradaki on dört saniye WinPE'nin gerçekten
çalıştığı kısımdı; artık ondan öncesinde ölüyoruz.

### `UFS OCS=0x07` yeni değil

Ekrandaki üç `UFS OCS=0x07 response=0x01 type=0x21` satırı bu turun bulgusu
değil. Yukarıda iki kez kayıtlı: `bootmgfw` her açılışta LUN 2 LBA 0'a yazmayı
deniyor, Samsung o boot LU'sunu `bLUWriteProtect` ile kilitlemiş, aygıt
reddediyor ve bootmgfw yoluna devam ediyor. Upstream `UfsDxe.c`'nin kendi
uyarısı, bizim enstrümantasyonumuz değil. Gürültü.

### Ayrılamayan iki ihtimal

Çemberi hem `bootmgfw` hem `winload` çiziyor. Dolayısıyla 1-2 saniyelik ölüm
şu ikisinden hangisi, ekrandan anlaşılmıyor:

- (a) `bootmgfw` birimleri tararken ölüyor — NTFS/UFS okumasında
- (b) `winload.efi`'yi bulup çalıştırdı, ölen winload

`menu` varyantı bunu tek açılışta ayırıyor: `displaybootmenu Yes` + `timeout 30`
menüyü **winload çağrılmadan hemen önce** çiziyor ve kullanıcı girdisi bekliyor.

| Ekranda | Sonuç |
|---|---|
| Menü çıkıp bekliyor | bootmgfw sağlam, tarama sorunsuz. Suç winload'da. Enter'a basıp aynı açılışta ikinci yarıyı da ölçüyoruz |
| Menü yok, yine 1-2 s | bootmgfw'in kendisi ölüyor; Windows'un çekirdek tarafına hiç gelmemişiz |

## `menu` varyantı: kırılma noktası tam olarak bulundu

Üç ölçüm, tek açılış:

1. **Windows Boot Manager menüsü çıktı ve bekledi.** Metin çizildi, geri sayım
   işledi, Enter'a cevap verdi. `bootmgfw` sapasağlam: birimleri taradı, NTFS'i
   okudu, BCD'yi ayrıştırdı, `winload.efi`'yi buldu.
2. Enter'dan sonra logo ekranı, **50 saniye çembersiz**. Bu winload'un yükleme
   safhası — PWM Gear 1'de ~150 MB'ı ~3 MB/s ile okumak tam bu kadar sürer.
   Tek bir hata satırı yok.
3. Sonra **2 saniye çember, ardından sıfırlama.**

### Ne öğrendik

| Katman | Durum |
|---|---|
| UEFI UFS sürücüsü | Çalışıyor — 50 saniye kesintisiz okuma |
| `bootmgfw` NTFS okuyucusu | Çalışıyor |
| `locate=` birim taraması | Çalışıyor |
| `winload.efi` | Çalışıyor — çekirdeği ve tüm açılış sürücülerini yükledi |
| **Çekirdek** | ~2 saniyede ölüyor |

RAM diskten açılışta çekirdek sonuna kadar gidiyordu (sönme animasyonu). Tek
fark açılış biriminin UFS olması. Yani geriye **tek bir suçlu** kaldı:
`storufs.sys`.

Kullanıcının fikri tam da bunu yapmak içindi ve yaptı: gözlemlenemez bir soru
("storufs bağlanıyor mu?") ölçülebilir bir imzaya dönüştü.

### 2 saniye neyi söylüyor

StorPort komut zaman aşımı 30-60 saniyedir. 2 saniye onun için çok kısa ama
"aygıt başlatma başarısız → PnP aygıtı reddetti → INACCESSIBLE_BOOT_DEVICE"
için tam yerinde. Yani storufs **ilk adımda** düşüyor: denetleyici init /
NOP OUT. Komut gönderip cevap beklerken değil.

### Elenen: kesme numarası

DTB `ufs@0x13D60000`: `interrupts = <0x00 0xf9 0x00>` → SPI 249. ACPI GSIV =
SPI + 32 = 281 = `0x119`. DSDT'de yazan değer bu. Doğru.

### Elenen: CRLF

Depodaki `windows/startnet.cmd` LF satır sonlu (0 CR). Ama `tools_winboot.sh`
satır 426 enjeksiyondan önce `sed 's/\r*$/\r/'` ile çeviriyor — cihaza giden
dosya CRLF. "startnet'e dokunmak açılışı öldürüyor" bunun yüzünden değil.

### Kayda geçen gerileme: `_CRS` dört aralığa dönmüş

STATUS.md 1238: "`_CRS` tek aralığa indirilince storufs zararsızlaştı
(5-6 s → 12 s)". Ama bilinen iyi noktaya (`43ef06c^`) dönerken bu sadeleştirme
kayboldu; `DSDT_minimal.dsl` şu an yine dört bellek aralığı taşıyor. Şimdilik
dokunmuyorum — tek değişkenli ölçüm bozulmasın — ama sıradaki aday bu.

### Bu turun değişkeni: `_CCA` 1 → 0

Exynos'ta koherans iki parçalı: SYSREG biti yolun bir ucunu açıyor, diğer uç
sürücünün UTRD tanımlayıcılarındaki paylaşım alanlarını doğru işaretlemesi.
Samsung'un sürücüsü ikisini de yapıyor, jenerik `storufs.sys` ikincisini
bilmiyor. `_CCA=1` Windows'a "önbellek bakımı yapma" diyor; koherans gerçekte
tam değilse tanımlayıcılar CPU önbelleğinde kalır, denetleyici bayat veri okur
ve init ilk adımda düşer — gördüğümüz imzanın tam kendisi.

`_CCA=0` ile Windows DMA tamponlarını önbelleklenmemiş ayırır ve temizlemeyi
kendi yapar. Koherans zaten çalışıyorsa sadece yavaşlarız.

| Ölçüm | Sonuç |
|---|---|
| 2 saniyeden uzun / açılıyor | Sorun tanımlayıcı görünürlüğüymüş |
| Yine 2 saniye | DSDT ekseni tükendi; sıra `_CRS` tek aralıkta, sonra RAM diske dönüp WinPE'yi ayakta tutmakta |

## `_CCA=0` sonuç vermedi — ve storufs: kapanan kapı

Yine 1-2 saniye. Tanımlayıcı görünürlüğü değilmiş.

Bu noktada DSDT ekseninin tükendiğini kabul etmek gerekiyor, ve sebebi bir
ölçüm eksikliği değil — **yapısal**:

`storufs.sys` jenerik bir UFSHCI miniport'u. Başlarken denetleyiciyi `HCE=1`
ile resetliyor ve ardından standart `DME_LINKSTARTUP` gönderiyor. Exynos'ta
HCE reseti UniPro'nun bütün yerel niteliklerini ve vendor blok yazmaçlarını
siliyor; Linux'un `ufs-exynos.c`'si bu yüzden **her HCE'den sonra**
`exynos_ufs_pre_link()` → `ufs_cal_pre_link()` çağırıp 26 tablo dolusu PMA ve
UniPro kalibrasyonunu yeniden yazıyor. Bizim UEFI sürücümüz de aynısını
yapıyor — çalışmasının sebebi bu.

`storufs.sys` o kalibrasyonu bilmiyor ve bilemez. Ve **hiçbir ACPI nesnesi
jenerik bir sürücüye Samsung PHY kalibrasyonu yaptıramaz.** `_HID`, `_CLS`,
`_CRS`, `_CCA`, `_DSM` — hiçbiri kod çalıştırmıyor, hepsi sadece tanımlıyor.

Bu kapı, Exynos için bir Windows UFS sürücüsü yazılana kadar kapalı. Mu-Silicium
durum tablosunda Exynos ailesinin tamamında `Internal Storage` işaretsiz
olmasının sebebi de bu.

### Eksen değişikliği: RAM diske dönüş

Diskten açma denemesi tam olarak yapması gerekeni yaptı — sorumluyu isimlendirdi
ve bir kapıyı kesin biçimde kapattı. Kaybedilen bir şey yok.

Bilinen durum:

| Yol | Sonuç |
|---|---|
| Disk (UFS/NTFS) | winload sorunsuz, çekirdek 2 s'de ölüyor — storufs |
| **RAM disk** | **Çekirdek sonuna kadar gidiyor** (sönme animasyonu, commit 43ef06c) |

RAM disk yolunda Windows gerçekten açılıyor. Depolama yok, ama açılıyor. Sıra
bunu **görünür ve kalıcı** kılmakta.

### Yeni `startnet.cmd`: WinPE'yi ayakta tut

```
wpeinit
ping -n 999 127.0.0.1 >nul
pause
```

Üç satır, **etiket ve `goto` yok**. Daha önce startnet'i her değiştirişimizde
açılış ölüyordu; o sürümlerin hepsinde `goto`/etiket vardı. `ping -n 999`
yaklaşık 16 dakika bekliyor, ağ yığınına bağımlı değil (loopback), stdin'e de
bağımlı değil. `pause` sadece emniyet kemeri.

Okuma çok basit:

| Ekranda | Sonuç |
|---|---|
| Cihaz açık kalıyor, sıfırlamıyor | **WinPE çalışıyor.** Sönme animasyonu bir çöküş değil, betiğin bitmesiydi |
| Yine sıfırlıyor | Sönme animasyonu yorumumuz yanlıştı, WinPE sonuna kadar gitmiyor |

Bu ölçüm otuz turdur cevaplayamadığımız soruyu kapatıyor: Windows bu telefonda
çalışıyor mu, çalışmıyor mu.

## Bir tur kaybı: `--win` unutuldu

Talimat `bash cache_esp.sh --system` şeklinde verildi, `--win` yazılmadı.
Script görevini kusursuz yaptı: SYSTEM bölümüne bir **Linux ESP'si** yazdı.
Cihaz `startup.nsh`'in Linux dalını çalıştırıp `vmlinuz-lts`'i açtı; Windows
dosyaları imaja hiç girmemişti.

Ekranda `MOD: Linux` yazıyordu — ama tek satır, elli satırlık çıktının içinde.
Sessiz doğru davranış, gürültülü yanlış davranıştan tehlikeli olabiliyor.

`cache_esp.sh` artık `--system` + `--win` yokken duruyor ve iki doğru komutu
yazıyor. Kombinasyonun var olma sebebi 742 MB'lık `boot.wim`'in 600 MiB'lık
CACHE'e sığmaması; 68 MB'lık Linux dosyaları için büyük bölüme geçmenin
anlamı yok, yani bu kombinasyon istenerek yazılmış olamaz.

## `startnet.cmd` suçlusu bulundu: `ping`, `goto` değil

`menuram` + üç satırlık `startnet.cmd` (etiket ve `goto` yok) yine öldürdü:
daire dönerken kesiliyor, sönme animasyonu yok.

Üç sürüm yan yana:

| İçerik | Sonuç |
|---|---|
| `wpeinit` (stok) | Sonuna kadar gidiyor, **sönme animasyonu** |
| `wpeinit` + `goto`/etiket + **`ping`** | Ölüyor, animasyon yok |
| `wpeinit` + **`ping`** + `pause`, etiket yok | Ölüyor, animasyon yok |

`goto` iki başarısızlığın yalnızca birinde var; **`ping` ikisinde de var.**
Değişken izolasyonu suçluyu isimlendiriyor.

Sebep makul: `ping` TCP/IP yığınını gerektiriyor. Bu cihazda hiç ağ donanımı
yok ve `wpeinit` yine de yığını ayağa kaldırmaya çalışıyor. Sert sıfırlama +
animasyon yokluğu temiz çıkış değil, çöküş demek.

Daha önceki iki tur bu yüzden boşa gitti ve yanlış sonuç çıkarmıştık:
"startnet.cmd'ye dokunmak açılışı öldürüyor" doğru değilmiş — **`ping`
çağırmak öldürüyormuş.**

### Ayrıca elenen: CRLF ve enjeksiyon mekanizması

`--stok` da aynı `wimlib-imagex update --command=add` yolundan geçiyor ve o
sürüm çalışıyor. Yani WIM'e dosya enjekte etmek sorunsuz; mesele yalnızca
dosyanın içeriği. Satır sonları da zaten CRLF'e çevriliyor (tools_winboot.sh
satır 426).

### Yeni sürüm: ağa hiç dokunmuyor

```
wpeinit
pause
for /l %%i in (1,1,2000000000) do @rem
```

`pause` ve `for /l` **cmd.exe yerleşikleri** — harici çalıştırılabilir yok,
ağ yok, dosya sistemi yok. `pause` konsol girdisi bekliyor; konsol
çizilmiyorsa da sonsuza kadar bloke kalıyor. Dönerse `for` döngüsü devralıyor.

| Ekranda | Sonuç |
|---|---|
| Cihaz açık kalıyor | **WinPE çalışıyor.** Otuz turluk soru kapanıyor |
| Sönme animasyonu | WinPE çalışıyor ama ikisi de anında dönüyor — yine de çalıştığı kanıtlanmış olur |
| Sert sıfırlama | Suçlu `ping` değilmiş, `wpeinit` sonrası herhangi bir komut öldürüyor |

## Metodoloji hatası: doğrulanmamış temel çizgiden bisection

Üçüncü `startnet.cmd` sürümü de öldürdü — bu sefer **anında sert sıfırlama**,
sönme yok. Yani `ping` de suçlu değilmiş.

Burada asıl hatayı görmek gerekiyor. "Sönme animasyonu" gözlemiyle bugün
arasında değişen şeyler:

1. `startnet.cmd` içeriği
2. BCD varyantı → `menuram` (`displaybootmenu`, `timeout 30`, `numproc 1`)
3. ESP'nin CACHE'ten SYSTEM'e taşınması
4. `boot.wim`'in yeniden üretilmesi (23H2 → 25H2 dahil)
5. DSDT'de `_CCA` 1 → 0

Ben hepsini `startnet.cmd`'ye yazdım ve iki tur onun içinde değişken izole
etmeye çalıştım. **Temel çizgi doğrulanmadan bisection yapılmaz** — aradığın
sinyal başka bir eksende olabilir ve her ölçüm seni yanlış yöne götürür.
Nitekim iki tur boyunca önce `goto`'yu, sonra `ping`'i suçladım; ikisi de
değildi.

### Önce temel çizgiyi geri kur

`--stok` ile `startnet.cmd` tek satıra (`wpeinit`) dönüyor. Başka **hiçbir
şey** değişmiyor.

| Sonuç | Anlamı |
|---|---|
| Sönme animasyonu geri geliyor | `startnet.cmd` içeriği gerçekten değişken. Bisection ancak şimdi anlamlı: sıradaki test `wpeinit` + `rem` (iki satır, komut yok) |
| Yine anında sert sıfırlama | `startnet.cmd` hiç suçlu değilmiş. Gerileme yukarıdaki 2-5'ten birinde; sırayla geri alınacak |

Ayrıca ölçülecek: Enter'dan sıfırlamaya kadar geçen süre. RAM diskten açılışta
`boot.wim`'i belleğe almak ve winload'un yüklemesi zaman alıyor; "anında"
sıfırlama winload'un hiç çalışmadığını, uzun süre ise çekirdeğe kadar
gidildiğini gösterir. İki durum tamamen farklı yerlere bakmayı gerektiriyor.

### Temel çizgi de kırık: `startnet.cmd` hiç suçlu değilmiş

Stok `startnet.cmd` (tek satır `wpeinit`) ile de sönme yok — 10 saniye tekerlek,
sonra doğrudan kesilme. İki turluk `startnet.cmd` bisection'ı tamamen boşa
gitti, ama artık bunu **kesin** biliyoruz. Gerileme başka bir eksende.

### En güçlü aday: WIM'in hive'ında biriken sürücü deneyleri

`tools_winpe_surucu.sh` ile 33 açılış sürücüsünü kapatmıştık. Son `liste`
çıktısı `acik: 24 kapali: 33` diyordu ve **`hepsi-ac` çalıştırdığımıza dair
kayıt yok.** `esp/sources/boot.wim` o günden beri yeniden üretilmediyse o 33
sürücü hâlâ `Start=4`.

Kapalı açılış sürücüleriyle WinPE'nin sakat açılması beklenen davranış — ve
"10 saniye tekerlek, sonra sert kesilme" tam da o profil.

Bu, `startnet.cmd`'yi suçlarken gözden kaçırdığımız şey: WIM her turda
değişiyordu ama biz sadece tek bir dosyaya bakıyorduk.

### Tam geri dönüş

Tek tek geri almak yerine hepsini birden temel çizgiye çekiyoruz. Geri alma
bisection değil, restorasyon — birden fazla değişkeni aynı anda geri almak
burada doğru.

| Eksen | Geri dönüş |
|---|---|
| `boot.wim` | `--wim-yenile` ile ISO'dan **sıfırdan** üretiliyor, bütün hive düzenlemeleri siliniyor |
| `startnet.cmd` | `--stok`, tek satır |
| DSDT `_CCA` | `Zero` → `One` (işe yaramamıştı, taşımanın anlamı yok) |
| BCD | `menuram` → `tekcpu` (menü yok, `numproc 1` var) |

| Sonuç | Anlamı |
|---|---|
| Sönme animasyonu geri geliyor | Temel çizgi sağlam. Değişiklikler tek tek geri eklenir; ilk şüpheli hive |
| Yine sert kesilme | Gerileme UEFI derlemesinde ya da ISO sürümünde; `yap.sh` ve ISO ekseni açılır |

## Gerileme bulundu: `B2LEl2Dxe` her açılışta TrustZone'a SMC atıyordu

Tam geri dönüş (WIM sıfırdan, `--stok`, `_CCA=One`, BCD `tekcpu`) sönmeyi geri
getirmedi. Yani gerileme WIM'de, `startnet`'te, `_CCA`'da veya BCD'de değil —
**UEFI derlemesinde.**

Ve yeni bir belirti var: cihaz bu sefer **sıfırlamadı, dondu.** Komple
kapatmadan yeniden başlatılamadı.

`DXE.inc` incelenince görüldü ki `B2LEl2Dxe` hâlâ derlemede ve her açılışta
çalışıyor. Yaptığı iş: Samsung'un TrustZone arka kapısına `smc #0` /
`x0=0xC2000400` gönderip **"şu kodu EL2'de çalıştır"** demek.

Kendi `B2L2` imzasını hiçbir zaman üretmediği için "çalışmıyor, zararı yok"
diye bırakmıştık. **Bu varsayım yanlıştı.** TrustZone'a EL2'de kod
çalıştırmasını söylemek asla no-op değil:

- Yük kabul edildiyse EL2'de **yarım çalışan** bir hipervizör oturuyor
- `HCR_EL2.TSC` açık demek, EL1'den gelen **her SMC** bizim vektörümüze
  düşüyor demek. Windows PSCI için SMC yapıyor.
- Vektör beklenmedik bir durumda dönmezse sistem oracıkta kilitlenir

Donma belirtisi tam olarak bunun imzası. Ve sönme animasyonunu gördüğümüzden
beri UEFI'ye eklenen tek bileşen buydu.

### Ders

"Çıktı üretmedi" ile "hiçbir şey yapmadı" aynı şey değil. Bir SMC'nin sessiz
kalması, TrustZone'un isteği reddettiği anlamına gelmiyor — kabul edip yarım
bıraktığı anlamına da gelebilir. Sonuç vermeyen bir deneyi derlemede
bırakmak, sonraki her ölçümü kirletiyor.

`B2LEl2Dxe` derlemeden çıkarıldı; kaynağı duruyor. Bellek haritasındaki
`B2L EL2` bölgesi yerinde bırakıldı — kod yüklenmediği için çalışmıyor ve o
ayrılmış RAM, Windows sürücüsünün ölüm sonrası halka tamponu için zaten
gerekecek.

## B2LFB ilk deneme: ne macenta ne imza — iki tasarım hatam

Sürücü derlendi, imzalandı, WIM'e girdi. Cihazda hiçbir şey görünmedi.

### Hata 1: `ErrorControl=0` en bilgilendirici arızayı sessizliğe çevirdi

Servisi "sürücü patlarsa açılışı bozmasın" diye `ErrorControl=0` (IGNORE) ile
kaydetmiştim. Bunun anlamı: **imza reddedilirse `winload` sürücüyü sessizce
atlar.** Yani elimizdeki en değerli hata yolunu — `ws2ifsl` denemesinde
gördüğümüz `0xc0000428 STATUS_INVALID_IMAGE_HASH` ekranını — kendi elimle
kapatmışım.

Projede bunun üçüncü tekrarı: `bootstatuspolicy IgnoreAllFailures`,
`AutoReboot=0`, şimdi `ErrorControl=0`. Her seferinde "güvenli tarafta duralım"
diye seçilen ayar, ölçümü kör etti.

`ErrorControl=3` (CRITICAL) ile tersine dönüyor: sürücü yüklenemezse winload
duruyor ve **sebebini ekrana yazıyor.** Açılış olmuyor, ama bu turda açılış
hedef değil — hedef sürücünün yüklenip yüklenmediğini öğrenmek.

### Hata 2: framebuffer adresi tahmin edildi

`0xCA000000` "Display Reserved" bölgesinin başı, ama GOP'un gerçek tabanı o
20 MB'lık alanın içinde başka bir yerde olabilir. İlk sürüm yalnızca bölgenin
başına ~120 satırlık bir bant yazıyordu; taban başka yerdeyse hiçbir şey
görünmez.

Tahmin tamamen kaldırıldı: **bölgenin tamamı** (`0x01400000`, 20 MB)
dolduruluyor. Framebuffer içinde nerede olursa olsun ekran macenta oluyor.

### Artık dört okuma birbirinden ayrılıyor

| Ekranda | Sonuç |
|---|---|
| Ekran macenta | Sürücü yüklendi ve çalıştı |
| `0xc0000428` | İmza reddedildi — çözülebilir, sebebi ekranda |
| Başka hata kodu | Sürücü yüklenemedi, kod sebebi söylüyor |
| Hiçbir şey | winload açılış sürücülerine hiç gelmiyor; arıza daha geride |

Önceki sürümde dördü de aynı görüntüyü veriyordu. Asıl kazanç bu.

## İLK KEZ: Windows kendi çekirdek açılışından bize hata bildirdi

```
File:   \windows\SystemRootSystem32driversb2lfb.sys
Status: 0xc000000f
Info:   The operating system couldn't be loaded because a critical
        system driver is missing or contains errors.
```

Bu ekran otuz turdur ulaşamadığımız yer. Dört şeyi birden kanıtlıyor:

1. **`winload` açılış sürücülerini yüklemeye geliyor.** Sessiz sıfırlamalar
   bunun öncesinde değil, sonrasında oluyormuş.
2. **Servis kaydımız okunuyor** — `Services\b2lfb` düğümü geçerli.
3. **Hata yolu çalışıyor** ve dosya adımızı yazıyor.
4. **`ErrorControl=3` doğru karardı.** `0` iken tam bu arıza sessizce
   atlanıyordu.

### Hata: `.reg` kaçış karakterleri

Dosya adına dikkat: `SystemRootSystem32driversb2lfb.sys`. Ters bölükler
yenmiş. `.reg` biçiminde ters bölük **kaçış karakteri** ve `\\` yazılması
gerekiyor. Biz tek yazdık, hive'a çizgisiz girdi, `winload` da olmayan bir
dosyayı aradı — `0xc000000f` zaten "dosya bulunamadı" demek.

**İmzayla ilgisi yok.** İmza testine daha sıra bile gelmemiş; sürücü hiç
okunmadı.

### Alınan önlem

`tools_b2lfb.sh` artık `ImagePath`'i `hivexget` ile **geri okuyor** ve ters
bölük sayısı üçten azsa oracıkta duruyor. Yazıp geri okumamak, yazdığını
varsaymaktır — bu proje o dersi daha önce `hivexsh setval` ile de almıştı.

### Sıradaki bilinmeyen

Yol düzelince sürücü gerçekten okunacak ve **imza kontrolü ilk kez
çalışacak**. İki sonuçtan biri:

| Ekranda | Sonuç |
|---|---|
| Ekran macenta | Sürücü yüklendi ve çalıştı. Canlı çıkış kanalı AÇIK |
| `0xc0000428` | İmza reddedildi. Sertifika WinPE güven deposuna eklenecek |

# ★ EKRAN MACENTA — Windows çekirdeğinin içinden ilk çıktı

`b2lfb.sys` yüklendi, çalıştı ve framebuffer'ı boyadı. Tekerlek macentanın
**üstünde** dönüyor.

Tek fotoğraf beş şeyi birden kanıtlıyor:

1. **Test imzalı sürücümüz kabul edildi.** `0xc0000428` yok — kendi
   sertifikamız + `testsigning` yeterliymiş. İmza engeli aşıldı ve WinPE'nin
   güven deposuna sertifika eklemek gerekmedi.
2. Sürücü **açılış sürücüsü** olarak (`Start=0`) yükleniyor.
3. Framebuffer `0xCA000000` doğru adres.
4. **Windows çekirdeğinden framebuffer'a yazabiliyoruz.**
5. Tekerleğin üstte dönmesi Windows'un da aynı tampona çizdiğini gösteriyor.

Piksel biçimi de belli oldu: `0x00FF00FF` yazdık, macenta çıktı → **BGRA**.

Otuz turluk sessizlik bitti. Faz 1 tamam.

## Faz 2: bugcheck mi, değil mi?

Kanal artık var; ilk işi otuz turdur cevaplayamadığımız soruya harcıyoruz.

10 saniyelik ölüm bir **bugcheck** mi, yoksa SoC'yi dışarıdan sıfırlayan
başka bir şey mi? İkisi ekranda aynı görünüyor çünkü Windows'un kendi mavi
ekranı bu platformda çizilmiyor. Ama çekirdek bugcheck sırasında **kayıtlı
geri çağrıları çağırıyor** ve biz o an ekrana yazabiliyoruz.

`KeRegisterBugCheckCallback` ile kaydoluyoruz; geri çağrı ekranı yeşile
boyuyor ve `0xCF030000+0x0C`'ye `BUGC` yazıyor.

| Ekranda | Sonuç |
|---|---|
| **Yeşile dönüyor** | Bugcheck. Windows çöküyor ve bunu biliyor. Suç Windows'ta, stop kodunu okumaya çalışırız |
| **Macenta kalıyor** | Bugcheck **değil**. Bir şey SoC'yi dışarıdan sıfırlıyor, çekirdeğin haberi bile yok. Suç platformda — PMU, watchdog, TrustZone |

İkisi tamamen farklı yerlere baktırıyor ve bu ayrımı ilk kez yapabiliyoruz.

### Tasarım notu

Haritalanmış adresler global tutuluyor. Sebebi kritik: bugcheck anında IRQL
çok yüksek ve `MmMapIoSpace` dahil çekirdek API'lerinin neredeyse tamamı
yasak. `DriverEntry`'de bir kez haritalayıp işaretçileri saklıyoruz; geri
çağrıda yalnızca hafızaya yazıyoruz.

## Yeşil gelmedi — ama önce aracı doğrulamak gerekiyor

Bugcheck geri çağrısı ateşlemedi. İki okuması var:

- (a) Çökme gerçekten bir bugcheck değil
- (b) Kayıt hiç tutmadı, dolayısıyla sessizliğin bir anlamı yok

Bu projede aynı ders defalarca alındı. `B2LEl2Dxe` "çıktı üretmedi" diye
zararsız sanıldı ve turlarca ölçüm kirletti. `ErrorControl=0` en bilgilendirici
hatayı sessizliğe çevirdi. **Bir aracın sessizliğine güvenmeden önce aracın
çalıştığı doğrulanmalı.**

`KeRegisterBugCheckCallback` başarılıysa TRUE dönüyor. Dönüş ekrana
yansıtılıyor: kayıt tuttuysa bölgenin ilk sekizde biri beyaza boyanıyor.

| Ekranda | Sonuç |
|---|---|
| Macenta + üstte **beyaz şerit** | Kayıt tuttu. Yeşil gelmiyorsa çökme **gerçekten bugcheck değil** |
| Şerit yok | Kayıt tutmadı; önceki turun sessizliği hiçbir şey kanıtlamıyor |

### Eğer bugcheck değilse ne demek

Windows'un haberi olmadan SoC sıfırlanıyor demek. Ama burada dikkat edilecek
bir ayrıntı var: `EbsTest` `ExitBootServices`'ten sonra **63 saniye** hiçbir
şey yapmadan hayatta kaldı. Serbest çalışan bir watchdog olsa orada da
ölürdü. Yani "watchdog" ilk akla gelen ama ölçüyle çelişen açıklama.

Geriye daha ilginç bir ihtimal kalıyor: **çökme değil, kasıtlı yeniden
başlatma.** WinPE'nin tasarımı gereği `startnet.cmd` bitince kabuk çıkıyor ve
makine kendini yeniden başlatıyor. `--stok` ile `startnet.cmd` tek satır
(`wpeinit`) — yani WinPE ~10 saniyede açılıp işini bitirip **düzgün biçimde**
yeniden başlıyor olabilir.

Bu doğruysa ortada arıza yok ve proje sandığımızdan çok daha ileride.
Beyaz şerit sorusu bunun ön koşulu: önce aracın çalıştığını bilelim.

## Geliştirme döngüsü hızlandırıldı

Tur başına süre çok uzamıştı. En büyük kayıp beklenmedik bir yerdeydi.

### 1. PWM Gear 1 kaldırıldı (asıl kazanç)

UFS linkini PWM Gear 1'e biz düşürmüştük — `storufs.sys`'in jenerik
linkup'ı çalışsın diye. **O kapı yapısal olarak kapandı** ve artık RAM
diskten açılıyoruz; storufs'a hiç ihtiyacımız yok.

Bedeli çok ağırdı: PWM G1 tek şerit ~3 MB/s. Her açılışta `bootmgfw` 742
MB'lık `boot.wim`'i o linkten RAM diske okuyordu. Karşılığı olmayan
dakikalar.

`BoardInit.c` → `GearMode = 3`, `yap.sh` → PWM yaması artık varsayılan
kapalı (`B2L_UFS_PWM=1 bash yap.sh` ile geri açılıyor).

### 2. ESP imajı içeriğe göre boyutlanıyor

`--system` her turda sabit 2 GiB'lık bir dosya üretip biçimlendiriyor ve
`img2simg` ile baştan sona tarıyordu; oysa içerik ~800 MB. Sparse imaj
sayesinde **aktarılan** veri zaten küçüktü, ama **yerel işlem** tam boyut
üzerindendi — döngünün görünmez maliyeti.

Artık içerik + %30 pay, 512 MiB alt sınırla. Alt sınırın sebebi FatPkg:
FAT32 için en az 65525 küme istiyor, 4 KiB kümeyle 256 MiB eder.

### 3. Sırayı kısaltmak

Yalnızca sürücü değiştiyse `tools_winboot.sh` çalıştırmaya gerek yok:

```bash
bash tools_b2lfb.sh && bash cache_esp.sh --win --system
```

### Sonraki adım olarak duruyor

`tools_winpe_adk.sh` ile ~250 MB'lık çıplak WinPE üretmek. O boyutta ESP
tekrar CACHE'e (600 MiB) sığar — hem flaş küçülür hem açılış hızlanır.
Windows tarafında ADK gerekiyor.

### Uyarı: süre karşılaştırması bu noktada kırılıyor

UFS hızı değiştiği için "10 saniye" gibi süreler bu turdan öncesiyle
karşılaştırılamaz. Beyaz şerit ölçümü etkilenmiyor — o bir dönüş değeri,
zamanlamaya bağlı değil.

# ★ Kesin sonuç: çökme bir BUGCHECK DEĞİL

Beyaz şerit çıktı → `KeRegisterBugCheckCallback` **tuttu**. Yeşil gelmedi →
sessizlik bu kez gerçek bir ölçüm:

**Windows kernel'i çökmüyor. Ölürken bunun farkında bile değil.**

Bu, otuz turdur açık olan en büyük soruyu kapatıyor ve arama alanını ikiye
bölüyor. Artık `numproc`, MADT, PSCI, sürücü listesi gibi "Windows çöküyor,
neden" eksenlerinin hiçbirinde iş yok.

## Geriye iki açıklama kalıyor

**Watchdog / dış sıfırlama** — ama ölçüyle çelişiyor: `EbsTest`
`ExitBootServices`'ten sonra **63 saniye** hiçbir şey kurmadan yaşadı.
Serbest çalışan bir watchdog olsa orada da ölürdü.

**Ortada arıza yok** — WinPE açılıyor, `wpeinit` çalışıyor, `startnet.cmd`
bitiyor, kabuk kapanıyor ve WinPE **tasarımı gereği** kendini yeniden
başlatıyor. Temiz kapanış hiçbir bugcheck üretmez. Süre de tutuyor.

İkincisi çok daha güçlü. Doğruysa **Windows bu telefonda çalışıyor** ve
aylardır aradığımız hata hiç var olmadı.

## Bu turun iki ölçümü

### 1. Ekranı bırakıyoruz — sönme animasyonu geri gelsin

Macenta ekran işini gördü ama Windows'un kendi görsellerini örttü, özellikle
**sönme animasyonunu**. O animasyon bu projede zaten bir ölçüydü: "kabuk
düzgün çıktı, çökme yok" (commit `43ef06c`). Şimdi tam da onu sınamamız
gerekiyor.

Artık yalnızca ince bir şerit boyanıyor (`FB_BYTES/32`), gerisi Windows'a
kalıyor.

### 2. Kalp atışı — çekirdek kaç saniye yaşadı

Saniyede bir kare çizen bir `KTIMER` + `KDPC`. Kareler sayılabiliyor ve
renk değiştirdiği için tek tek ayırt ediliyor. Sayaç `0xCF030000+0x10`'a da
yazılıyor.

Asıl söylediği şey süre değil: **kareler son ana kadar artıyorsa** çekirdek
ölene dek zamanlayıcı işletiyordu, yani sağlıklıydı — çöken bir sistem
zamanlayıcı işletmez.

| Ekranda | Sonuç |
|---|---|
| Kareler artıyor + **sönme animasyonu** | Çökme yok. WinPE işini bitirip düzgünce yeniden başlıyor |
| Kareler artıyor, animasyon yok, sert kesilme | Çekirdek sağlıklı ama dışarıdan sıfırlanıyor |
| Kareler bir yerde duruyor | Çekirdek orada ölüyor — kaç kare saydıysak o kadar saniye yaşadı |

## Kalp atışı: 9 çizgi, hepsi düzenli

Çekirdek dokuz saniye yaşadı ve **zamanlayıcı son saniyeye kadar işledi.**
Hiçbir yerde takılma, yavaşlama, duraklama yok.

Çöken bir sistem DPC işletmez. Yani çekirdek öldüğü ana kadar sağlıklıydı.

Elimizdeki tablo:

| Gözlem | Ne eliyor |
|---|---|
| Bugcheck yok (doğrulanmış geri çağrı) | Çökme, panik, sürücü hatası |
| Kalp atışı son ana kadar düzenli | Takılma, kilitlenme, kademeli bozulma |
| Tam 9 saniye | Rastgelelik yok — bir iş bitiyor |
| Bir anda yok oluş | Kapanma süreci değil, tek adımda gidiş |

Bunların hepsi tek bir şeye işaret ediyor: **kasıtlı ve temiz yeniden
başlatma.** WinPE `startnet.cmd` bitince kabuğu kapatır ve makineyi yeniden
başlatır — tasarım böyle.

## Ölçüm: kabuk çıkmazsa yeniden başlatma da olmamalı

Artık bunu gözle animasyon arayarak değil **sayarak** sınayabiliriz. Kalp
atışı nesnel bir sayaç; kimsenin "sönme oldu mu olmadı mı" diye bakmasına
gerek yok.

Yeni `startnet.cmd` iki satır:

```
pause
for /l %%i in (1,1,2000000000) do @rem
```

`wpeinit` bilerek çıkarıldı. Bu turda tek soru "kabuk çıkmazsa ne olur";
aygıt sayımı ve ağ başlatma o soruya hiçbir şey katmıyor, sadece değişken
ekliyor. `pause` ve `for /l` cmd.exe yerleşikleri — harici çalıştırılabilir
yok, ağ yok, dosya sistemi yok. `pause` beklenmedik biçimde dönerse `for`
döngüsü yakalıyor.

| Ölçüm | Sonuç |
|---|---|
| **Çizgiler 9'u geçiyor ve devam ediyor** | Ortada arıza yok. **Windows bu telefonda çalışıyor.** 9 saniyelik "çökme" WinPE'nin işini bitirmesiymiş |
| Yine 9'da duruyor | Sıfırlama kabuktan bağımsız, dışarıdan geliyor. O zaman PMU/watchdog eksenine geçilir |

Bu, projenin başından beri sorduğumuz sorunun cevabı.

## Ölüm sonrası kanal doğrulandı — ama PMU ekrandan kaydı

`dmem 0xCF030000` dökümünde `B2LF` görünüyor. İki şey birden kanıtlandı:

1. Sürücü ayrılmış RAM'e yazıyor
2. **Kayıt sıcak sıfırlamayı aşıyor** — Windows'un içinden yazılan veriyi
   sıfırlamadan sonra UEFI'de okuyabiliyoruz

`BUGC` hiçbir yerde yok: bugcheck olmadığı bağımsız olarak ikinci kez
doğrulandı.

### Ama aradığımız değer ekranda değildi

`RST_STAT` `startup.nsh`'in en başında basılıyordu; altındaki `ls`, `map` ve
0x60 baytlık `dmem` çıktısı 25 satırlık ekranı aşıp onu yukarı kaydırmıştı.
Ölçüm aleti doğruydu, çıktısı görünmüyordu.

Düzeltme:

- `PMU RST_STAT` **en sona** alındı
- `ls fs0:\` ve `map fs0` kaldırıldı — ikisi de görevini çoktan yaptı
  (`EBSTEST.EFI` imajda mı, `fs0` hangi LUN). Her gereksiz satır, görmek
  istediğimiz satırı aşağı itiyor.
- RAM dökümü `0x60` → `0x20`

### Kayıt artık ASCII

Önceki sürümde sayaçlar ikili yazılıyordu ve bulanık bir fotoğraftan hex
çözmek zorunda kalıyorduk; hangi baytın hangi alan olduğu tartışma konusu
oldu. `dmem`'in sağ sütunundaki ASCII görünümü bedava.

Artık doğrudan şu okunuyor:

```
B2LF........TIK=0010 BUG=H
```

| Alan | Anlamı |
|---|---|
| `B2LF` | Sürücü çalıştı |
| `TIK=nnnn` | Çekirdek kaç saniye yaşadı |
| `BUG=H` | Bugcheck yok. `E` olursa var |

Hex çözme, bayt sayma, tahmin yok.

## `TIK=` gelmedi — sürücü değil, script hatası

Kullanıcı sordu: "scriptte hata yapmış olabilir misin?" Evet.

`b2lfb.c` doğruydu; `HeartbeatDpc` ASCII kaydı düzgün yazıyor ve `gLog`
zamanında atanıyor. Sorun `tools_b2lfb.sh`'te: `wimlib-imagex update` ile
`add`, hedefte **aynı isimde bir dosya varken onu değiştirmeyebiliyor.**
Çıktı `/dev/null`'a gidiyordu, dolayısıyla hiçbir uyarı görünmedi ve her şey
başarılı sanıldı. Cihazda çalışan hâlâ **eski sürücüydü** — bu yüzden yalnızca
`B2LF` vardı, `TIK=` yoktu.

### Aynı sınıftan üçüncü hata

1. `hivexsh setval` düğümdeki diğer değerleri siliyordu → beş ölçüm geçersiz
2. `.reg` içindeki ters bölükler kaçış olarak yutuldu → `0xc000000f`
3. Şimdi bu

Üçünde de aynı ders: **yazdığını geri oku.** Bir yazma işleminin hata
vermemesi, yazdığın şeyin orada olduğu anlamına gelmiyor.

### Düzeltme

Hem sürücü hem hive için: **önce sil, sonra ekle, sonra geri oku.**

- `b2lfb.sys` WIM'den çıkarılıp SHA1'i derlenenle karşılaştırılıyor; farklıysa
  script duruyor ("flaşlamak boşuna; cihaza eski kod gider")
- `SYSTEM` hive'ı çıkarılıp `hivexget` ile `ImagePath` okunuyor; yoksa duruyor

Artık yanlış sürücüyle bir Odin turu harcanamıyor.
