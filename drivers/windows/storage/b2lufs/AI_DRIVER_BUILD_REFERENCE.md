# B2LUFS sürücü ve derleme zinciri teknik referansı

Bu belge, `beyond2lte` (Samsung Exynos 9820) için geliştirilen B2LUFS Windows
ARM64 StorPort miniportunun kaynak dosyalarını ve onu üretmek, imzalamak,
WinPE'ye yerleştirmek ve Odin paketine dönüştürmek için kullanılan araçları
açıklar. Başka bir dil modeline veya sürücü geliştiricisine doğrudan bağlam
olarak verilebilmesi için hazırlanmıştır.

Belgenin kapsamı yalnız mimari, dosya sorumlulukları, veri/çağrı akışı,
derleme seçenekleri ve paketleme araçlarıdır.

## 1. Kısa mimari özeti

B2LUFS:

- `ACPI\BYND9820` compatible kimliğine bağlanan fiziksel bir StorPort
  miniportudur.
- Windows'un inbox `storufs.sys` sürücüsünün filtresi değildir.
- UEFI'nin önceden başlatıp çalışır bıraktığı Exynos UFSHCI/UniPro bağlantısını
  devralır.
- Yalnız kullanıcı verisini taşıyan UFS LUN 0'ı Windows'a sunar.
- Boot ve RPMB gibi diğer UFS LUN'larını Windows disk alanının dışında tutar.
- Klasik `SCSI_REQUEST_BLOCK` ve BTL8 adresleme biçimini kullanır.
- Tek etkin UTRD/UCD yuvasıyla, eşzamanlı ve polling tabanlı çalışır.
- Normal blok I/O'sunda StorPort tarafından ayrılmış ortak DMA bounce tamponu
  kullanır.
- `READ CAPACITY`, `REPORT LUNS`, `INQUIRY`, `MODE SENSE`, `READ`, `WRITE` ve
  ilgili SCSI komutlarını UFS COMMAND UPIU biçimine dönüştürür.
- StorPort, `disk.sys`, `partmgr.sys` ve dosya sistemi sürücülerinin üst katman
  görevlerini kendisi üstlenmez; onlara standart disk/SCSI yüzeyi sağlar.

Yüksek seviyeli akış:

```text
ACPI UFS0 aygıtı
    |
    |  _HID/_CID, _CRS, _CCA, DEV0._ADR
    v
b2lufs.inf -> b2lufs.sys (StorPort miniport)
    |
    |  HwFindAdapter -> HwInitialize -> HwBuildIo/HwStartIo
    v
StorPort -> disk.sys -> partmgr.sys -> volmgr/filesystem
    |
    v
UFSHCI UTRD/UCD/PRDT -> UFS LUN 0
```

Firmware ve Windows görev sınırı şöyledir:

- UEFI, denetleyicinin güç, saat, PHY, UniPro ve link kalibrasyonunu yapar;
  WinPE dosyalarını UFS üzerinden okuyabilecek bir bağlantı bırakır.
- ACPI, denetleyicinin kimliğini, MMIO kaynaklarını ve alt aygıt topolojisini
  Windows PnP'ye bildirir.
- B2LUFS, canlı denetleyiciyi StorPort'a bağlar ve standart SCSI blok erişimi
  sağlar.
- GPT ve dosya sistemi keşfi Windows depolama yığını tarafından blok okumaları
  üzerinden yapılır; ACPI veya UEFI GPT bölüm listesini Windows'a ayrı bir
  tablo olarak aktarmak zorunda değildir.

## 2. Kaynak ağacı ve dosya sınıfları

### 2.1 Doğrudan SYS ikilisine giren dosyalar

`build-b2lufs.ps1` aşağıdaki beş dosyayı Windows `%TEMP%` altında yerel bir
staging dizinine kopyalar. Üç `.c` dosyası derlenir, iki `.h` dosyası bunların
başlığıdır:

| Dosya | SYS içindeki rolü |
|---|---|
| `windows/b2lufs/b2lufs_hw.h` | İşletim sisteminden bağımsız Exynos 9820 MMIO/kaynak sözleşmesi |
| `windows/b2lufs/b2lufs_transport.h` | StorPort taşıma katmanının veri yapıları ve dış fonksiyonları |
| `windows/b2lufs/b2lufs_resources.c` | ACPI/StorPort kaynaklarını taban adresine göre doğrulayıp bağlama |
| `windows/b2lufs/b2lufs_transport.c` | UFSHCI, UTP, UPIU, PRDT, SCSI, DMA ve GPT veri yolu |
| `windows/b2lufs/b2lufs_storport.c` | StorPort callback'leri, DriverEntry, PnP ve registry telemetrisi |

### 2.2 Paket tanımı ve derleme dosyaları

| Dosya | Rolü |
|---|---|
| `windows/b2lufs/b2lufs.inf.in` | ARM64 PnP sürücü paketinin INF şablonu |
| `windows/b2lufs/build-b2lufs.ps1` | Windows MSVC/WDK ile yalnız `b2lufs.sys` derlemesi |
| `windows/b2lufs/b2lgptdump.c` | WinPE'de PhysicalDrive0 GPT bölgelerini salt-okunur döken ve başarılı doğrulamadan sonra Windows bölüm önbelleğini yenileyen freestanding ARM64 araç |
| `windows/b2lufs/test_resources.c` | WDK gerektirmeyen kaynak-bağlama ve GPT normalizasyon birim testi |
| `windows/b2lufs/README.md` | Tasarım ve çalışma davranışının geliştirici notları |
| `windows/b2lufs/AI_DRIVER_BUILD_REFERENCE.md` | Bu dosya; kaynak ve build zinciri referansı |

### 2.3 WinPE ve dağıtım zincirindeki ilgili dosyalar

| Dosya | Rolü |
|---|---|
| `tools_b2lufs.sh` | WSL'de imza, katalog, manifest ve `boot.wim` enjeksiyonu |
| `windows/startnet.cmd` | WinPE açılışında PnP kurulumu ve çalışma zamanı yardımcı akışı |
| `cache_esp.sh` | SYSTEM/CACHE FAT imajlarını ve Odin için sparse tar dosyalarını üretme |
| `tools_odin_winpe_bundle.sh` | BOOT + SYSTEM + CACHE'i tek Odin AP arşivinde birleştirme |
| `yap.sh` | Mu-Silicium UEFI ve isteğe bağlı ACPI derleme/overlay zinciri |

### 2.4 Firmware ve ACPI tarafındaki ilgili dosyalar

| Dosya | Rolü |
|---|---|
| `silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl` | Windows'a UFS0 ACPI aygıtını ve kaynaklarını ilan eder |
| `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Include/ACPI.inc` | Derlenmiş AML tablolarını firmware volume'a ekleme tanımı |
| `mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/PlatformUfsLib/BoardInit.c` | Exynos 9820 UFS güç, pin, adres, saat ve coherency kurulumu |
| `mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/PlatformUfsLib/UfsCal.c` | Samsung PHY/UniPro CAL dizileri ve kalibrasyon işlevleri |
| `mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/PlatformUfsLib/UfsCal.h` | CAL mod, board, PHY katmanları ve fonksiyon prototipleri |
| `mu-silicium-overlay/Silicon/Samsung/S5E9820Pkg/Library/PlatformUfsLib/PlatformUfsLib.inf` | EDK2 kütüphane modülü tanımı |
| `mu-silicium-overlay/Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.inf` | EDK2 UFS DXE sürücüsü modül tanımı |

### 2.5 Araç matrisi

