# B2LUFS sürücü ve derleme zinciri teknik referansı

Bu belge, `beyond2lte` (Samsung Exynos 9820) için geliştirilen B2LUFS Windows
ARM64 StorPort miniportunun kaynak dosyalarını ve onu üretmek, imzalamak,
WinPE'ye yerleştirmek ve Odin paketine dönüştürmek için kullanılan araçları
açıklar. Başka bir dil modeline veya sürücü geliştiricisine doğrudan bağlam
olarak verilebilmesi için hazırlanmıştır.

Belgenin ilk bölümleri B2LUFS mimarisi ve araç zinciridir. Son bölümler,
B2LTOUCH geliştirmesi sırasında cihazda doğrulanan yöntemleri kullanarak yeni
Windows ARM64 sürücülerinde uygulanacak tasarım, tanı, build, WinPE, flash ve
geri dönüş disiplinini anlatır. Böylece belge farklı bir sohbete tek başına
bağlam olarak verilebilir.

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
| `windows/b2lufs/test_resources.c` | WDK gerektirmeyen kaynak-bağlama birim testi |
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
kaynakların reddedilmesini `assert()` ile doğrular.

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

Bu test yalnız saf kaynak bağlama işlevini kapsar; StorPort veya gerçek MMIO
erişimi çalıştırmaz.

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

## 17. Cihazda doğrulanan ortak geliştirme yöntemi

Bu bölüm B2LTOUCH geliştirmesinden çıkarılmış, yeni Windows ARM64 aygıt
sürücülerinde tekrar uygulanacak çalışma düzenidir. B2LUFS ve B2LTOUCH aynı
flash paketinde bulunsa da dört ayrı katman olarak ele alınmalıdır:

| Katman | Sorumluluk | Başlıca araç |
|---|---|---|
| UEFI/BOOT | Güç, saat, pin, reset, güvenli ilk donanım hazırlığı ve ACPI | `yap.sh` |
| Windows kernel driver | WDF/StorPort/VHF callback'leri ve donanım protokolü | MSVC + WDK PowerShell scripti |
| WinPE | INF kurulumu, servis/PnP hazırlığı ve tanı uygulamaları | `tools_b2lufs.sh`, `tools_b2ltouch.sh`, `wimlib` |
| Flash paketi | BOOT, SYSTEM ve gerekirse CACHE sparse imajları | `cache_esp.sh` |

Bir değişiklik yapıldığında yalnız ilgili katman yeniden üretilmelidir. Bu hem
tur süresini kısaltır hem de her flash turunda tek değişken ilkesini korur.

### 17.1 Doğrulanmış günlük tam sıra

Dokunmatik UEFI ve Windows katmanları birlikte değiştiğinde:

```bash
cd ~/beyond2lte-uefi-8549
bash yap.sh --touch-test --acpi --clean
bash tools_b2ltouch.sh
bash cache_esp.sh --win --system --baseline
tar -tf out/beyond2lte-touch-diag.tar
```

Komutların anlamı:

1. `yap.sh`, güncel UEFI BOOT imajını ve ACPI tablolarını temiz derler.
2. `tools_b2ltouch.sh`, Windows ARM64 SYS ve `b2lmt.exe` dosyasını derler;
   INF/CAT üretir, imzalar ve güncel `boot.wim` içine ekler.
3. `cache_esp.sh`, güncel WinPE içeren SYSTEM ile UEFI içeren BOOT'u tek Odin
   AP arşivine koyar.
4. `tar -tf`, flash öncesi arşivde en az `boot.img` ve `system.img` bulunduğunu
   gözle doğrular.

Nihai dosya:

```text
out/beyond2lte-touch-diag.tar
```

Odin'de yalnız **AP** alanına verilir ve **Re-Partition kapalı** tutulur.

### 17.2 Değişiklik türüne göre en kısa doğru sıra

| Değişiklik | Gerekli komutlar |
|---|---|
| UEFI güç/pin/reset, ACPI veya touch test | `yap.sh` → `cache_esp.sh` |
| Yalnız `b2ltouch` C/INF kaynağı | `tools_b2ltouch.sh` → `cache_esp.sh` |
| Yalnız `b2lufs` C/INF kaynağı | `tools_b2lufs.sh` → `cache_esp.sh` |
| Yalnız `startnet.cmd` veya WinPE içeriği | ilgili `tools_*.sh` → `cache_esp.sh` |
| Yalnız paketleme kuralı | `cache_esp.sh` |
| Temiz ADK WinPE tabanı | `tools_winpe_adk.sh` → UFS → touch → paketleme |

