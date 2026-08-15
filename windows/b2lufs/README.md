# B2LUFS — Exynos 9820 Windows UFS miniportu

Bu klasör `storufs.sys` filtresi değildir. Hedef, `ACPI\\SMSN9820\\0`
aygıtına bağlanan bir **fiziksel StorPort miniportu**dur.

## Faz 1: salt-okunur LUN 0 / GPT keşfi

`b2lufs_resources.c`, UEFI `PlatformUfsLib` ile stok Exynos sürücüsünün
kullandığı altı MMIO penceresini bir StorPort miniportunun kabul edeceği
şekilde tanımlar:

| Kaynak | Taban | Minimum boyut |
|---|---:|---:|
| UFSHCI | `0x13D60000` | `0x1000` |
| Samsung VS-HCI | `0x13D61100` | `0x200` |
| UniPro | `0x13D50000` | `0x8000` |
| PHY PMA | `0x13D64000` | `0x800` |
| IO coherency | `0x13C10700` | `0x4` |
| PMU UFS PHY | `0x15860724` | `0x4` |

Kaynaklar dizi sırasına güvenilmeden tabana göre eşleştirilir. Eksik,
yinelenen, memory olmayan veya kısa bir pencere güvenli biçimde reddedilir.

Faz 1 bu PnP sözleşmesini korur ve yalnız aşağıdaki dar taşıma akışını ekler:

1. StorPort'tan bir adet **uncached/bitişik** DMA alanı alır; 1 KiB hizalı
   tek UTRD ve onun 1 KiB hizalı UCD'sini burada kurar. Her istekte UTRD,
   Linux'un yaptığı gibi `UFS_STORAGE` komut türünü taşır.
2. Firmware'in WinPE'yi yüklemek için açık bıraktığı HCE/link durumunu ve
   `HCS` hazır bitlerini doğrular. HCE sıfırsa sürücü onu açmaz; UIC/DME,
   PHY/CAL veya software reset göndermez.
3. Denetleyicinin boş transfer-list scheduler'ını kendi DMA halkasına
   yöneltir. Exynos Linux sürücüsünün transfer-özel ayarlarını (4 KiB PRDT,
   `DATA_REORDER=0xa`, UTR/UTMR nexus maskeleri, AXI burst) uygular ve
   firmware'den kalan donanım AHIT zamanlayıcısını kapatır. Link/power-mode
   yeniden kurulmaz.
4. PnP taramasından hemen önce, bitişik DMA tamponunda önce standart
   `READ CAPACITY(10)` cevabını doğrular; yalnız `0xFFFFFFFF` LBA yanıtı
   verirse `READ CAPACITY(16)`ye geçer. Bu yalnız tanı ön-kontrolüdür:
   Windows'un her sonraki `READ CAPACITY` SRB'si de doğrudan UFS'e gider;
   kalan-bayt sayısı Response UPIU'dan alınarak StorPort'a doğru bildirilir.
5. Aynı LUN 0 üzerinden bir kez standart `REPORT LUNS` envanterini alır.
   Bu, diğer LUN'ların bloklarını okumaz, hiçbir LUN'u Windows'a eklemez ve
   boot/RPMB LUN'larını StorPort/PnP taramasından uzak tutar. Yanıt, yalnız
   WinPE'nin RAM-backed kayıt defterinde `B2LUFS` tanı alt anahtarına yazılır.
6. `INQUIRY`, `READ CAPACITY`, `MODE SENSE` ve `READ` sonucunda Windows
   GPT'yi ve Android bölümlerini okuyabilir. Inbox `storufs` ile aynı disk
   yaşam-döngüsü yüzeyi için `START STOP UNIT`, donanım güç/medya durumunu
   değiştirmeyen yerel bir no-op olarak başarıyla tamamlanır ve HCI'ye
   gönderilmez. Her `DATA_OUT`, WRITE, FORMAT, UNMAP, MODE SELECT,
   SECURITY OUT ve benzeri mutasyon CDB'si doğrudan
   `SRB_STATUS_INVALID_REQUEST` ile reddedilir ve HCI'ye ulaşmaz. Boot LUN, RPMB ve
   diğer bütün LUN'lar `NO_DEVICE` olarak gizlenir.