| Araç | Çalıştığı taraf | Bu zincirdeki görevi |
|---|---|---|
| `powershell.exe` | Windows | Yerel MSVC/WDK derleme betiğini çalıştırır |
| `vcvarsamd64_arm64.bat` | Windows | x64 host / ARM64 target MSVC ortamını kurar |
| `cl.exe` | Windows | Üç C translation unit'i ARM64 kernel object'e derler |
| `link.exe` | Windows | Object'leri StorPort/NT kernel importlarıyla SYS'ye bağlar |
| WDK headers ve `.lib` dosyaları | Windows | StorPort/NT DDI, ARM64 kernel ABI ve import library'leri |
| `Inf2Cat.exe` | Windows, WSL'den çağrılır | INF paketini doğrular ve catalog üretir |
| `signtool.exe` | Windows, WSL'den çağrılır | Catalog imzalar; catalog/SYS eşleşmesini doğrular |
| `wslpath` | WSL | Linux ve Windows yol biçimleri arasında dönüşüm yapar |
| `openssl` | WSL | Test sertifikası ve geçici PFX üretir |
| `osslsigncode` | WSL | SYS PE dosyasına Authenticode imzası ekler |
| `sha256sum` | WSL | Paket, WIM ve Odin artefakt bütünlüğünü ölçer |
| `wimlib-imagex` | WSL | INF/SYS/CAT/manifest/startnet/GPT dump aracını `boot.wim`e ekler ve geri okur |
| `dosfstools` / `mkfs.vfat` | WSL | SYSTEM/CACHE FAT volume imajlarını biçimlendirir |
| `mtools` (`mmd`, `mcopy`, `mdir`) | WSL | Loop mount olmadan FAT imaj içeriğini düzenler ve listeler |
| `img2simg` | WSL | Ham SYSTEM/CACHE'i Android sparse imaja dönüştürür |
| `tar` | WSL | Odin'in kabul ettiği ustar arşivlerini üretir |
| `dd`, `od` | WSL | Ham imaj oluşturur ve BPB/sparse alanlarını doğrular |
| `iasl` | WSL | ACPI DSL tablolarını AML'ye derler |
| `python3` | WSL | Mu-Silicium build'i ve repo içi ACPI/UEFI yardımcılarını çalıştırır |
| `clang` / `lld-link` / `llvm-dlltool` | WSL | Freestanding ARM64 `b2lgptdump.exe` ve host testleri; SYS derleyicisi değildir |

## 3. Ana sürücü dosyaları

### 3.1 `b2lufs_hw.h`

Bu başlık WDK API'lerinden bağımsız tutulmuştur. Böylece hem kernel sürücüsü
hem de host üzerinde çalışan küçük birim testi aynı donanım sözleşmesini
kullanabilir.

Tanımladığı altı fiziksel kaynak:

| Kaynak | Fiziksel taban | Minimum uzunluk |
|---|---:|---:|
| Standart UFSHCI | `0x13D60000` | `0x1000` |
| Samsung VS-HCI | `0x13D61100` | `0x0200` |
| UniPro | `0x13D50000` | `0x8000` |
| PHY PMA | `0x13D64000` | `0x0800` |
| I/O coherency | `0x13C10700` | `0x0004` |
| PMU UFS PHY control | `0x15860724` | `0x0004` |

Başlıktaki önemli sabit grupları:

- `B2LUFS_*_BASE` ve `B2LUFS_*_LENGTH`: ACPI `_CRS` ile StorPort
  `ACCESS_RANGE` dizisinin ortak fiziksel sözleşmesi.
- `B2LUFS_UNIPRO_MCLK_HZ`: UniPro zamanlama hesabında kullanılan 166 MHz
  referans saat.
- `B2LUFS_IO_COHERENCY_BITS`: Exynos SYSREG içindeki UFS coherency bitleri.
- `B2LUFS_REG_*`: standart UFSHCI register ofsetleri.
- `B2LUFS_VS_*`: Samsung vendor HCI register ofsetleri.
- `B2LUFS_RESOURCE_ID`: bağlı sanal adreslerin sabit indeksleri.
- `B2LUFS_RESOURCE`: StorPort kaynağının OS-bağımsız görünümü.
- `B2LUFS_RESOURCES`: doğrulanmış altı mapped base pointer'ı.

`B2LUfsBindResources()` prototipi de burada bulunur. Fonksiyon adres sırasına
güvenmez; kaynakları fiziksel tabana göre eşler.

### 3.2 `b2lufs_resources.c`

Bu dosya yalnız kaynak sözleşmesini doğrular. MMIO taraması, fiziksel adres
tahmini veya kendi başına `MmMapIoSpace` kullanımı yapmaz.

`B2LUfsExpected[]` dizisi her kaynağın taban ve minimum boyutunu içerir.
`B2LUfsBindResources()`:

1. Tam olarak altı kaynak ister.
2. Her beklenen tabanı giriş dizisinin tamamında arar.
3. Kaynağın memory kaynağı olduğunu kontrol eder.
4. Uzunluğun minimum değerden kısa olmadığını kontrol eder.
5. StorPort'un verdiği mapped pointer'ın NULL olmadığını kontrol eder.
6. Aynı fiziksel tabanın tam bir kez bulunmasını ister.
7. Sonuç pointer'larını `B2LUFS_RESOURCE_ID` sırasına yerleştirir.

Dönüş değerleri:

| Değer | Anlam |
|---:|---|
| `0` | Kaynak kümesi eksiksiz ve geçerli |
| `-1` | NULL argüman veya yanlış kaynak sayısı |
| `-2` | Memory olmayan, kısa veya mapped pointer'ı olmayan kaynak |
| `-3` | Eksik ya da yinelenmiş fiziksel taban |

### 3.3 `b2lufs_transport.h`

Bu başlık StorPort katmanı ile UFS taşıma katmanı arasındaki sözleşmedir.

#### `B2LUFS_SRB_EXTENSION`

Her SRB için StorPort tarafından ayrılır. İçerdiği başlıca alanlar:

- İmza ve özgün aktarım uzunluğu.
- Scatter/gather eleman sayısı ve SGL pointer'ı.
- `StorPortGetSystemAddress` sonucu.
- Sistem adresi çözümleme durumu.

Bu bilgilerin adapter extension yerine SRB extension içinde tutulmasının
nedeni `HwBuildIo` çağrılarının eşzamansız olabilmesidir. Bilgi yalnız ilgili
isteğin yaşam süresi boyunca geçerlidir.

#### `B2LUFS_ADAPTER`

Miniportun aygıt başına ana durum yapısıdır. Alanları işlevsel olarak şu
gruplara ayrılır:

- Doğrulanmış ve eşlenmiş MMIO kaynakları.
- StorPort common-buffer sanal/fiziksel adresleri.
- Tek UTRD ve UCD yuvası.
- Erken `READ CAPACITY` cevabı ve doğrulanmış kapasite bilgisi.
- `REPORT LUNS` ham cevabı ve sayımları.
- LBA0/LBA1, PMBR, GPT başlığı ve entry-array doğrulama bilgileri.
- Windows için CPU belleğinde tutulan GPT snapshot'ı.
- Normal I/O için ortak DMA bounce tamponu.
- HCI kapasite/durum ve transfer/task slot sayıları.
- StorPort'un seçtiği SRB, adresleme, bus ve kuyruk özellikleri.
- Gecikmeli bus scan timer nesnesi ve durumları.
- PnP/dump/remove durumu.
- Sınırlı ETW telemetri sayaçları.
- Son UTRD OCS, interrupt, PRDT ve SGL bilgileri.
- Taşımanın yeni komut kabul edip edemeyeceğini belirten `TransportReady`.

Başlıkta dışarı açılan taşıma fonksiyonları:

- `B2LUfsAllocateTransport`: common DMA alanlarını ve tek slotu ayırır.
- `B2LUfsStartTransport`: canlı linki resetlemeden transfer kuyruğunu devralır.
- `B2LUfsStopTransport`: yalnız miniportun UTP transfer listesini durdurur.
- `B2LUfsResetTransport`: sahip olunan transfer kuyruğunu tekrar kurar.
- `B2LUfsProbeLun0Capacity`: gerçek LUN 0 kapasitesini okur ve doğrular.
- `B2LUfsProbeLuns`: standart `REPORT LUNS` snapshot'ı alır.
- `B2LUfsProbeMediaHeaders`: blok okuma yoluyla PMBR/GPT verisini toplar.
- `B2LUfsTryCompleteCachedCapacity`: uygun kapasite SRB'sini yazılımda tamamlar.
- `B2LUfsTryCompleteCachedGptRead`: snapshot kapsamındaki GPT okumasını
  yazılımda tamamlar.