Yalnız Windows dokunmatik sürücüsü değiştiğinde UEFI'yi tekrar derlemek
gerekmez:

```bash
cd ~/beyond2lte-uefi-8549
bash tools_b2ltouch.sh
bash cache_esp.sh --win --system --baseline
tar -tf out/beyond2lte-touch-diag.tar
```

### 17.3 Temiz ADK tabanı oluşturma

WinPE `boot.wim` sıfırdan hazırlanacaksa:

```bash
bash tools_winpe_adk.sh \
  "/mnt/c/Program Files (x86)/Windows Kits/10/Assessment and Deployment Kit/Windows Preinstallation Environment/arm64/Media"
bash tools_b2lufs.sh
bash tools_b2ltouch.sh
bash cache_esp.sh --win --system --baseline
```

UFS önce eklenir; aksi halde WinPE sistem diskine erişemeyebilir. Touch bunun
üzerine eklenir. Temiz WIM kullanmak, daha önceki paketten kalan sürücü veya
registry durumunun sonucu maskelemesini engeller.

### 17.4 Yalnız MSVC/WDK derlemesi ve hazır SYS kullanımı

PowerShell'de doğrudan ARM64 touch build'i:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi-8549\windows\b2ltouch\build-b2ltouch.ps1" -WdkVersion "10.0.28000.0"
```

Sonra WSL'de mevcut çıktıyı imzalama/WIM paketleme aşamasına geçirmek için:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2ltouch.sh
```

`B2L_SKIP_MSVC_BUILD=1`, SYS veya tanı EXE'si yoksa hata verir. Çıktı kaynak
dosyalarından eskiyse de normalde reddedilir. `B2L_ALLOW_STALE_SYS=1` yalnız
acil teşhis kapısıdır; günlük build'de kullanılmamalıdır. PowerShell satırında
Bash'in `\` satır devamı sözdizimi kullanılmaz.

## 18. B2LTOUCH: UEFI keşfinden Windows multi-touch'a

### 18.1 Güvenli keşif ve aktif tarama sınırı

Android DT incelemesinde 47 HSI2C denetleyicisi ve önceden eşlenmiş slave
adresleri bulundu. Kamera, EEPROM, PMIC ve şarj entegrelerine kör aktif tarama
gönderilmedi. Aktif okuma yalnız dokümante edilmiş touch yolu olan B10 üzerinde
yapıldı:

| Kaynak | Fiziksel taban | Uzunluk | Rol |
|---|---:|---:|---|
| B10 HSI2C10 | `0x104B0000` | `0x1000` | sec_ts I²C denetleyicisi |
| B10 USI | `0x10421000` | `0x1000` | HSI2C çalışma modu |

Bu sınır Windows ACPI `_CRS` ve sürücü kaynak doğrulamasında da aynen
korundu. Sürücü rasgele fiziksel adres eşlemez; yalnız bu iki tam 4 KiB kaynağı
kabul eder. DRAM, PMIC veya firmware yazma yolu yoktur.

### 18.2 UEFI sırasında çözülen donanım sorunları

İlk denemelerde `Not Ready`, `Device Error` ve HSI2C timeout görüldü. Çözüm
tek bir register tahmin etmek değil, stok DT ve sürücü sırasını izlemek oldu:

1. Gerekli touch güç hatları açıldı.
2. USI/HSI2C çalışma modu ve hedef slave adresi programlandı.
3. GPIO pinmux ile reset/interrupt yönü hazırlandı.
4. sec_ts reset ve handshake sırası uygulandı.
5. ID okuması ile gerçek cihaz yanıtı doğrulandı; örnek kimlik
   `53 45 37 71 00` oldu.

Kritik parser hatası event kuyruğundaydı. Donanım bazen 32–35 olay raporladı;
sayım 32'yi geçtiğinde bütün kuyruğu temizlemek geçerli koordinatları da
siliyordu. Kuyruk 32 olaylık güvenli parçalar hâlinde boşaltılıp tekrar
poll edilince gerçek temas verileri görünür oldu. UEFI kareli testte farklı
noktalara yapılan dokunmaların koordinatları ve çizgi izi doğrulandı.