Bu nedenle DiskPart'ta görünür olan disk, var olan Android kullanıcı-LUN'ıdır
ve get-up aşamasında salt-okunurdur. Sürücünün yazdığı tek yer HCI/VS-HCI
çalışma yazmaçları ile StorPort'un verdiği RAM DMA halkasıdır; UFS medyasına
yazma yolu yoktur.

## Mevcut WinPE turu: sahiplenmeden önce HCI anlık görüntüsü

`b2lfb.sys`, b2lufs sürücüsü değildir ve UFS'ye bağlanmaz. Ancak sonraki
miniport başlatmasının doğru yerde kurulabilmesi için, Windows'un en erken
boot-driver aşamasında HCI'nin gerçekten hangi durumda olduğunu kalıcı
diagnostik kayda alır. Ölçüm `CAP`, `VER`, `HCE` ve Samsung
`1US_TO_CNT` yazmaçlarını **yalnız okur**. Eşleme aynı `DriverEntry`
çağrısında geri bırakılır; PnP/storufs ile eşzamanlı veya kalıcı ikinci bir
MMIO eşlemesi yoktur.

Yazılımsal reset bu platformda WinPE'den dönemediği için, aynı kayıt
`b2lcmd` görüldükten sonra WinPE RAM diskine de aktarılır:

```
%SystemRoot%\B2LUFS.TXT
```

`startnet.cmd` bunu otomatik tanının sonunda ekrana basar. Bu ikinci kanal
yalnız CMD'den fotoğraflanabilirlik içindir: dosya `X:` RAM diskindedir ve
UFS LUN'una yazı yapılmaz. Sıcak reset çalıştığında B2LGoster yine şu iki
satırı da gösterir:

```
UFS C=xxxxxxxx V=xxxxxxxx
UFS H=xxxxxxxx T=xxxxxxxx
```

`T` değerinin alt 10 biti UEFI'nin beklediği `166` değerini göstermelidir.
Bu kayıt HCE'yi değiştirmez, UIC/DME komutu göndermez, kesme durumunu
okumaz/temizlemez ve hiçbir UFS LUN'una erişmez.

Başka bir S9+ Windows get-up ekranında görülen “one-shot NOP” yaklaşımı,
bizim için bir uygulama tarifi değil bir **önkoşul listesi**dir. Kaynak kodu
ve donanım revizyonu bilinmediği için onun register yazma sırasını kopyalamak
güvenli değildir. Buna rağmen doğrulanacak noktalar doğru sıralamayı verir:

1. `CAP` içinden NUTRS/NTMRS ve adresleme kapasitesi; UTRD/UTMR ring boyutu
   tahmin edilmez.
2. `HCE` ve UFS owner/link durumu; firmware hâlâ sahibiyse denetleyici
   resetlenmez veya yeniden başlatılmaz.
3. StorPort'un verdiği fiziksel DMA belleğinin düşük-4-GB, bitişik ve
   sıfırlanmış olup olmadığı; bu varsayım yalnız `CAP` doğrulandıktan sonra
   kullanılabilir.
4. Bunlar kayda geçtikten sonra, **tek** yazmayan UTP NOP Out komutu ve
   yalnız tamamlanma kodunun okunması. UIC/DME/power-mode veya LUN I/O'su
   bu adımdan önce gelmez.

Faz 1, NOP yerine gerçek ancak salt-okunur SCSI taşımasını kullanır; dolayısıyla
Windows'un kendi standart SCSI keşfi fiziksel UFS kullanıcı-LUN'ını tarar.

## WinPE'de ham okuma izi