- `B2LUfsHandlePnpSrb`: legacy StorPort PnP SRB'sini işler.
- `B2LUfsReleaseBusScanTimer`: timer nesnesini serbest bırakır.
- `B2LUfsExecuteScsi`: normal SCSI SRB'sini UFS LUN 0'a yönlendirir.

### 3.4 `b2lufs_transport.c`

Bu dosya miniportun donanıma en yakın bölümüdür.

#### Yardımcı veri işlevleri

- Big-endian ve little-endian 16/32/64-bit okuma/yazma yardımcıları.
- Kernel C runtime bağımlılığını önleyen yerel zero/copy yardımcıları.
- GPT için CRC32 ve GPT header CRC hesabı.
- CDB'den LBA ve blok sayısı çözümleme.
- SRB bayrakları belirsiz olduğunda CDB opcode'undan veri yönü çıkarma.

#### UFSHCI/UTP veri yapıları

Dosya, UFSHCI'nin gerektirdiği yerleşimlerle uyumlu özel yapılar tanımlar:

- UTRD: UTP Transfer Request Descriptor.
- UCD: command/response UPIU ve PRDT'yi içeren command descriptor.
- COMMAND ve RESPONSE UPIU başlıkları.
- PRDT elemanları.

`C_ASSERT` kontrolleri kritik yapı boyutlarının derleme sırasında değişmemesini
sağlar. Bu kontroller ABI ve DMA yerleşimi için önemlidir.

#### Common DMA ve PRDT

`B2LUfsAllocateTransport()` StorPort'tan uncached ortak bellek alır. Bu alan
içinde hizalanmış olarak:

- UTRD,
- UCD,
- kapasite probu tamponu,
- REPORT LUNS tamponu,
- normal I/O bounce tamponu

yerleştirilir.

Normal I/O tamponu 128 KiB ile sınırlıdır. `B2LUfsBuildPrdt()` bu alanı en
fazla 32 adet 4 KiB fiziksel parçaya böler. Windows'un rastgele sayfalarına
doğrudan DMA yapılmaz; WRITE öncesinde Windows sistem adresinden ortak tampona,
READ sonrasında ortak tampondan Windows sistem adresine CPU kopyası yapılır.

#### Denetleyiciyi devralma

`B2LUfsStartTransport()`:

- HCI capability/status değerlerini okur.
- HCE ve link-ready koşullarını doğrular.
- Mevcut UTRL/UTMR slot durumlarını kontrol eder.
- Miniportun tek UTRD listesinin tabanını programlar.
- Transfer-list run/stop bitini yönetir.
- Exynos transfer-yolu vendor ayarlarını uygular.
- Aktif taşıma sırasında hardware Auto-Hibern8 timer'ını kapatır.

Bu yol PHY CAL, `DME_LINKSTARTUP`, UIC power-mode negotiation veya UFS device
configuration işlemlerini yeniden yapmaz; bunlar firmware katmanının işidir.

#### Komut gönderme

`B2LUfsSubmitScsi()` ana donanım gönderim rutinidir:

1. UTRD/UCD/UPIU yapılarını sıfırlar ve doldurur.
2. SCSI CDB'yi COMMAND UPIU içine kopyalar.
3. Veri yönünü UTRD ve UPIU alanlarına yazar.
4. Gerekirse PRDT'yi kurar.
5. Önceki write-one-to-clear tamamlanma durumunu temizler.
6. Slot 0 doorbell bitini çalar.
7. Kesme kullanmadan, sınırlı süreyle tamamlanmayı poll eder.
8. Doorbell, taze transfer-complete olayı, UTRD OCS ve RESPONSE UPIU'yu
   doğrular.
9. SCSI status/residual length/sense bilgisini SRB durumuna çevirir.

Kesme callback'i tanımlı olsa da veri yolu polling ile tamamlanır; miniport
interrupt sahipliği üzerinden iş üretmez.

#### Keşif ve metadata

- `B2LUfsProbeLun0Capacity`: önce `READ CAPACITY(10)`, yalnız adres alanı
  yetmezse `READ CAPACITY(16)` kullanır.
- `B2LUfsProbeLuns`: LUN 0 üzerinden `REPORT LUNS` gönderir ve yalnız envanter
  amacıyla sonucu saklar.
- `B2LUfsProbeMediaHeaders`: `READ(10)` ile LBA0, LBA1 ve GPT entry dizisini
  okur; imza, boyut ve CRC alanlarını doğrular.
- `B2LUfsBuildWindowsGptSnapshot`: doğrulanmış GPT verisinin CPU tarafındaki
  tutarlı Windows görünümünü üretir.
- `B2LUfsTryCompleteCachedGptRead`: tamamı snapshot kapsamına giren metadata
  okumasını donanıma göndermeden cevaplar.

Snapshot yalnız RAM'deki cevap görünümüdür. On-media GPT'yi doğrudan değiştiren
bir işlem değildir.

#### Normal SCSI isteği

`B2LUfsExecuteScsi()`:

- Path/Target/LUN sınırını denetler.
- PnP ve power isteklerini uygun yazılım yoluna yönlendirir.
- Kapasite/GPT cache yolunda tamamlanmamış normal komutları UFS'e gönderir.
- Uygun READ/WRITE CDB'lerinde blok uzunluğu ile transfer uzunluğunu eşler.
- Autosense verisini SRB sense tamponuna taşır.
- Tamamlanma durumunu StorPort'a bildirir.

### 3.5 `b2lufs_storport.c`

Bu dosya Windows StorPort miniport giriş yüzeyidir.

#### `DriverEntry`

`HW_INITIALIZATION_DATA` yapısını doldurur ve `StorPortInitialize` çağırır.
Başlıca seçimler:

- Klasik `SCSI_REQUEST_BLOCK` biçimi.
- BTL8 adresleme.
- `HwFindAdapter`, `HwInitialize`, `HwBuildIo`, `HwStartIo`, `HwInterrupt`,
  `HwResetBus`, `HwAdapterControl` ve `HwUnitControl` callback'leri.
- SRB başına `B2LUFS_SRB_EXTENSION` boyutu.
- Bütün READ/WRITE tamponlarının eşlenebilmesi için StorPort map davranışı.

#### `B2LUfsFindAdapter`

PnP tarafından sağlanan `PORT_CONFIGURATION_INFORMATION` ve
`ACCESS_RANGE` dizisini işler:

1. StorPort unit bus-type özelliğini etkinleştirir.
2. Altı MMIO kaynağını `StorPortGetDeviceBase` ile eşler.
3. OS-bağımsız görünümü `B2LUfsBindResources()` ile doğrular.
4. Adapter ve SRB extension yapılarını hazırlar.
5. Klasik SRB/BTL8, tek bus, target ve queue sınırlarını ayarlar.
6. Common DMA taşıma alanını ayırır.
7. Gecikmeli bus scan için StorPort timer nesnesini oluşturur.

Kaynak eşlemesi başarısızsa o ana kadar alınmış StorPort mapping'leri geri
bırakılır.

#### `B2LUfsInitialize` ve passive init

`HwInitialize` bağlamındaki `B2LUfsInitialize()` taşıma katmanını başlatır.
Başlangıçta PASSIVE_LEVEL gereken prob ve registry işleri
`StorPortEnablePassiveInitialization` ile `B2LUfsPassiveInitialize()` içine
aktarılır.

Passive init sırası:

1. LUN 0 kapasite probu.
2. REPORT LUNS probu.
3. Medya/GPT header probu.
4. Kapasite, Windows GPT snapshot ve snapshot self-check geçerliyse normal
   LUN keşif kapısının açılması.