### 18.3 Neden inbox HIDI2C yerine KMDF + VHF?

Samsung `sec_ts`, Windows HID-over-I²C descriptor/register protokolünü doğal
olarak sunmuyor. Bu nedenle ACPI'ye sahte HID-over-I²C tanımı ekleyip
`HIDI2C.sys` bağlamak doğru mimari değildir.

B2LTOUCH iki katmanlıdır:

```text
sec_ts donanımı
  -> B10 HSI2C MMIO ve Samsung event parser
  -> b2ltouch.sys (ARM64 KMDF function driver)
  -> Microsoft Virtual HID Framework (VHF)
  -> standart Windows HID Touch Screen child aygıtı
  -> Windows pointer/touch yığını
```

VHF ile üreticiye özel transport kernel sürücüsünde kalırken Windows'a
standart bir HID rapor tanımı sunulur. INF kaynak aygıta `LowerFilters=vhf`
ekler. VHF child oluşturma ve başlatma işlemleri PASSIVE_LEVEL'da yapılır.

Microsoft referansları:

- [Virtual HID Framework (VHF)](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
- [Touchscreen required HID top-level collections](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchscreen-required-hid-top-level-collections)
- [HID over I2C guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/hid-over-i2c-guide)

### 18.4 Polling ve yaşam döngüsü

Donanım interrupt yolu henüz Windows'a taşınmadığından sürücü PASSIVE_LEVEL
one-shot KMDF timer ile event kuyruğunu poll eder. Callback bitince timer
yeniden kurulur. Periyodik passive timer ile yaşanan yaşam döngüsü sorunu
`a78cd83` değişikliğinde bu yapıya çevrilerek giderildi.

UEFI güç/pin/reset hazırlığını yapar; Windows sürücüsü regülatörleri tekrar
programlamaz. Böylece çalışan firmware handoff'u bozulmaz ve riskli PMIC
yazmaları kernel sürücüsüne taşınmaz.

### 18.5 Multi-touch HID sözleşmesi

Doğrulanan HID descriptor en çok 10 temas sunar. Her temas için en az şu
kullanımlar korunmalıdır:

- `Contact ID`
- `X` ve `Y`
- `Tip Switch`
- rapor başına `Contact Count`
- feature raporunda `Contact Count Maximum`

Aktif temas boyunca Contact ID kararlı kalır. Parmak bırakıldığında aynı ID
için bir kez `Tip Switch=0` raporu gönderilir; aksi hâlde Windows temasın hâlâ
ekranda olduğunu düşünebilir. Kullanıcı cihazda iki farklı pointer ID ve aynı
anda hareket eden birden fazla temas ile gerçek multi-touch'ı doğruladı.

Tanı uygulaması:

```cmd
X:\Windows\B2LTOUCH\b2lmt.exe
```

`b2lmt.exe`, `WM_POINTER` ve `WM_TOUCH` üzerinden aktif parmak sayısını,
pointer kimliklerini ve X/Y konumlarını canlı gösterir. Yalnız mouse mesajı
gelirse bunu `MOUSE ONLY` olarak belirginleştirir; böylece imlecin hareket
etmesi ile gerçek multi-touch desteği karıştırılmaz.

## 19. ARM64 WDK/VHF build sorunları ve kalıcı çözüm

### 19.1 WDK sürümü seçimi

Makinede şu bileşenler bulundu:

```text
Include\10.0.26100.0\shared\vhf.h
Include\10.0.28000.0\shared\vhf.h
Lib\10.0.28000.0\km\arm64\VhfKm.lib
Lib\10.0.28000.0\km\arm64\ntoskrnl.lib
```

Önemli iki sonuç:

1. `vhf.h`, `km` değil WDK `shared` include dizinindedir.
2. ARM64 `VhfKm.lib` yalnız 10.0.28000.0 kitinde bulunduğu için header ve lib
   aynı sürümden seçilmelidir.

Bu yüzden build scripti gerekirse açıkça şununla çağrılır:

```powershell
-WdkVersion "10.0.28000.0"
```

### 19.2 VhfKm.lib unresolved external hataları

İlk link şu sembollerde durdu:

```text
_InterlockedCompareExchange
_InterlockedDecrement
_InterlockedExchange
__security_pop_cookie
__security_push_cookie
```

Sorun VHF API kullanımı değil, ARM64 kernel runtime kütüphanelerinin link
sırasında eksik olmasıydı. Çözüm kullanıcı-modu CRT eklemek değil;
`ntoskrnl.lib`, `hal.lib`, `VhfKm.lib` yanında WDK kernel runtime
`libcntpr.lib` ve `BufferOverflowFastFailK.lib` kütüphanelerini bağlamaktır.
KMDF 1.15/WdfLdr girişi de build sözleşmesinin parçasıdır.

### 19.3 WSL ve Windows araç zincirini ayırma

WSL repo UNC yolu `cmd.exe` veya bazı Visual Studio araçları için çalışma
dizini olamaz. PowerShell build scripti kaynakları Windows `%TEMP%` altında
bir staging dizinine taşır, `vcvarsamd64_arm64.bat` ortamını açar ve çıktıları
UNC yoluyla `out/b2ltouch` dizinine geri koyar.

Rol ayrımı:

| İş | Araç ortamı |
|---|---|
| ARM64 C derleme/link | Windows MSVC + WDK |
| INF/CAT doğrulama | Windows Inf2Cat |
| CAT imzalama/doğrulama | Windows SignTool ve WSL OpenSSL araçları |
| WIM değiştirme | WSL `wimlib-imagex` |
| FAT/sparse/TAR | WSL Linux araçları |

WSL burada Windows kernel driver compiler'ı değildir; yalnız orkestrasyon ve
paketleme katmanıdır.

## 20. WinPE PnP ve VHF hata ayıklama düzeni

### 20.1 Code 31 tek başına kök neden değildir

İlk Windows denemelerinde aygıt:

```text
Problem Code: 31 (CM_PROB_FAILED_ADD)
Problem Status: 0xC00000BB
```

gösterdi. Bu durum AddDevice callback hatası, eksik bağımlı servis veya daha
sonraki start aşamasındaki hata olabilir. Bu yüzden sürücü aşağıdaki aşamaları
registry'ye kalıcı olarak yazar:

| Değer | Aşama |
|---:|---|
| `0x01` | driver entry/add başlangıcı |
| `0x02` | WDF device create |
| `0x10` | device context hazır |
| `0x20` | timer hazır |
| `0x30` | `VhfCreate` |
| `0x40` | `VhfStart` |
| `0x50` | ACPI kaynakları doğrulandı |
| `0x60`–`0x64` | sec_ts başlatma alt aşamaları |
| `0x70` | `D0Ready`, çalışma hazır |

Değerler:

```text
HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch\B2LTouchStage
HKLM\SYSTEM\CurrentControlSet\Services\b2ltouch\B2LTouchStatus
```

Son doğrulamada aygıt `Started`, stage `0x70`, status `0x0` oldu.

Microsoft Code 31 kaynağı:
[Device Manager problem codes](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/device-manager-problem-codes).

### 20.2 Bare WinPE'de VHF servis kaydı

WinPE imajında `vhf.sys` dosyasının bulunması servis kaydının da var olduğu
anlamına gelmedi. Üstelik minimal imajda `sc.exe` yoktu. Şu hata önemli bir
ipuçuydu:

```text
'sc.exe' is not recognized as an internal or external command
```

Çözüm `sc.exe` kopyalamak değil, `reg.exe` ile yalnız RAM'deki WinPE oturumu
için `HKLM\SYSTEM\CurrentControlSet\Services\vhf` servis anahtarını doğru
ImagePath/Type/Start/Group değerleriyle oluşturmak oldu. INF'deki
`LowerFilters=vhf` ile birlikte inbox `vhf.sys` yüklenebildi. Kurulu tam
Windows'ta mevcut VHF kaydı zorla ezilmemelidir.

### 20.3 Tanı çıktısında birlikte kaydedilecek veriler

Her cihaz testinde tek fotoğraf veya log içinde şunlar bulunmalıdır:

- PnP instance ID, driver name, status, Problem Code ve Problem Status,
- `vhf` servis anahtarı ve kaynak aygıt `LowerFilters`,
- `B2LTouchStage` ve `B2LTouchStatus`,
- paket version/hash manifesti,
- UFS PnP sonucu,
- CACHE yaz/geri-oku sonucu,
- gerekirse `b2lmt.exe` temas sayısı/ID/koordinat ekranı.

Bu kayıt, “sürücü yüklendi” ile “donanım gerçekten çalışıyor” ayrımını korur.

## 21. WIM, imza ve flash bütünlüğü

### 21.1 Her enjeksiyonda geri okuma

WIM güncellemesinde yalnız `wimlib-imagex update` başarı koduna güvenilmez:

1. Eski hedef dosya WIM'den silinir.
2. Yeni dosya eklenir.
3. Dosya WIM'den geçici dizine geri çıkarılır.
4. Kaynak ve geri okunan dosyanın SHA-256 değeri karşılaştırılır.

Eski WIM'de kalan SYS/INF, cihazda bir önceki sürümün çalışmasına ve doğru kod
değişikliğinin yanlış yorumlanmasına yol açmıştır. Manifest; package adı,
driver version ve INF/SYS/CAT/EXE hash'lerini taşır. `cache_esp.sh` bu
manifesti ve dosya eşleşmelerini son flash paketi öncesinde kapı olarak
denetler.

### 21.2 wimlib tuzakları

- `wimlib-imagex dir`, tek dosya varlığını sınamak için güvenilir değildir;
  gerçek doğrulama `extract` ve hash ile yapılır.
- Windows WIM yolları case-insensitive olsa da Unix'teki wimlib çağrıları
  büyük/küçük harfe duyarlı davranabilir. Windows kaynak aramasında
  `WIMLIB_IMAGEX_IGNORE_CASE=1` kullanılmalıdır.
- Bash `set -u` altında şu kalıp hatalıdır:

  ```bash
  local Name="$1" Output="$Directory/$Name"
  ```

  Aynı `local` deyiminde `Name` henüz tanımlı olmayabilir. Güvenli biçim:

  ```bash
  local Name Output
  Name="$1"
  Output="$Directory/$Name"
  ```

### 21.3 Test imzası

Catalog SignTool ile test sertifikası kullanılarak imzalanır. Build bilgisayarı
test köküne güvenmiyorsa “zincir güvenilmiyor” uyarısı tek başına bozuk imza
anlamına gelmez; CAT imzası, SYS katalog eşleşmesi ve hedef WinPE'nin test
signing durumu ayrı ayrı doğrulanır. Manifest hash'i Authenticode catalog
imzasının yerine geçmez.

## 22. Hata kataloğu

| Belirti | Kök neden | Kalıcı çözüm | Durum |
|---|---|---|---|
| HSI2C `Not Ready`/timeout | Güç, USI modu, slave adresi veya reset sırası eksik | Stok DT/sürücü sırasına göre UEFI init | Çözüldü |
| Temas anında event count 32–35 | 32 üstünde bütün kuyruk temizleniyordu | 32'lik parçalarla güvenli drain ve tekrar poll | Çözüldü |
| `vhf.h`/ARM64 VhfKm bulunamadı | Header `shared` altında; lib yalnız 28000 kitinde | Aynı 28000 WDK include/lib çiftini seç | Çözüldü |
| `_Interlocked*`, security cookie link hatası | ARM64 kernel runtime libleri eksik | `libcntpr` + `BufferOverflowFastFailK` | Çözüldü |
| Code 31, stage yok | Add/start akışı görünmüyordu | Registry stage/status telemetrisi | Çözüldü |
| `VHF_SERVICE=YOK` | WinPE'de dosya var, servis kaydı yok | `reg.exe` ile geçici `vhf` service + LowerFilter | Çözüldü |
| `sc.exe` bulunamadı | Minimal WinPE bileşeni yok | `sc.exe` bağımlılığı kaldırıldı | Çözüldü |
| Stage `0x20` civarında kalma | Passive periyodik timer yaşam döngüsü | One-shot timer ve callback sonunda rearm | Çözüldü |
| İmleç çalışıyor ama multi-touch belirsiz | Mouse emülasyonu gerçek touch'ı maskeleyebilir | 10-contact HID + `b2lmt.exe` WM_POINTER tanısı | Çözüldü |
| Eski davranış flash sonrası sürüyor | WIM'de eski artefakt veya stale SYS | Sil/ekle/geri-oku/hash kapısı | Çözüldü |
| OSK script `Name: unbound variable` | `set -u` ve aynı satır local genişlemesi | Declaration ve assignment ayırma | Çözüldü |
| OSK kaynakta `ADVAPI32.dll` yok göründü | WIM yolunda harf duyarlılığı | Case-insensitive extract | Çözüldü |
| `Could not start On-Screen Keyboard` | 28000 WinPE ile 26200 OSK runtime uyumsuzluğu veya eksik masaüstü servisleri | Aynı-build kaynakla yeniden inceleme gerekli | **Ertelendi** |

İlgili touch kilometre taşları git geçmişinde `971be69`–`e3bbd8f`, UEFI
sec_ts düzeltmeleri `55a34a4`–`42412d0`, OSK deneyi ise `d447c1d`–`97ed290`
aralığındadır.

## 23. Native Windows OSK deneyi: mevcut sınır

Kullanıcı isteği gereği özel klavye yazılmadı; yalnız Microsoft'un native
`osk.exe` uygulaması denendi. Komut:

```bash
B2L_WINDOWS_ARM64_SOURCE="/mnt/c/Users/orhun/Downloads/Windows11_25H2_ARM64.iso" \
  bash tools_winpe_native_osk.sh
```

`tools_b2ltouch.sh`, `B2L_SKIP_NATIVE_OSK=1` verilmediğinde bu adımı da
çağırabilir. Fakat temiz WIM/paketleme akışında OSK manifest kapıları olduğu
için bu değişkenin etkisi build scriptleri okunmadan varsayılmamalıdır.

Deneyde:

- hedef ADK WinPE: ARM64 `10.0.28000`,
- kaynak Windows/OSK: ARM64 en-US `10.0.26200`,
- `osk.exe`, MUI, recursive eksik PE importları,
- doğrudan eksik bulunan `MSCTF.dll` ve `OSKSUPPORT.dll`,
- TSF `CLSID_TF_ThreadMgr` COM kaydı

WIM'e eklendi ve hash/geri-okuma doğrulaması geçti. Yeni kaynaktan eski hedefe
core DLL karıştırılmasına izin verilmedi; yalnız eski OSK'nin yeni hedefte
eksik importlarını tamamlama denendi. Loader/import kapanışı ve TSF kayıtları
geçmesine rağmen OSK kendi `Could not start On-Screen Keyboard` mesajıyla
kapanıyor.

Sonuç: **OSK çalışıyor kabul edilmemelidir.** Touch ve multi-touch bundan
bağımsız olarak tamamen çalışmaktadır. İleride yeniden ele alınırsa ilk temiz
deney, OSK/runtime ile WinPE'yi aynı Windows build'inden üretmek olmalıdır.
Rastgele system DLL'lerini 28000 WinPE üzerine ezmek güvenli bir çözüm değildir.

## 24. Yeni Windows ARM64 sürücüsü için kontrol listesi

### 24.1 Araştırma ve güvenlik

- Android DT'den gerçek MMIO, IRQ, clock, regulator, GPIO ve firmware
  bağımlılıklarını çıkar.
- Stok Linux/Android sürücüsündeki init sırasını referans al; register anlamı
  doğrulanmadan yazma yapma.
- İlk UEFI tanısını salt-okuma ile başlat; sonra yalnız doğrulanmış güç/init
  adımlarını ayrı ayrı aç.
- ACPI `_CRS` kaynaklarını tam boyut ve allowlist ile sınırla.
- PMIC, kamera EEPROM'u, flash/NVRAM veya geniş DRAM alanına kör probe yapma.

### 24.2 Windows mimari kararı

- Önce Windows inbox class/transport sürücüsü var mı araştır.
- Donanım standarda uyuyorsa ACPI `_CID` ve `_CRS` ile inbox sürücüyü kullan.
- Donanım standarda uymuyorsa üretici transport'unu KMDF içinde tutup mümkünse
  VHF, NetAdapterCx, SPB, SensorCx gibi uygun Microsoft class extension'a bağla.
- PnP, power, interrupt/timer ve donanım protokolü katmanlarını ayrı dosyalara
  böl.
- İlk günden registry stage/status ve küçük bir kullanıcı-modu tanı aracı ekle.

### 24.3 Build ve cihaz turu

1. Host parser/protokol testlerini çalıştır.
2. ARM64 WDK build ve link'i tamamla.
3. INF/CAT/sign doğrulamasını yap.
4. WIM'e sil/ekle/geri-oku/hash yöntemiyle enjekte et.
5. Manifest hash'leri ile final sparse/TAR paketini kapat.
6. Tek Odin AP, Re-Partition kapalı flash yap.
7. PnP + stage/status + gerçek işlev testini aynı turda kaydet.
8. Çalışan ilk sürümü ayrı baseline commit olarak işaretle.

### 24.4 Host doğrulama komutları

```bash
git diff --check
bash -n tools_b2ltouch.sh tools_winpe_native_osk.sh cache_esp.sh
bash tests/test_windows_touch_safety.sh
cc -std=c11 -Wall -Wextra -Werror \
  tests/test_windows_touch_parser.c \
  windows/b2ltouch/b2ltouch_parser.c \
  -o /tmp/b2ltouch-parser-test
/tmp/b2ltouch-parser-test
```

Test geçmesi cihaz doğrulamasının yerine geçmez; MMIO, PnP, HID report ve
gerçek çoklu temas yalnız donanım turunda kesinleşir.

## 25. Sıradaki özellik seçimi

### 25.1 Adayların değerlendirmesi

| Özellik | Kullanıcı değeri | Zorluk/risk | Not |
|---|---|---|---|
| USB host/xHCI | Çok yüksek | Orta-yüksek | Klavye, depolama, log alma ve USB ağ adaptörü açabilir |
| Güç/ses tuşları | Orta | Düşük-orta | Hızlı kazanım; dokunmasız kurtarma kontrolü sağlar |
| Batarya/şarj göstergesi | Yüksek | Yüksek | PMIC yazma riski; salt-okuma tanısıyla başlanmalı |
| Ses | Yüksek | Çok yüksek | Codec, DSP, firmware ve saat zinciri karmaşık |
| Wi-Fi/Bluetooth | Yüksek | Çok yüksek | Firmware, bus ve Windows vendor driver bağımlılığı |
| Kamera | Orta | Çok yüksek | Sensör/ISP/firmware ve güvenli güç sırası gerekir |
| Native OSK | Orta | Orta-yüksek | Touch'ı engellemiyor; aynı-build runtime bekliyor |

### 25.2 Öneri: USB host/xHCI

Sıradaki ana hedef **USB host desteği** olmalıdır. Nedeni yalnız bir çevre
birimi açması değil, sonraki bütün geliştirmelerin hızını artırmasıdır:

- fiziksel klavye OSK bağımlılığını kaldırır,
- USB bellek ile log/driver taşımayı kolaylaştırır,
- USB Ethernet daha sonra ağ tanısını mümkün kılar,
- standart xHCI ise Windows inbox `USBXHCI.sys` kullanılabilir ve yeni bir USB
  host sürücüsü yazmak gerekmez.

Repo DT'sinde ilk aday DWC3 host bloğu `usb@10C00000`/`0x10C00000` olarak
görülür. PHY alanları ve interrupt dönüşümü stok DT ile Exynos sürücüsünden
kesinleştirilmeden ACPI'ye yazılmamalıdır. Önerilen güvenli sıra:

1. UEFI'de DWC3/PHY/clock/power durumunu yalnız okuyup raporla.
2. Stok DT ve Samsung DWC3/PHY sürücüsünden doğrulanmış init/handoff sırasını
   çıkar.
3. ACPI'de vendor `_HID`, debug yeteneği olmayan standart xHCI için önce
   `_CID=PNP0D15` ve dar `_CRS` kaynaklarını ilan et. `PNP0D10`, standart debug
   yolu gerçekten DBG2/KDNET ile doğrulandıktan sonra değerlendirilmelidir.
4. WinPE'de inbox xHCI sürücüsünün bağlanıp bağlanmadığını PnP stage ve USB
   aygıt ağacıyla ölç.
5. Önce düşük riskli USB klavye, sonra USB depolama ile gerçek işlev testi yap.

Microsoft'un ACPI bring-up dokümanı SoC üzerindeki xHCI denetleyicisinin ACPI
ile tanıtılıp inbox sürücüye bağlanmasını açıklar:
[Other ACPI namespace objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/other-acpi-namespace-objects).
Microsoft'un aygıt kimliği tablosu `PNP0D10` ile standart debug bulunan,
`PNP0D15` ile standart debug bulunmayan xHCI denetleyicisini ayırır:
[Device management namespace objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/device-management-namespace-objects).

USB üzerinde donanımsal bir engel çıkarsa paralel ana iş açmadan önce hızlı
kazanım olarak ACPI button array ile ses/power tuşları ele alınabilir. Batarya,
ses, Wi-Fi ve kamera ise USB ve temel güç yönetimi gözlemlenebilir hâle
geldikten sonraya bırakılmalıdır.