`HwStartIo` çağrısı içinde konsola veya dosya sistemine doğrudan yazmak güvenli
değildir. Bunun yerine miniport, ilk on iki SCSI SRB için StorPort Operational
ETW kanalına bir `BLS1` ikili tanı kaydı bırakır. `startnet.cmd`, ikinci
DiskPart taramasından hemen sonra bu kayıtları CMD'de gösterir. Kaydın son 64
baytı, başarılı bir `INQUIRY`, kapasite veya `READ` isteğinden dönen ham
örnektir. UFS bir `CHECK CONDITION` döndürürse aynı alan medya yerine UFS
RESPONSE UPIU'dan kopyalanan sense baytlarını taşır ve SRB
`SRB_STATUS_AUTOSENSE_VALID` ile tamamlanır. Bu, `AutoRequestSense=TRUE`
ilanını gerçek davranışla eşler; desteklenmeyen isteğe bağlı bir INQUIRY VPD
sayfası artık taşıma arızası gibi görünmez. Ayrı `BLP1` kayıtları, legacy `SCSI_PNP_REQUEST_BLOCK` üzerinden
gelen BTL8 PnP/LUN yaşam döngüsü isteğini gösterir; CDB veya UFS medya verisi
içermez. Bu kayıtların tümü yalnız WinPE olay günlüğüne/RAM'e yazılır; UFS
LUN'larına yazı yapılmaz.

Bir UFS komutu 50 ms içinde kapı zilini bırakmazsa miniport UFS reseti veya
iptal yazısı göndermez. İlk SRB `TIMEOUT` ile tamamlanır, yeni komutlar hızlı
biçimde `NO_DEVICE` döner ve WinPE/PnP akışı ekranda takılı kalmaz. Bu koruma,
ham okunabilirlik tanısı için önemlidir; UFS medya yazma yolu eklemez.

`startnet.cmd` ayrıca `REPORT LUNS` anlık görüntüsünü `Device Parameters\B2LUFS`
altından basar. `ReportLunsRaw` değeri standart yanıtın tamamıdır: ilk dört
bayt big-endian liste uzunluğudur, sekizinci bayttan sonra her sekiz bayt bir
LUN adresidir. Aynı kayıt altında görünen `Lun00Id`, `Lun01Id`, ... değerleri
bu adreslerin etiketlenmiş kopyalarıdır. Bu liste yalnız donanımın bildirdiği
envanterdir; B2LUFS yine sadece kullanıcı **LUN 0** için I/O kabul eder.