5. StorPort timer'ın bus scan bildirimi için planlanması.
6. Registry telemetrisinin yayımlanması.

Kapı açılmadan gelen normal SCSI keşif istekleri aygıt yok durumuyla
tamamlanır; dump stack bu kapıdan muaftır. Timer callback'i yalnız kapı
açıldıktan sonra `BusChangeDetected` bildirimi üretir. Timer, remove yaşam
döngüsünde iptal edilip bırakılır.

#### `B2LUfsBuildIo`

- İstek için sistem sanal adresini alır.
- Tanı için StorPort scatter/gather listesini kaydeder.
- Bu bilgileri yalnız SRB extension içine yazar.
- Adapter extension'a ve ortak snapshot'lara dokunmaz.
- İsteği serileştirilmiş `HwStartIo` yoluna geçirmek için `TRUE` döndürür.

#### `B2LUfsStartIo`

Kilitli StorPort çalışma yoludur. İsteği `B2LUfsExecuteScsi()`ye verir.
Miniport tek aktif donanım slotu kullandığı için normal istek burada
eşzamanlı tamamlanır. Cache'deki `READ CAPACITY` ve tamamen canonical GPT
snapshot'ına düşen metadata READ cevapları da adapter durumuna yalnız bu
serileştirilmiş yoldan erişerek burada tamamlanır.

#### `B2LUfsUnitControl`

StorPort'un birim yaşam döngüsü ve özellik sorgularını cevaplar:

- Yalnız Path 0 / Target 0 / LUN 0 kabul edilir.
- Unit start/remove/surprise-remove durumları işlenir.
- Paging, hibernation ve dump kullanım bildirimleri karşılanır.
- `ScsiUnitQueryBusType` için `BusTypeUfs` döndürülür.
- `StorQueryCapabilities` için sabit dahili aygıt bilgisi verilir.

Diskin SCSI adresi BTL8 `0/0/0` iken ACPI companion adresi `Address=8` olarak
ilan edilir; bu değer DSDT içindeki `DEV0._ADR=8` ile eşleşir.

#### Adapter control ve reset

`B2LUfsAdapterControl()` destek sorgusu, stop, restart ve power benzeri
StorPort yaşam döngüsü çağrılarını taşımaya çevirir. `B2LUfsResetBus()` yalnız
miniportun sahip olduğu transfer kuyruğunu yeniden kurar.

#### Registry telemetrisi

`B2LUfsPublishLunSnapshot()` aşağıdaki grupları aygıtın
`Device Parameters\B2LUFS` alt anahtarına yazar:

- HCI capability/status ve slot sayıları.
- StorPort SRB/adres/bus/queue sözleşmesi.
- Timer oluşturma/planlama durumları.
- READ CAPACITY sonucu ve ham cevap.
- REPORT LUNS sonucu ve LUN kimlikleri.
- PMBR/GPT imzaları, boyutları ve CRC değerleri.
- RAM'deki Windows GPT snapshot bilgileri.

Bu kayıtlar çalışma mantığının girdisi değil, WinPE RAM registry'sindeki
gözlem çıktılarıdır.

### 3.6 `b2lufs.inf.in`

INF şablonunun önemli bölümleri:

| Alan | Değer ve anlamı |
|---|---|
| `Class` | `SCSIAdapter`; fiziksel storage miniport sınıfı |
| `ClassGuid` | `{4d36e97b-e325-11ce-bfc1-08002be10318}` |
| `CatalogFile` | `b2lufs.cat`; INF ve SYS hash'lerini kapsar |
| `DriverVer` | Paketin tarih ve dört parçalı sürümü |
| `DefaultDestDir` | `12`; `%SystemRoot%\System32\drivers` |
| Manufacturer modeli | Yalnız `NTarm64` |
| Hardware match | `ACPI\BYND9820` |
| `AddService` flag | `0x00000002`; aygıtla ilişkili servis |
| `ServiceType` | `1`; kernel driver |
| `StartType` | `3`; PnP/demand start |
| `ErrorControl` | `1`; normal hata denetimi |
| `ServiceBinary` | `%12%\b2lufs.sys` |

`SourceDisksFiles` içindeki açık `b2lufs.sys` eşlemesi, Inf2Cat'in ikiliyi
katalog paketinin bir üyesi olarak hash'lemesini sağlar.

INF yalnız vendor compatible kimliğine bağlanır. Genel PCI/UFS class kimliği
eklenmez; böylece aynı HBA için başka bir miniportla eşleşme alanı açılmaz.

### 3.7 `test_resources.c`

Bu test WDK veya Windows kernel ortamı gerektirmez. Kaynakların farklı sırada
verilmesini, doğru base pointer'lara bağlanmasını ve hatalı memory/duplicate
kaynakların reddedilmesini `assert()` ile doğrular. Ayrıca SYSTEM/CACHE GPT
adlarının tam ve büyük/küçük harf duyarsız UTF-16LE eşleşmesini, Basic Data
GUID dönüşümünü, attribute temizliğini ve kısmi/bozuk adların değiştirilmeden
reddedilmesini sınar.

Yerel çalıştırma örneği:

```bash
clang -std=c11 -Wall -Wextra -Werror \
  windows/b2lufs/b2lufs_resources.c \
  windows/b2lufs/test_resources.c \
  -o /tmp/b2lufs-resources-test
/tmp/b2lufs-resources-test
```

Bayraklar:

- `-std=c11`: C11 dil modu.
- `-Wall -Wextra`: geniş kullanıcı-uzayı uyarı kümesi.
- `-Werror`: uyarıları hata yapar.
- `-o`: test executable çıktı yolu.

Bu test yalnız saf kaynak/GPT yardımcılarını kapsar; StorPort, gerçek MMIO veya
fiziksel UFS GPT erişimi çalıştırmaz.

## 4. Windows MSVC/WDK derlemesi

### 4.1 Araçlar

SYS derlemesinde gereken Windows bileşenleri:

- Visual Studio C++ Build Tools.
- Host x64 -> target ARM64 `cl.exe` ve `link.exe`.
- `vcvarsamd64_arm64.bat`.
- Windows Driver Kit ARM64 kernel headers/libraries.
- WDK ile aynı sürüm Windows SDK headers.
- PowerShell 5+ veya uyumlu `powershell.exe`.

`build-b2lufs.ps1` en yeni uygun WDK sürümünü şu dosyaların birlikte mevcut
olmasına göre seçer:

- `Include/<version>/km/ntddk.h`
- `Include/<version>/km/storport.h`
- `Lib/<version>/km/arm64/storport.lib`
- `Lib/<version>/km/arm64/ntoskrnl.lib`

Visual Studio tarafında aynı kurulum ağacındaki en yeni
`bin/Hostx64/arm64/cl.exe` ve `link.exe` seçilir.

### 4.2 Neden Windows TEMP staging kullanılır?

Kaynak repo WSL'nin `\\wsl.localhost\...` UNC yolu üzerinden açılabilir.
`cmd.exe` UNC çalışma dizinini normal bir current directory olarak kullanamaz.
PowerShell betiği beş kaynak/header dosyasını `%TEMP%\b2lufs-arm64-<GUID>`
altına kopyalar, derlemeyi orada yapar ve yalnız doğrulanmış SYS'yi repo
`out\b2lufs` dizinine geri kopyalar.

Staging dizini `finally` bloğunda beş kısa yeniden denemeyle temizlenir.
Antivirüs veya kapanmakta olan compiler dosyayı kısa süre tuttuğunda temizlik
uyarısı gerçek build sonucunu gölgelemez.

### 4.3 PowerShell parametreleri

`build-b2lufs.ps1` parametreleri:

| Parametre | Varsayılan | İşlev |
|---|---|---|
| `-ProjectRoot` | Betikten iki üst dizin | Repo kökü |
| `-OutDir` | `<root>\out\b2lufs` | SYS çıktı dizini |
| `-KitsRoot` | `%ProgramFiles(x86)%\Windows Kits\10` | WDK/SDK kökü |
| `-WdkVersion` | Otomatik en yeni uygun sürüm | Sabit WDK sürümü seçimi |