`b2lufs_storport.c`, gerçek WDK `HwFindAdapter` çağrısını bu katmana bağlar.
`DriverEntry` ve `HwFindAdapter` birlikte klasik
`SCSI_REQUEST_BLOCK` + `BTL8` adresleme sözleşmesini açıkça ilan eder:
`SrbTypeFlags`/`SrbType` eski SRB biçimini, `AddressTypeFlags`/`AddressType`
ise `Path=0, Target=0, Lun=0` BTL8 yolunu seçer. Bu ikisi, `HwStartIo`nun
okuduğu alanlarla aynıdır ve StorPort'un LUN 0 için disk PDO'su
numaralandırabilmesinin önkoşuludur. Fiziksel StorPort miniportlarının
zorunlu `TaggedQueuing` ve `MultipleRequestPerLu` yetenekleri açık tutulur.
`NumberOfPhysicalBreaks=32`, 32 girişli PRDT'nin tamamını StorPort'a bildirir.
Tek aktif UTRD'yi doğru yansıtmak için `MaxNumberOfIO`, `MaxIOsPerLun` ve
başlangıç kuyruk derinliği 1'dir; bunlar zorunlu tagged/multiple-request
yetenek bayraklarını kapatmak için kullanılmaz.
Miniport tam çift yönlü (`StorSynchronizeFullDuplex`) çalışır; StorPort'un
StartIo kilidi altında her komut `HwStartIo` dönmeden eşzamanlı tamamlandığı
için donanımda yalnız UTRD slot 0 etkin olabilir. Legacy `QueueTag`, UFS
COMMAND UPIU'ya taşınır ve RESPONSE UPIU'da doğrulanır. Microsoft'un fiziksel
StorPort örneklerindeki PnP yüzeyi de buna eklenmiştir: eşzamansız ve kilitsiz
`HwBuildIo` aygıt durumunu değiştirmez; legacy `SRB_FUNCTION_PNP` istekleri
kilitli `HwStartIo` yolunda tamamlanır. `HwUnitControl`, yalnız
statik `Path=0, Target=0, Lun=0` için unit-start/remove/surprise-removal
yaşam döngüsünü ilan eder. Ayrıca Microsoft'un iki parçalı bus-türü
sözleşmesini uygular: `HwFindAdapter` başında
`StorPortSetFeatureList(StorportFeatureBusTypeUnitControl)` çağrılır,
`ScsiUnitQueryBusType` destek listesine eklenir ve birim sorgusuna
`BusTypeUfs` döndürülür. Bu bildirim olmadan StorPort aygıtı varsayılan
`FIBRE` olarak etiketler. `StorQueryCapabilities` için tam ve salt-okunur
`STOR_DEVICE_CAPABILITIES_EX` cevabı verilir. Böylece StorPort disk PDO'sunu
normal PnP sözleşmesi ile kurar; PnP isteği UFS'e komut göndermez.
Her `ACCESS_RANGE`, yalnız StorPort'un verdiği kaynak üzerinden
`StorPortGetDeviceBase` ile haritalanır; `RangeInMemory` değeri API'nin
`InIoSpace` parametresi için terslenir. Kısmi/uyuşmayan kaynak dizisi hemen
geri bırakılır. `HwInitialize`, canlı HCE ve boş UTRL scheduler'ını
doğrulayıp tek DMA slotunu bağlar; uzun sürebilen salt-okunur kapasite/LUN
probları `PASSIVE_LEVEL` callback'ine bırakılır. Microsoft'un
[StorPort yaşam döngüsü](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/life-cycle-of-a-storport-driver)
`HwFindAdapter -> HwInitialize -> HwAdapterControl(QuerySupported) -> I/O`
sırasındadır. Bu nedenle başlangıç callback'leri içinden doğrudan bus taraması
istenmez. `HwFindAdapter`, sonucu denetlenebilen açık bir StorPort timer
nesnesini `StorPortInitializeTimer` ile oluşturur. PASSIVE probundan sonra
`StorPortRequestTimer` bunu 500 ms'ye kurar; timer çalışınca bus 0 için tek
`BusChangeDetected` gönderilir. Operational kanaldaki `BLT1`
kaydı timer'ın gerçekten çalıştığını, içindeki `TransportReady=1` ise UFS
taşımasının tarama öncesinde hazır olduğunu gösterir. Bildirim PDO oluşturma
sözleşmesinin yerine geçmez. StorPort, ilan edilmiş
BTL8 yolunda `HwBuildIo`/`HwUnitControl` sözleşmesi üzerinden LUN 0 için
standart PnP ve ardından SCSI keşfini yürütür. Timer'ın oluşturma ve planlama
durumları `BusScanTimerInitializeStatus`/`BusScanTimerRequestStatus` olarak
tanıya yazılır; PnP remove sırasında bekleyen istek iptal edilip nesne
`StorPortFreeTimer` ile bırakılır. StorAHCI örneğiyle aynı şekilde dump
stack'te timer/PASSIVE tanı yolu kurulmaz. Zorunlu adapter-control çağrıları
Microsoft'un istediği gibi daima `ScsiAdapterControlSuccess` döndürür; gerçek
taşıma durumu ayrıca `TransportReady` ile korunur.

`ScatterGather`, `Master`, `NeedPhysicalAddresses`, `TaggedQueuing`,
`AutoRequestSense` ve `MultipleRequestPerLu` alanları StorPort tarafından
hazırlanır; güncel DDI'nin “miniport must not modify” kuralına göre
`PORT_CONFIGURATION_INFORMATION` içinde yeniden yazılmaz. Eski
`ResetTargetSupported` alanı da Microsoft tarafından obsolete ilan edildiği
için kullanılmaz; çok katmanlı reset desteği gerçek reset SRB'leri ve
`HwResetBus` callback'iyle sağlanır.
Normal SRB veri yolu `Srb->DataBuffer` sanal eşlemesine bağımlı değildir;
PRDT doğrudan `StorPortGetScatterGatherList` tarafından verilen, SRB
tamamlanana kadar geçerli fiziksel listeden kurulur. Böylece Microsoft'un
belirttiği düşük bellek durumunda `DataBuffer == NULL` olsa da disk okuması
fiziksel SG listesi üzerinden ilerleyebilir.
Ön taramadaki gerçek UFS `READ CAPACITY` cevabı doğrulandıktan sonra disk
sınıfının aynı kapasite sorgusu bu değişmez anlık görüntüden tamamlanır.
Yanıt hedefi olarak `Srb->DataBuffer` doğrudan dereference edilmez;
Microsoft'un StorPort veri SRB'leri için sağladığı `StorPortGetSystemAddress`
ile sistem adresi alınır. Bu yol kapasite uydurmaz: `last-LBA` ve 4096 bayt
blok değeri doğrudan aynı açılışta UFS'den gelen doğrulanmış cevaptır.
Kesme sahipliği alınmaz; tamamlanma kapı zili poll edilir. Miniport fiziksel
bir ACPI HBA olduğu için `Internal` arayüz türünü
kullanmaz: `InterfaceTypeUndefined` ile
`STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE` StorPort'a PnP'nin gerçek platform
arayüzünü seçtirtir. Bu ayrım önemlidir; Microsoft `Internal`ı sanal
miniportun işareti olarak tanımlar. Kaynak sayısı, MMIO eşlemesi veya kaynak
sözleşmesi reddedilirse sürücü `Microsoft-Windows-Storage-Storport/Operational`
kanalına `B2LUFS HwFindAdapter resource contract failure` ETW kaydı bırakır.
`startnet.cmd` yalnız bu kayıtları otomatik ekrana basar. Faz 1'de PnP
kurulumundan sonraki ikinci DiskPart betiği `list disk`, `detail disk` ve
`list partition` çalıştırır; bunların tümü salt-okunurdur.

## Faz 1 WinPE kabul ölçütü

`startnet.cmd` önce sürücü kurulmadan önceki DiskPart sonucunu, ardından
`pnputil /add-driver`, yeniden tarama ve **kurulum sonrası** ikinci DiskPart
sonucunu gösterir. Fotoğrafta şu üç kanıt birlikte aranır:

1. `B2LUFS Faz 1: salt-okunur LUN 0 miniportunu PnP'ye ekle` altında INF'nin eklendiği ve
   yeniden taramanın tamamlandığı görülür.
2. `UFS0: CID, eslesen surucu ve stack` ile `SCSIAdapter` çıktılarında
   `ACPI\SMSN9820\0` için `Beyond2LTE Exynos 9820 UFS Host Controller`,
   `b2lufs.inf` ve `Started` yer alır. INF'nin eşleşmesi `ACPI\BYND9820`
   compatible ID'si üzerinden olur; instance ID'nin `SMSN9820` kalması
   normaldir.
3. İkinci DiskPart bloğu `Disk 0`ı göstermeli, ardından `detail disk` ile
   `list partition` Android kullanıcı-LUN'ındaki GPT bölümlerini listelemelidir.
   `X:` yalnız WinPE RAM diskidir.

StorPort Operational çıktısında önce `B2LUFS deferred bus scan timer fired`
ve `BLT1`, ardından normal `BLS1` INQUIRY/READ CAPACITY kayıtları beklenir.
`BLT1` var fakat `BLS1` yoksa timer/adapter yaşam döngüsü tamamlanmış olmasına
rağmen StorPort tarama üretmemiştir; `BLT1` de yoksa sorun timer'ın
planlanmasından önceki miniport başlatma aşamasındadır.

`Code 10`/`FAILED_START`, HCE/link durumunun firmware'den canlı devralınamadığını
veya transfer-list scheduler'ın boş olmadığını gösterir. İmza veya `pnputil`
hatası ise sürücünün hiç çalıştırılmadığını gösterir. Her iki durumda da
sürücü UFS LUN'una komut göndermeden başarısız olur.

Bu testte DSDT `UFS0` aygıtı yalnız `ACPI\\BYND9820` compatible ID'sini
yayınlar ve altı MMIO penceresini yalnız b2lufs'a verir. Böylece inbox
`storufs` ile aynı HBA'ya iki sürücünün bağlanması mümkün değildir. WinPE
`startnet.cmd`, paket eklendiyse `pnputil /add-driver ... /install` çağrısını
otomatik yapar; fotoğrafta hedef PnP durumu `b2lufs.inf` + `Started`dır.

`tools_b2lufs.sh`, WSL'den Windows WDK'nin ARM64 `storport.lib` ve
`ntoskrnl.lib` kitaplıklarını kullanır. Kaynaklar WSL clang'iyle değil,
`build-b2lufs.ps1` üzerinden Visual Studio'nun `vcvarsamd64_arm64.bat` ile
kurulan **MSVC ARM64** ortamında `cl.exe`/`link.exe` ile derlenir. Betik beş
kaynak dosyasını önce Windows'un yerel `%TEMP%` dizinine kopyalar: böylece
`\\wsl.localhost\\...` yolu ne `cmd.exe`nin çalışma dizini olur ne de
derleyiciye aktarılır. Bu, `CMD.EXE was started with the above path...` UNC
hatasını kalıcı olarak önler.