Ana derleme komutu:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

PowerShell seçenekleri:

- `-NoProfile`: kullanıcı profil betiklerini yüklemez; ortamdan gelen yan
  etkileri azaltır.
- `-ExecutionPolicy Bypass`: yalnız bu process için betiğin policy tarafından
  engellenmesini önler; kalıcı system policy değiştirmez.
- `-File`: çalıştırılacak `.ps1` yolunu belirtir.

Bu komutun görevi yalnız `out/b2lufs/b2lufs.sys` üretmektir. Katalog, imza,
WIM ve Odin işlemleri WSL tarafında yapılır.

### 4.4 `cl.exe` derleme bayrakları

Betikteki tam ortak compiler bayrak dizisi:

```text
/nologo /c /TC /kernel /Zl /GS- /W4 /WX /wd4324 /O1
/D_WIN32_WINNT=0x0A00 /D_ARM64_=1
/I"<WDK>\km" /I"<WDK>\shared" /I"<WDK>\ucrt" /I"<WDK>\um"
```

Ayrıntılı anlamları:

| Bayrak | İşlevi ve bu projedeki gerekçesi |
|---|---|
| `/nologo` | MSVC banner'ını bastırır; log yalnız önemli build çıktısını taşır. |
| `/c` | Yalnız compile yapar; link adımı ayrı `link.exe` çağrısıdır. |
| `/TC` | Uzantıdan bağımsız tüm girdileri C olarak yorumlar. |
| `/kernel` | MSVC'nin kernel-mode code generation ve kısıtlarını etkinleştirir. |
| `/Zl` | Object içine varsayılan CRT library directive'i koymaz. |
| `/GS-` | Stack security-cookie üretimini kapatır; CRT/runtime bağımlılığı olmayan mevcut minimal link sözleşmesinin bilinçli seçimidir. Koruma maliyeti ayrıca değerlendirilmelidir. |
| `/W4` | Yüksek uyarı seviyesi. |
| `/WX` | Bütün etkin uyarıları build hatasına çevirir. |
| `/wd4324` | ARM64 WDK header yapılarındaki hizalama/padding uyarısını seçici olarak bastırır; genel uyarı seviyesini düşürmez. |
| `/O1` | Kod boyutu odaklı optimizasyon. |
| `/D_WIN32_WINNT=0x0A00` | Windows 10+ API/DDI deklarasyonlarını seçer. |
| `/D_ARM64_=1` | WDK başlıklarında ARM64 hedef yollarını etkinleştirir. |
| `/I...\km` | Kernel-mode WDK başlıkları. |
| `/I...\shared` | Kernel/user tarafından paylaşılan NT ve platform başlıkları. |
| `/I...\ucrt` | `vadefs.h` gibi temel SDK deklarasyonları. |
| `/I...\um` | WDK header bağımlılıklarının çözümlenmesi; hedefi user-mode yapmaz. |
| `/Fo<file>` | Her kaynak için object çıktı adını belirler. |

`_KERNEL_MODE` elle `/D` ile tanımlanmaz. `/kernel` gereken compiler durumunu
kurar; `_KERNEL_MODE` MSVC'nin rezerve makrosudur ve elle tanımı `/WX`
altında uyarıyı build hatasına çevirebilir.

Üretilen object dosyaları:

```text
b2lufs_resources.obj
b2lufs_transport.obj
b2lufs_storport.obj
```

### 4.5 `link.exe` bayrakları

Tam link biçimi:

```text
link.exe /nologo /kernel /driver /subsystem:native
  /entry:DriverEntry /machine:arm64 /nodefaultlib
  /libpath:"<WDK>\Lib\<version>\km\arm64"
  /out:"b2lufs.sys"
  b2lufs_resources.obj b2lufs_transport.obj b2lufs_storport.obj
  storport.lib ntoskrnl.lib
```

| Bayrak/girdi | Anlamı |
|---|---|
| `/nologo` | Linker banner'ını bastırır. |
| `/kernel` | Kernel-mode ikili link semantiğini seçer. |
| `/driver` | Çıktıyı Windows kernel driver PE'si olarak işaretler. |
| `/subsystem:native` | Win32 kullanıcı subsystem'i yerine Native subsystem seçer. |
| `/entry:DriverEntry` | PE entry point olarak miniportun `DriverEntry` fonksiyonunu kullanır. |
| `/machine:arm64` | PE machine değerini ARM64 (`0xAA64`) yapar. |
| `/nodefaultlib` | CRT ve diğer varsayılan library'leri otomatik bağlamaz. |
| `/libpath` | ARM64 kernel import library dizini. |
| `/out` | SYS çıktı adı. |
| `storport.lib` | StorPort DDI importları. |
| `ntoskrnl.lib` | NT kernel importları. |

Bu link sözleşmesi statik C runtime veya user-mode library bağlamaz.

### 4.6 Derleme sonrası PE doğrulaması

PowerShell betiği kopyalamadan önce:

1. Dosyanın var ve boş olmadığını kontrol eder.
2. DOS `MZ` imzasını kontrol eder.
3. `e_lfanew` üzerinden PE imzasını bulur.
4. PE machine alanının `0xAA64` olduğunu doğrular.
5. Çıktıyı `out/b2lufs/b2lufs.sys` yoluna kopyalar.
6. SHA-256 değerini ekrana basar.

## 5. INF, katalog ve imza zinciri

### 5.1 `tools_b2lufs.sh`

Bu betik WSL'de çalışır. Kullanıcının tercih edilen iki aşamalı akışında SYS
önceden PowerShell ile derlenir, ardından:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh
```

Betik aşağıdaki işleri yapar:

1. WDK/SDK ve Visual Studio araç yollarını doğrular.
2. `B2L_SKIP_MSVC_BUILD=1` ise mevcut SYS'nin sürücü kaynaklarından eski olmadığını
   kontrol eder.
3. WSL clang/lld ile salt-okunur GPT okuyan ve yalnız Windows'un bellek içi
   bölüm önbelleğini yenileyen ARM64 `b2lgptdump.exe` üretir.
4. SYS'yi test sertifikasıyla Authenticode imzalar.
5. INF şablonunu `out/b2lufs/b2lufs.inf` olarak kopyalar.
6. Inf2Cat ile INF+SYS katalogu üretir.
7. PEM anahtar/sertifikadan geçici PFX oluşturur.
8. CAT dosyasını SignTool ile imzalar.
9. CAT imzasını ve SYS-katalog eşleşmesini doğrular.
10. INF/SYS/CAT/GPTDUMP SHA-256 manifesti üretir.
11. Paket, GPT dump aracı ve `startnet.cmd` dosyasını `boot.wim` içine ekler.
12. WIM'den geri extract ederek bütün hash'leri tekrar doğrular.

Betik `set -euo pipefail` kullanır:

- `-e`: başarısız komutta çıkar.
- `-u`: tanımsız shell değişkenini hata yapar.
- `pipefail`: pipeline içindeki erken bir komutun hatasını kaybetmez.

### 5.2 Ortam değişkenleri

| Değişken | İşlev |
|---|---|
| `B2L_WINROOT` | WSL içinden Windows C: kökü; varsayılan `/mnt/c` |
| `B2L_WDK_ROOT` | Windows Kits kökünü override eder |
| `B2L_INF2CAT` | Belirli `Inf2Cat.exe` yolu |
| `B2L_SIGNTOOL` | Belirli `signtool.exe` yolu |
| `B2L_MSVC_INCLUDE` | `vadefs.h` içeren include dizini |
| `B2L_VCVARS_ARM64` | Belirli `vcvarsamd64_arm64.bat` yolu |
| `B2L_MSVC_ARM64_BIN` | ARM64 hedef `cl.exe/link.exe` dizini |
| `B2L_SKIP_MSVC_BUILD=1` | SYS derlemesini atlar; önceden PowerShell ile üretilmiş SYS'yi kullanır |
| `B2L_ALLOW_STALE_SYS=1` | Kaynaklardan eski SYS kapısını bilinçli olarak aşar; normal kullanım için değildir |

### 5.3 SYS imzası: `osslsigncode`

Kullanılan biçim:

```bash
osslsigncode sign \
  -certs windows/b2lfb/b2l-test.pem \
  -key windows/b2lfb/b2l-test.key \
  -n "beyond2lte Exynos UFS StorPort LUN 0" \
  -h sha256 \
  -in out/b2lufs/b2lufs.sys \
  -out out/b2lufs/b2lufs-signed.sys
```

| Seçenek | Anlamı |
|---|---|
| `sign` | Authenticode imza işlemi |
| `-certs` | PEM sertifika zinciri |
| `-key` | PEM özel anahtar |
| `-n` | İmzalı dosyanın açıklama adı |
| `-h sha256` | Digest algoritması SHA-256 |
| `-in` / `-out` | Girdi ve imzalı çıktı |

`windows/b2lfb/b2l-test.pem` ve `.key` burada yalnız ortak test-imza kimliği
olarak kullanılır. Dosyalar yoksa `openssl req -x509 -newkey rsa:2048 -nodes
-days 3650` ile self-signed test sertifikası üretilir.

### 5.4 Catalog üretimi: `Inf2Cat.exe`

```text
Inf2Cat.exe /driver:<out-b2lufs-windows-path> /os:10_GE_ARM64
```

- `/driver:`: INF ve ona ait dosyaların bulunduğu paket dizini.
- `/os:10_GE_ARM64`: Windows 10 ve sonrası ARM64 hedefleri için INF/paket
  doğrulaması ve katalog üretimi.

Çıktı `b2lufs.cat`, INF'nin referans verdiği SYS hash'ini kapsar.

### 5.5 Catalog imzası: `signtool.exe`

PEM sertifika ve anahtar önce geçici PFX'e çevrilir:

```bash
openssl pkcs12 -export -out b2l-test.pfx \
  -inkey b2l-test.key -in b2l-test.pem -passout pass:<password>