`b2lufs.sys` derlemesi yalnız Windows PowerShell/MSVC ortamında çalıştırılır:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

Bu komut yalnız `out\b2lufs\b2lufs.sys` üretir. Ardından WSL'de imza,
katalog ve WIM paketlemesi için:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh
```

WDK ile **aynı build numaralı Windows SDK Headers** da kurulmuş olmalıdır:
`ntdef.h` SDK'nın `shared` başlığıdır; ADK bunu sağlamaz. Örneğin
`10.0.28000.0` WDK seçilmişse beklenen dosya
`Include/10.0.28000.0/shared/ntdef.h` olur. Araç, ayrıca `Inf2Cat.exe` ile
INF+SYS hash'lerini içeren `b2lufs.cat` üretir, onu ve SYS'yi b2lfb'nin var
olan test sertifikasıyla imzalar. PnP yüklemesi için yalnız gömülü SYS
imzasına güvenmek doğru değildir. `CopyFiles` bölümü için INF'de açık bir
`SourceDisksFiles` eşlemesi vardır; bu, Inf2Cat'in SYS dosyasını imzalanacak
paketin parçası olarak doğrulamasını sağlar.

Paketlenmiş WIM'de ayrıca `\Windows\B2LUFS\manifest.txt` bulunur. Bu dosya
INF/SYS/CAT SHA-256 değerlerini taşır. `cache_esp.sh --win --system`,
`out/b2lufs/b2lufs.sys` bulunduğunda bu manifestteki SYS hash'ini WIM'i geri
okuyarak doğrular; uyuşmazsa sparse `system.img` üretmez. WinPE'de
`startnet.cmd` manifestin içeriğini PnP kurulumundan hemen önce gösterir.
Dolayısıyla tekrar `B2LUFS paketi yok` görülürse çalışan ESP, bu doğrulama
kapısından geçmiş son artefakt değildir; UFS donanım sonucu olarak yorumlanmaz.

Ölçülen ilk HCI durumu: `CAP=1303FF1F`, `VER=00000300`, `HCE=1`,
`1US_TO_CNT=000000A6`. CAP alanında NUTRS `[4:0]` ve NUTMRS `[18:16]`
sırasıyla `31` ve `3` olduğundan denetleyici 32 UTR + 4 UTMR kapasitelidir;
bit 24 de 64-bit adreslemeyi bildirir. Faz 1 bu kapasitenin yalnız bir UTR
yuvasını kullanır; StorPort bu yüzden tek SRB'ye sınırlandırılır. Sürücü,
fiziksel sayfa sınırını geçebilen bir isteği en çok 32 adet 4 KiB PRDT girişine
böler; bitişik fiziksel RAM varsaymaz ve tek okuma için üst sınır 128 KiB'dir.

Yerel doğrulama:

```bash
clang -std=c11 -Wall -Wextra -Werror \
  windows/b2lufs/b2lufs_resources.c windows/b2lufs/test_resources.c \
  -o /tmp/b2lufs-resources-test && /tmp/b2lufs-resources-test
```

## Sonraki iki değişiklik

1. İlk salt-okunur GPT/LUN 0 keşfini DiskPart fotoğrafıyla doğrula.
2. UEFI'den Exynos başlatması ancak HCE/link devralması kesin olarak
   başarısız olursa ele alınır: VCC/PHY isolation, IO coherency,
   `1US_TO_CNT=166`, saat/refclk/clock-gating, CAL pre-link, standart
   `DME_LINKSTARTUP`, CAL post-link. UTRD/UPIU/SCSI bundan sonra gelir.

Bu sıra veri güvenliği içindir: eksik miniport hiçbir UFS yazma isteği kabul etmez.
Test imza zinciri mevcut `windows/b2lfb/b2l-test.pem` üzerinden korunur;
üretim imzası bu get-up aşamasının kapsamı dışındadır.