```

Catalog imzası:

```text
signtool.exe sign /v /fd sha256 /f <pfx> /p <password> b2lufs.cat
```

Doğrulama:

```text
signtool.exe verify /v /pa b2lufs.cat
signtool.exe verify /v /pa /c b2lufs.cat b2lufs.sys
```

| Seçenek | Anlamı |
|---|---|
| `sign` | Dosyayı imzalar. |
| `/v` | Ayrıntılı çıktı. |
| `/fd sha256` | File digest SHA-256. |
| `/f` | PFX dosyası. |
| `/p` | PFX parolası. |
| `verify` | İmza doğrulaması. |
| `/pa` | Default Authenticode verification policy. |
| `/c <cat>` | İkinci dosyanın belirtilen katalog içinde yer aldığını doğrular. |

Geçici PFX işlem bitince silinir; PEM kaynak anahtarı kalıcı test anahtarıdır.

### 5.6 Manifest

`out/b2lufs/b2lufs-manifest.txt` şu alanları içerir:

```text
B2LUFS_PACKAGE=<paket-kimliği>
B2LUFS_INF_SHA256=<hash>
B2LUFS_SYS_SHA256=<hash>
B2LUFS_CAT_SHA256=<hash>
B2LUFS_GPTDUMP_SHA256=<hash>
```

Manifest Windows sürücü imzasının yerine geçmez. Amacı farklı paketleme
katmanlarının aynı INF/SYS/CAT üçlüsünü kullandığını doğrulamaktır.

## 6. `boot.wim` enjeksiyonu

`tools_b2lufs.sh`, `wimlib-imagex update` ile WIM image index 1'e şunları
ekler:

```text
/Windows/B2LUFS/b2lufs.inf
/Windows/B2LUFS/b2lufs.sys
/Windows/B2LUFS/b2lufs.cat
/Windows/B2LUFS/manifest.txt
/Windows/System32/b2lgptdump.exe
/Windows/System32/startnet.cmd
```

Kullanılan temel biçimler:

```bash
wimlib-imagex update <boot.wim> 1 --command="delete --force <target>"
wimlib-imagex update <boot.wim> 1 --command="add '<source>' <target>"
wimlib-imagex extract <boot.wim> 1 <target> --dest-dir=<dir> --no-acls
```

- `update`: WIM içeriğini değiştirir.
- `1`: WIM image index.
- `delete --force`: eski kopyayı varsa kaldırır.
- `add`: yeni dosyayı belirtilen Windows yoluna koyar.
- `extract`: paketleme sonrası geri-okuma doğrulaması.
- `--no-acls`: Linux/WSL geçici doğrulamasında NT ACL restore gereksinimini
  kaldırır; WIM içindeki dosya baytlarını değiştirmez.

`startnet.cmd` WIM'e eklenmeden önce LF satır sonları CRLF'e çevrilir.

## 7. WinPE çalışma dosyası: `windows/startnet.cmd`

Bu dosya derleyici girdisi değildir; WinPE runtime orchestration dosyasıdır.

Ana bölümleri:

- `wpeinit.exe` ile WinPE PnP/network initialization.
- Sürücü kurulmadan önce salt-okunur DiskPart envanteri.
- Manifest ve PnP araçlarının varlık denetimi.
- StorPort Operational event channel'ını etkinleştirme.
- `pnputil /add-driver ... /install` ile INF paketini ekleme ve bağlama.
- `pnputil /scan-devices` ile PnP yeniden tarama.
- PhysicalDrive0 üzerinden PMBR, primary header/entries ve backup header ham
  okuması; sonuçların X: RAM diskine yazılması ve başarılı doğrulamadan sonra
  `IOCTL_DISK_UPDATE_PROPERTIES` ile bellek içi bölüm önbelleğinin yenilenmesi.
- Kurulum sonrası disk/partition/volume envanteri.
- Aygıt property ve registry telemetri özeti.
- Snapshot neden/self-check telemetrisi ve StorPort Operational EVTX aktarımı.
- Tanımlı CACHE volume için güvenlik kontrollü bağlama ve dosya yaz/geri-oku
  yardımcı akışı.

Bu CMD dosyası `diskpart clean`, `convert`, `create partition`, `format` veya
GPT'yi yeniden yazan bir komut içermez. CACHE erişimi için disk, partition,
boyut ve volume label doğrulamalarını tamamlamadan dosya yazmaz.

## 8. SYSTEM ve CACHE imajları: `cache_esp.sh`

Betik WSL/Linux araçlarıyla FAT imajı hazırlar ve gerekirse Android sparse
biçimine çevirir.

Sürücü/WinPE zincirinde kullanılan komutlar:

```bash
bash cache_esp.sh --win --system
bash cache_esp.sh --win --bos
```

Seçenekler:

| Seçenek | İşlev |
|---|---|
| `--win` | Yalnız Windows/WinPE dosyalarını ekler; Linux kernel/initramfs'i dışarıda tutar. |
| `--system` | ESP/WinPE içeriği için daha büyük SYSTEM hedef imajını seçer. |
| `--bos` | Tam 600 MiB, boş, FAT32 ve `CACHE` etiketli CACHE imajı üretir. |
| `--linux` | Linux içeriğini bilinçli olarak seçer; Windows UFS paketi akışının parçası değildir. |
| `--baseline` | Windows-only, tanı uygulamalarını dışarıda tutan başlangıç varyantı. |
| `--psci-hvc` | ACPI/firmware HVC varyantına ait Windows başlangıç paketi. |

`--bos`, `--win` olmadan kullanılamaz ve `--system` ile karşılıklı dışlayıcıdır.

### 8.1 FAT32 üretim bayrakları

Boş CACHE için:

```bash
dd if=/dev/zero of=out/cache.img bs=1M count=600
mkfs.vfat -F 32 -S 4096 -s 1 -n CACHE out/cache.img
```

| Bayrak | Anlamı |
|---|---|
| `dd bs=1M count=600` | 600 MiB ham dosya oluşturur. |
| `mkfs.vfat -F 32` | FAT32 seçer. |
| `-S 4096` | FAT logical sector boyutunu UFS logical block boyutuyla 4096 yapar. |
| `-s 1` | Küme başına bir sektör; küme boyutu 4096 bayt. |
| `-n CACHE` | Volume label. |

Betik BPB içinden bytes-per-sector, sectors-per-cluster, label ve FAT32
işaretini geri okuyup doğrular.

### 8.2 Mtools ve sparse araçları

- `mmd -i <img>`: FAT imajı içinde klasör açar.
- `mcopy -i <img>`: host dosyasını FAT imajına kopyalar.
- `mdir -i <img>`: imaj içeriğini ve boş alanı listeler.
- `MTOOLS_SKIP_CHECK=1`: mtools'un partition-table beklentisini atlar; burada
  dosyanın kendisi doğrudan bir FAT volume imajıdır.
- `img2simg <raw> <sparse>`: ham dosyayı Android sparse image'e çevirir.
- Sparse magic `0xED26FF3A` ile doğrulanır.
- `tar -H ustar`: Odin uyumlu basit ustar arşivi üretir.

SYSTEM imajı üretilmeden önce betik WIM içindeki B2LUFS manifestini extract
eder ve SYS hash'inin `out/b2lufs/b2lufs.sys` ile aynı olduğunu doğrular.

## 9. Odin AP paketi: `tools_odin_winpe_bundle.sh`

Varsayılan girdiler:

```text
out/uefi-boot.tar   -> içinde yalnız ham boot.img
out/system.img      -> ham FAT SYSTEM imajı
out/cache.img       -> ham FAT32 CACHE imajı
```

Çıktı:

```text
out/beyond2lte-winpe-ufs.tar
```

İşlem sırası:

1. BOOT tar'ın yalnız `boot.img` içerdiğini doğrular.
2. SYSTEM ve CACHE'i `img2simg` ile Android sparse biçimine dönüştürür.
3. Her sparse başlığın magic değerini kontrol eder.
4. `boot.img`, `system.img`, `cache.img` sırasıyla ustar arşivi oluşturur.
5. Arşivi geri açıp her üyenin SHA-256 değerini kaynakla karşılaştırır.

İsteğe bağlı positional parametreler:

```bash
bash tools_odin_winpe_bundle.sh [boot-tar] [system-raw] [cache-raw]
```

`boot.img` ham kalır; SYSTEM ve CACHE sparse olur. Arşiv Odin'de tek AP
girdisi olarak kullanılacak yapıdadır ve partition table içermediği için
Re-Partition seçeneği kullanılmaz.

## 10. ACPI dosyaları

### 10.1 `DSDT_minimal.dsl`

UFS açısından `Device (UFS0)` şu sözleşmeyi kurar:

- `_HID = "SMSN9820"`: fiziksel vendor host kimliği.
- `_CID = "BYND9820"`: B2LUFS INF eşleşme kimliği.
- `_UID = 0`: instance numarası.
- `_CCA = 1`: coherent DMA ilanı.
- `_STA = 0x0F`: aygıt mevcut, enabled, UI'da görünür ve işlevsel.
- `_CRS`: altı MMIO `Memory32Fixed` kaynağı ve GSIV 281 interrupt kaynağı.
- Alt `Device (DEV0)` ve `_ADR = 8`: StorPort unit PDO ile companion ACPI
  child eşleşmesi.
- `DEV0._RMV = 0`: sabit/dahili medya.

`_CRS` sırası miniportta anlam kaynağı değildir; `b2lufs_resources.c`
fiziksel tabana göre eşler.

### 10.2 `ACPI.inc`

EDK2 firmware volume içine hazır AML bloblarını `SECTION RAW` olarak koyan
FFS tanımıdır. Repo kopyasında blok yorumlu tutulabilir. `yap.sh --acpi`, önce
AML dosyalarını üretir, overlay'i Mu-Silicium'a kopyalar ve yalnız build
workspace içindeki `ACPI.inc` kopyasında satırları etkinleştirir.

Bu ayrım repo dosyasının her ACPI build'inde mekanik olarak kirlenmesini
önler.

### 10.3 ACPI derleme aracı ve bayrakları

`yap.sh --acpi` şu akışı kullanır:

```bash
python3 tools_pptt_gen.py
iasl -ve <table>.dsl
```

- `iasl`: ACPICA ASL compiler.
- `-ve`: yalnız error sınıfındaki tanıları gösterir; warning, remark ve
  optimization mesajlarını bastırır.
- Her `.dsl` kendi `.aml` çıktısına derlenir.
- PPTT bu projede ayrı Python üreticisiyle hazırlanır.

## 11. UEFI UFS dosyaları

### 11.1 `PlatformUfsLib/BoardInit.c`

Exynos 9820 board-level UFS kurulumunu yapar:

- HCI, VS-HCI, UniPro ve PHY PMA base adreslerini `UfsHost` yapısına yazar.
- UFS VCC GPIO'sunu ve PHY isolation register'ını tanımlar.
- 166 MHz UniPro master-clock değerini ayarlar.
- Samsung VS `1US_TO_CNT` ve `IA_TICK_SEL` alanlarını programlar.
- UFS reset/reference-clock pin mux ve pull ayarlarını yapar.
- UFS VCC enable GPIO'sunu output yapar.
- SYSREG UFS I/O coherency bitlerini etkinleştirir.
- Seçilen gear mode bilgisini CAL/UFS DXE katmanına verir.

Bu dosya Windows SYS'ye link edilmez. Görevi Windows yüklenmeden önce çalışan
UEFI UFS DXE sürücüsünün platform bağımlı hazırlığıdır.

### 11.2 `PlatformUfsLib/UfsCal.h`

Samsung CAL katmanının ortak tanımları:

- PWM/HS power-mode sınıflandırma makroları.
- MCLK'den UniPro/PCS zaman değeri hesapları.
- PHY common ve lane-specific adres hesapları.
- Desteklenen gear/lane bit maskeleri.
- Board maskeleri.
- PHY/PCS/UniPro yapılandırma katmanı enum'u.
- `UfsCalPhyCfg` tablo elemanı.
- Pre-link, post-link, pre/post power-mode-change ve Hibern8 callback
  prototipleri.

### 11.3 `PlatformUfsLib/UfsCal.c`

Exynos 9820 için register/value/flag/layer tablolarını ve bu tabloları
uygulayan CAL motorunu içerir. Başlıca evreler:

- `UfsCalInit`: parametre ve hedef board başlangıcı.
- `UfsCalPreLink`: link startup öncesi PHY/PCS/UniPro ayarları.
- `UfsCalPostLink`: link startup sonrası ayarlar.
- `UfsCalPrePmc`: power-mode change öncesi ayarlar.
- `UfsCalPostPmc`: power-mode change sonrası ayarlar.
- `ufs_cal_post_h8_enter`: Hibern8 giriş sonrası kalibrasyon.
- `ufs_cal_pre_h8_exit`: Hibern8 çıkış öncesi kalibrasyon.

PLL/CDR lock bekleme ve lane başına register uygulama yardımcıları dosyanın
içinde statik fonksiyonlardır.

### 11.4 `PlatformUfsLib.inf`

EDK2 library module metadata'sıdır:

- `MODULE_TYPE = DXE_DRIVER`
- `LIBRARY_CLASS = PlatformUfsLib|DXE_DRIVER`
- Sources: `BoardInit.c`, `UfsCal.c`
- Packages: `MdePkg`, `SamsungPkg`
- Library classes: `DebugLib`, `IoLib`, `TimerLib`,
  `UefiBootServicesTableLib`, `UfsCalAdapterLib`
- GPIO protocol dependency: `gEfiGpioProtocolGuid`

EDK2 build sistemi bu dosyayı C compiler komut satırına çevirmek ve library
instance çözümlemek için kullanır.

### 11.5 `UfsDxe/UfsDxe.inf`

Genel Samsung UFS DXE sürücüsünün modül tanımıdır:

- `MODULE_TYPE = DXE_DRIVER`
- `ENTRY_POINT = InitUfsDriver`
- Source: upstream `UfsDxe.c`
- `PlatformUfsLib` ve `UfsCalAdapterLib` üzerinden SoC bağımlı katmana bağlanır.
- `gEfiBlockIoProtocolGuid` yayımlar/kullanır; UEFI block I/O yüzeyini kurar.
- `Depex = TRUE`; DXE dispatcher modülü koşulsuz değerlendirebilir.

Bu DXE sürücüsü UEFI boot erişimi içindir; Windows `b2lufs.sys` ile aynı PE
ikilisi veya aynı driver modeli değildir.

## 12. UEFI build aracı: `yap.sh`

`yap.sh` WSL'de Mu-Silicium ağacına overlay kopyalar, isteğe bağlı ACPI'yi
derler ve `build_uefi.py` çağırır.

Seçenekler:

| Seçenek | İşlev |
|---|---|
| Parametresiz | `DEBUG` UEFI build |
| `-r <mode>` / `--release <mode>` | Build mode değerini geçirir; örn. `RELEASE` |
| `--acpi` | ACPI DSL/AML üretimini ve ACPI.inc aktivasyonunu açar |
| `--psci-hvc` | ACPI build'i de açarak HVC conduit varyantı üretir |
| `--clean` | `build_uefi.py`ye `-c` geçirir |
| `--tar` | Geriye uyumluluk; Odin tar artık varsayılan üretilir |

Asıl EDK2 build çağrısı:

```bash
python3 build_uefi.py -d beyond2lte -r <DEBUG-or-RELEASE> [-c]
```

- `-d beyond2lte`: cihaz platformu.
- `-r`: EDK2 target/build configuration.
- `-c`: temiz build.

İlgili ortam değişkenleri:

| Değişken | İşlev |
|---|---|
| `MU_SILICIUM` | Mu-Silicium workspace yolu |
| `B2L_UFS_PWM=1` | İsteğe bağlı UFS PWM patch akışı |
| `B2L_WDT=0` | Watchdog patch uygulamasını kapatır |
| `OUTDIR` | Üretilen flash dosyalarının kopyalanacağı dizin |

UEFI çıktısı `Mu-beyond2lte*.img` olarak bulunur, `boot.img` adıyla ustar
içine konup `uefi-boot.tar` üretilir ve repo `out/` dizinine kopyalanır.

## 13. Üretilen artefaktlar

Kaynak ile türetilmiş çıktıları ayırmak önemlidir:

| Artefakt | Üreten araç | İçerik |
|---|---|---|
| `out/b2lufs/b2lufs.sys` | `build-b2lufs.ps1`, sonra `tools_b2lufs.sh` imzası | ARM64 kernel miniport |
| `out/b2lufs/b2lufs.inf` | `tools_b2lufs.sh` | INF şablonunun paket kopyası |
| `out/b2lufs/b2lufs.cat` | Inf2Cat + SignTool | İmzalı driver catalog |
| `out/b2lufs/b2lufs-manifest.txt` | `tools_b2lufs.sh` | Paket kimliği ve SHA-256 değerleri |
| `esp/sources/boot.wim` | `tools_b2lufs.sh` tarafından güncellenir | WinPE + B2LUFS paketi |
| `out/system.img` | `cache_esp.sh --win --system` | Ham FAT WinPE/SYSTEM imajı |
| `out/cache.img` | `cache_esp.sh --win --bos` | Ham 600 MiB FAT32 CACHE imajı |
| `out/uefi-boot.tar` | `yap.sh` | Yalnız ham `boot.img` |
| `out/beyond2lte-winpe-ufs.tar` | `tools_odin_winpe_bundle.sh` | Tek Odin AP paketi |

`out/` içeriği kaynak değildir. C veya INF değişikliğinden sonra SYS ve paket
yeniden üretilmelidir.

## 14. Tekrarlanabilir tam build sırası

### 14.1 Windows PowerShell: yalnız SYS derleme

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

### 14.2 WSL: imza, katalog ve WIM enjeksiyonu

```bash
cd ~/beyond2lte-uefi
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh
```

### 14.3 WSL: UEFI/ACPI gerekiyorsa

```bash
bash yap.sh --acpi
```

ACPI/UEFI kaynakları değişmediyse mevcut `out/uefi-boot.tar` kullanılabilir.

### 14.4 WSL: SYSTEM ve boş CACHE

```bash
bash cache_esp.sh --win --system
bash cache_esp.sh --win --bos
```

### 14.5 WSL: tek Odin AP arşivi

```bash
bash tools_odin_winpe_bundle.sh
```

Nihai çıktı:

```text
out/beyond2lte-winpe-ufs.tar
```

## 15. Katmanlar arası değişiklik haritası

Bir özelliğin hangi dosyada değiştirilmesi gerektiğini hızlı belirlemek için:

| Değişiklik türü | Birincil dosya(lar) |
|---|---|
| MMIO taban/uzunluk sözleşmesi | `b2lufs_hw.h`, `b2lufs_resources.c`, `DSDT_minimal.dsl`, `BoardInit.c` |
| StorPort callback veya bus/PnP davranışı | `b2lufs_storport.c` |
| SCSI CDB, UPIU, UTRD, PRDT, DMA | `b2lufs_transport.c`, `b2lufs_transport.h` |
| Adapter/SRB çalışma durumu | `b2lufs_transport.h` |
| PnP hardware ID veya service ayarı | `b2lufs.inf.in`, `DSDT_minimal.dsl` |
| ACPI child adresi/topolojisi | `DSDT_minimal.dsl` |
| UEFI güç/pin/saat/coherency | `BoardInit.c` |
| PHY/UniPro kalibrasyonu | `UfsCal.c`, `UfsCal.h` |
| MSVC compiler/link seçenekleri | `build-b2lufs.ps1` |
| Catalog ve imza politikası | `tools_b2lufs.sh`, `b2lufs.inf.in` |
| WinPE otomasyonu | `windows/startnet.cmd` |
| FAT/SYSTEM/CACHE üretimi | `cache_esp.sh` |
| Odin arşiv biçimi | `tools_odin_winpe_bundle.sh` |
| EDK2 build/overlay | `yap.sh`, ilgili `.inf` dosyaları |

## 16. İnceleme sırasında korunması gereken sınırlar

- SYS yalnız Windows MSVC/WDK ile derlenir; WSL bu aşamada compiler değildir.
- İmza, katalog, WIM, FAT/sparse ve Odin paketleme WSL araç zinciridir.
- `b2lufs_resources.c` ACPI'nin verdiği kaynakları doğrular; kaynak keşfetmez
  veya fiziksel belleği bağımsız haritalamaz.
- `b2lufs_storport.c` Windows driver-model sınırıdır; `b2lufs_transport.c`
  donanım/SCSI taşıma sınırıdır.
- UEFI CAL ve BoardInit dosyaları Windows SYS'nin parçası değildir.
- ACPI aygıtı kaynak/topoloji ilan eder; blok I/O gerçekleştirmez.
- INF PnP eşleşmesi ve servis kurulumunu tanımlar; donanımı başlatmaz.
- `startnet.cmd` sürücüyü derlemez veya imzalamaz; paketlenmiş sürücüyü
  WinPE'de kurar.
- Manifest bütünlük/versiyon eşlemesi içindir; Authenticode catalog imzasının
  yerini tutmaz.
- `out/` dosyaları yeniden üretilebilir build artefaktlarıdır; gerçek kaynak
  değişiklikleri `windows/b2lufs`, ACPI/UEFI overlay ve kök build scriptlerinde
  yapılır.
