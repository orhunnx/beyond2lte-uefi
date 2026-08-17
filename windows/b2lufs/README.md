# B2LUFS — Exynos 9820 Windows UFS miniportu

Bu klasör `storufs.sys` filtresi değildir. Hedef, `ACPI\\SMSN9820\\0`
aygıtına bağlanan bir **fiziksel StorPort miniportu**dur.

Kaynak dosyalarının görevleri, MSVC/WDK bayrakları ve imza–WIM–Odin araç
zinciri için: [`AI_DRIVER_BUILD_REFERENCE.md`](AI_DRIVER_BUILD_REFERENCE.md).

## LUN 0 okuma/yazma ve GPT erişimi

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

B2LUFS bu PnP sözleşmesini korur ve aşağıdaki taşıma akışını uygular:

1. StorPort'tan bir adet **uncached/bitişik** DMA alanı alır; 1 KiB hizalı
   tek UTRD ve onun 1 KiB hizalı UCD'sini burada kurar. Her istekte UTRD,
   Linux'un yaptığı gibi `UFS_STORAGE` komut türünü taşır.
2. Firmware'in WinPE'yi yüklemek için açık bıraktığı HCE/link durumunu ve
   `HCS` hazır bitlerini doğrular. HCE sıfırsa sürücü onu açmaz; UIC/DME,
   PHY/CAL veya software reset göndermez.
3. Denetleyicinin boş transfer-list scheduler'ını kendi DMA halkasına
   yöneltir. Exynos Linux sürücüsünün transfer-özel ayarlarını (4 KiB DMA
   data-unit değeri,
   `DATA_REORDER=0xa`, UTR/UTMR nexus maskeleri, AXI burst) uygular ve
   firmware'den kalan donanım AHIT zamanlayıcısını kapatır. Link/power-mode
   yeniden kurulmaz.
4. PnP taramasından hemen önce, bitişik DMA tamponunda önce standart
   `READ CAPACITY(10)` cevabını doğrular; yalnız `0xFFFFFFFF` LBA yanıtı
   verirse `READ CAPACITY(16)`ye geçer. Bu gerçek UFS cevabı doğrulanıp
   saklanır. Disk sınıfının sonraki kapasite SRB'si Microsoft StorAHCI ile
   aynı yaşam döngüsünde, `HwBuildIo` içindeki mapped `Srb->DataBuffer`
   adresine yazılıp tamamlanır; callback `FALSE` döndürdüğü için yazılımsal
   cevap sahte bir donanım DMA isteği olarak `HwStartIo`ya girmez. Normal
   READ/WRITE tamponları sistem adresine çevrilir; tek-slotlu/senkron miniport,
   Windows tamponu ile 128 KiB'lik StorPort uncached ortak DMA tamponu arasında
   CPU kopyası yapar ve UFS PRDT'sini bu doğrulanmış bitişik alana yöneltir
   (`STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE`). Böylece kapasite probunda
   çalışan ortak-DMA sözleşmesi GPT READ ve CACHE WRITE için de aynıdır.
5. Aynı LUN 0 üzerinden bir kez standart `REPORT LUNS` envanterini alır.
   Bu, diğer LUN'ların bloklarını okumaz, hiçbir LUN'u Windows'a eklemez ve
   boot/RPMB LUN'larını StorPort/PnP taramasından uzak tutar. Yanıt, yalnız
   WinPE'nin RAM-backed kayıt defterinde `B2LUFS` tanı alt anahtarına yazılır.
6. `INQUIRY`, `READ CAPACITY`, `MODE SENSE`, `READ` ve `WRITE` istekleri
   normal UFS SCSI komutlarıdır. Inbox `storufs` ile aynı disk
   yaşam-döngüsü yüzeyi için `START STOP UNIT`, donanım güç/medya durumunu
   değiştirmeyen yerel bir no-op olarak başarıyla tamamlanır ve HCI'ye
   gönderilmez. Açık `DATA_IN`/`DATA_OUT` yönleri doğrudan UTRD/UPIU yönüne
   çevrilir. Legacy `SRB_FLAGS_UNSPECIFIED_DIRECTION` iki yön bitini birlikte
   taşıdığından, miniport yönü CDB'den çözer; bu bayrak artık READ CAPACITY
   veya READ isteğini yanlışlıkla yazma saymaz. Boot LUN, RPMB ve diğer bütün
   LUN'lar `NO_DEVICE` olarak gizlenir.

Bu nedenle DiskPart'ta görünür olan disk, var olan Android kullanıcı-LUN'ıdır.
Sürücü `WRITE(6/10/12/16)`, `UNMAP`, `MODE SELECT`, `WRITE SAME`, güvenlik
OUT ve açık DATA_OUT komutlarını gerçek UFS aktarımına verir. Bu komutlar
mevcut Android verisini değiştirebilir; yalnız test/kurulum komutları ayrıca
salt-okunur tutulmuştur.

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

Dosya gerektiğinde CMD'den elle okunabilir; sadeleştirilmiş `startnet.cmd`
normal disk/GPT akışında bu eski HCI dökümünü basmaz. Dosya `X:` RAM
diskindedir ve UFS LUN'una yazı yapılmaz. Sıcak reset çalıştığında B2LGoster şu iki
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

B2LUFS, NOP yerine gerçek SCSI taşımasını kullanır; Windows'un standart SCSI
keşfi ve normal blok I/O'su fiziksel UFS kullanıcı-LUN'ına gider.

## WinPE'de ham okuma izi

`HwStartIo` çağrısı içinde konsola veya dosya sistemine doğrudan yazmak güvenli
değildir. Bunun yerine miniport, bütün donanım SCSI SRB'leri için StorPort
Operational ETW kanalına bir `BLS2` ikili tanı kaydı bırakır. Kayıt özgün/dönen aktarım
uzunluğunu, SGL/PRDT girdi sayılarını ve ilk fiziksel parçayı da taşır.
`PrdtBuildStatus` alanı `0=denenmedi`, `1=başarılı`, `2=DMA eşlemesi yok`,
`3=PRDT dolu`, `4=SGL kısa`, `5=geçersiz adres/uzunluk` olarak yorumlanır.
Snapshot'tan tamamlanan GPT okumaları ayrıca `BLG1` kaydı üretir. Bu kayıtta
sayaç, başlangıç LBA'sı, blok sayısı, eşleme kaynağı ve kopya tamamlandıktan
sonra StorPort sistem adresinden geri okunan ilk 64 bayt bulunur. Böylece
CPU snapshot içeriği ile partmgr'a teslim edilen son tampon ayrılır.
`startnet.cmd`, Operational günlüğünü `X:\B2LUFS-StorPort.evtx` dosyasına
aktarır ve son kayıtları konsola döker. `BLS2` kaydının son 64
baytı, başarılı bir `INQUIRY`, kapasite veya `READ` isteğinden dönen ham
örnektir. UFS bir `CHECK CONDITION` döndürürse aynı alan medya yerine UFS
RESPONSE UPIU'dan kopyalanan sense baytlarını taşır ve SRB
`SRB_STATUS_AUTOSENSE_VALID` ile tamamlanır. Bu, `AutoRequestSense=TRUE`
ilanını gerçek davranışla eşler; desteklenmeyen isteğe bağlı bir INQUIRY VPD
sayfası artık taşıma arızası gibi görünmez. Ayrı `BLP1` kayıtları, legacy `SCSI_PNP_REQUEST_BLOCK` üzerinden
gelen BTL8 PnP/LUN yaşam döngüsü isteğini gösterir; CDB veya UFS medya verisi
içermez. Başarılı DATA_OUT payload'ları ETW örneğine hiçbir zaman kopyalanmaz;
kayıtların tümü yalnız WinPE olay günlüğüne/RAM'e yazılır.

Bir UFS komutu 50 ms içinde kapı zilini bırakmazsa miniport UFS reseti veya
iptal yazısı göndermez. İlk SRB `TIMEOUT` ile tamamlanır, yeni komutlar hızlı
biçimde `NO_DEVICE` döner ve WinPE/PnP akışı ekranda takılı kalmaz. Bu koruma,
tek-slotlu, senkron taşımanın kilitlenmesini önler.

`startnet.cmd`, snapshot kurma ve self-check sonucunu da özetler; tam anlık
görüntü `Device Parameters\B2LUFS` altında kalır. `ReportLunsRaw` değeri standart yanıtın tamamıdır: ilk dört
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
`NumberOfPhysicalBreaks=32`, donanımın PRDT sınırını StorPort'a bildirir.
Microsoft StorAHCI örneğindeki gibi, `HwBuildIo` sırasında hazır olan sistem
adresi ve tanı amaçlı DMA SGL pointer'ı isteğe özel `SrbExtension` içine
alınır. Kilitli `HwStartIo` normal aktarımı ortak DMA bounce tamponuyla yapar.
Exynos vendor yazmacındaki `PRDT_ENTRY_SIZE=12`, 4 KiB veri birimini seçer.
UFSHCI daha büyük bir DBC'ye izin verse de bu Exynos yapılandırması için
aktarımı vendor veri biriminde tanımlamak daha güvenlidir. Bu nedenle B2LUFS,
en çok 128 KiB'lik normal isteği tek SCSI komutu olarak korurken ortak DMA
alanını en çok 32 adet 4 KiB PRDT girdisine böler.
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
yaşam döngüsünü ve paging/hibernation/dump kullanım bildirimini ilan eder.
`ScsiUnitPower` ilan edilmediği için gelen legacy `SRB_FUNCTION_POWER`
istekleri adres doğrulamasından sonra tamamlanır; gerçek UTRL durdurma ve
yeniden başlatma StorPort'un ardışık `ScsiStopAdapter`/`ScsiRestartAdapter`
çağrılarına bırakılır. Ayrıca Microsoft'un iki parçalı bus-türü
sözleşmesini uygular: `HwFindAdapter` başında
`StorPortSetFeatureList(StorportFeatureBusTypeUnitControl)` çağrılır,
`ScsiUnitQueryBusType` destek listesine eklenir ve birim sorgusuna
`BusTypeUfs` döndürülür. Bu bildirim olmadan StorPort aygıtı varsayılan
`FIBRE` olarak etiketler. `StorQueryCapabilities` için tam
`STOR_DEVICE_CAPABILITIES_EX` cevabı verilir. Böylece StorPort disk PDO'sunu
normal PnP sözleşmesi ile kurar; PnP isteği UFS'e komut göndermez.
Sabit gömülü LUN 0'ın capabilities cevabı `Address=8` ve `UINumber=0`
taşır; SCSI BTL adresi yine `0/0/0` kalır. UFS0 ACPI hostunun altındaki
`DEV0`, `_ADR=8` ve `_RMV=0` ile StorPort'un disk PDO'sunu sabit dahili
medyaya bağlar. Bu hiyerarşi Lumia 950'nin `SDC1.EMMC` ve Surface Duo'nun
`UFS0.DEV0` tanımlarıyla aynıdır. Windows'un salt-okunur
`DEVPKEY_Device_LocationPaths` özelliğini işletim sistemi üretir; INF ve
miniport bu özelliği doğrudan yazmaz. `startnet.cmd`, HBA ve DiskDrive PDO
özelliklerini `pnputil /properties` ile göstererek DiskPart'ın biçimlendirilmiş satırından bağımsız
olarak Windows'un ürettiği ham LocationPaths değerini görünür kılar.
Her `ACCESS_RANGE`, yalnız StorPort'un verdiği kaynak üzerinden
`StorPortGetDeviceBase` ile haritalanır; `RangeInMemory` değeri API'nin
`InIoSpace` parametresi için terslenir. Kısmi/uyuşmayan kaynak dizisi hemen
geri bırakılır. `HwInitialize`, canlı HCE ve boş UTRL scheduler'ını
doğrulayıp tek DMA slotunu bağlar; başlangıç kapasite/LUN
probları `PASSIVE_LEVEL` callback'ine bırakılır. Microsoft'un
[StorPort yaşam döngüsü](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/life-cycle-of-a-storport-driver)
`HwFindAdapter -> HwInitialize -> HwAdapterControl(QuerySupported) -> I/O`
sırasındadır. Bu nedenle başlangıç callback'leri içinden doğrudan bus taraması
istenmez. `HwFindAdapter`, sonucu denetlenebilen açık bir StorPort timer
nesnesini `StorPortInitializeTimer` ile oluşturur. PASSIVE probundan sonra
`StorPortRequestTimer` bunu 500 ms'ye kurar; timer çalışınca bus 0 için tek
`BusChangeDetected` gönderilir. Operational kanaldaki `BLT1`
kaydı timer'ın gerçekten çalıştığını, içindeki `TransportReady=1` ise UFS
taşımasının tarama öncesinde hazır olduğunu gösterir. Normal çalışma
yığınında otomatik ilk LUN taraması, kapasite ile Windows GPT snapshot ve
self-check sonuçları hazır olana kadar `SRB_STATUS_NO_DEVICE` ile kapalı
tutulur. PASSIVE probu bütün snapshot alanlarını yayımladıktan sonra
`EnumerationReady=1` olur; gecikmeli `BusChangeDetected` böylece disk PDO'yu
ilk başarılı okumadan itibaren tek ve CRC-tutarlı GPT görünümüyle oluşturur.
`EnumerationGateRejectCount`, bu başlangıç penceresinde reddedilen keşif
isteklerini sayar. Bildirim PDO oluşturma sözleşmesinin yerine geçmez.
StorPort, ilan edilmiş
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
Normal SRB veri yolu `HwBuildIo`da `Srb->DataBuffer` veya
`StorPortGetSystemAddress` ile elde edilen sistem sanal adresini isteğe özel
extension'da saklar. Kilitli `HwStartIo`, WRITE payload'ını önce uncached ortak
DMA tamponuna; başarılı READ sonucunu da bu tampondan Windows adresine
kopyalar. Doğrudan Windows sayfalarına DMA yapılmadığı için kapasite probunda
çalıştığı kanıtlanmış fiziksel yol GPT ve dosya sistemi I/O'sunda da kullanılır.
Ön taramadaki gerçek UFS `READ CAPACITY` cevabı doğrulandıktan sonra disk
sınıfının aynı kapasite sorgusu bu değişmez anlık görüntüden, kilitli
`HwStartIo` yolunda tamamlanır.
`STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE` hem başlangıç hem adapter
yapılandırmasında ilan edilir. READ CAPACITY'nin `Srb->DataBuffer` alanı sistem
sanal adresidir; `HwBuildIo` yalnız istek-yerel adres/SGL hazırlığını yapar.
Adapter snapshot'ını okuyan yazılımsal cevap ve `RequestComplete`, eşzamanlı
`HwBuildIo`dan değil StorPort'un serileştirdiği `HwStartIo`dan yürütülür.
Normal READ/WRITE için eşleme düşük bellek
nedeniyle NULL gelirse `StorPortGetSystemAddress` denenir; bu da başarısızsa
SRB medyaya dokunmadan hatayla tamamlanır. Bu yol kapasite uydurmaz: `last-LBA`
ve 4096 bayt blok değeri aynı açılışta UFS'den gelen doğrulanmış cevaptır.
Kesme sahipliği alınmaz; tamamlanma kapı zili poll edilir. Miniport fiziksel
bir ACPI HBA olduğu için `Internal` arayüz türünü
kullanmaz: `InterfaceTypeUndefined` ile
`STOR_FEATURE_SET_ADAPTER_INTERFACE_TYPE` StorPort'a PnP'nin gerçek platform
arayüzünü seçtirtir. Bu ayrım önemlidir; Microsoft `Internal`ı sanal
miniportun işareti olarak tanımlar. Kaynak sayısı, MMIO eşlemesi veya kaynak
sözleşmesi reddedilirse sürücü `Microsoft-Windows-Storage-Storport/Operational`
kanalına `B2LUFS HwFindAdapter resource contract failure` ETW kaydı bırakır.
Bu kayıtlar gerektiğinde Operational kanaldan elle alınır. Otomatik WinPE
akışı bu kayıtları veya genel disk envanterini konsola dökmez.

## WinPE kabul ölçütü

`startnet.cmd` otomatik harf atamasını sürücü kurulumundan önce kapatır,
B2LUFS'u kurup yeniden tarar; `Disk 0 / Partition 28` odağındaki CACHE
volume'unu açıkça çevrimiçi yapıp `W:` olarak bağlar.
Akış, eski GPT/PnP envanteri/registry/ETW tanı kalabalığını üretmeden
yalnız yazma yolunun karar vermek için gereken ayrıntılarını gösterir:
paket kimliği, PnP kurulum ve tarama sonucu, DiskPart'ın volume'u online
yapma ve `W:` atama sonucu, `vol W:` bilgisi, yazılan yol ve geri okunan
içerik. Başarılı testin kabul kanıtı şu tek satırdır:

`CACHE basarili: W:\B2LUFS-WRITE-OK.TXT yazildi ve geri okundu.`

Bu satır, `Disk 0 / Partition 28`, bağlama sonrası `CACHE` etiketi ve FAT32
bağlama kontrollerinin geçtiğini; dosyanın yazılıp aynı içerikle geri okunduğunu
gösterir. `vol` çıktısındaki `CACHE` etiketi Windows'un `find.exe`
aracıyla doğrulanır; böylece yerelleştirilmiş `vol` metninde `findstr` kaynaklı
yanlış negatif oluşmaz. Otomatik akış GPT, tam PnP envanteri, registry veya
ETW tanı dökümü üretmez.

### Cihaz üstü doğrulama

17 Ağustos 2026 tarihinde beyond2lte/Exynos 9820 üzerinde
`storport-cache-online-write-report` paketiyle doğrulandı. WinPE, CACHE
bölümünü `Disk 0 / Partition 28` olarak çevrimiçi yaptı, `W:` harfini atadı ve
FAT32 `CACHE` volume etiketini doğruladı. Ardından
`W:\B2LUFS-WRITE-OK.TXT` dosyasına `B2LUFS CACHE WRITE OK` yazıldı ve aynı
içerik geri okundu. Böylece Windows dosya sistemi üzerinden UFS blok okuma ve
yazma zinciri gerçek cihazda başarıyla tamamlandı.

WIM'deki `b2lgptdump.exe`, `\\.\PhysicalDrive0` üzerinden salt-okunur olarak
PMBR, primary header, 16 KiB entry dizisi ve backup header'ı okur. Sonuçlar
sırasıyla `X:\B2LGPT-PMBR.BIN`, `X:\B2LGPT-PRIMARY.BIN`,
`X:\B2LGPT-ENTRIES.BIN` ve `X:\B2LGPT-BACKUP.BIN` dosyalarına yazılır;
ilk 64 baytlar aynı boot'ta konsola basılır. Dört okuma da başarılıysa araç
`IOCTL_DISK_UPDATE_PROPERTIES` göndererek `disk.sys`/`partmgr` bölüm
önbelleğini geçersiz kılar ve aynı aygıtı yeniden numaralandırır. Bu kontrol
isteği medyadaki GPT baytlarını değiştirmez. Araç WIM'de manuel tanı için
bulunur; sade otomatik CACHE yazma testi tarafından çağrılmaz.

StorPort Operational çıktısında önce `B2LUFS deferred bus scan timer fired`
ve `BLT1`, ardından `BLC3 Outcome=5, CompletionPhase=2`, normal `BLS2`
INQUIRY/READ CAPACITY kayıtları beklenir.
`BLT1` var fakat `BLS2` yoksa timer/adapter yaşam döngüsü tamamlanmış olmasına
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

RAM-disk WinPE'deki çalışma anı testi için paketin `Windows\B2LUFS` altından
`pnputil` ile kurulması yeterliydi. UFS'teki SYSTEM'den doğrudan boot ederken
ise çekirdek aynı diske erişmeden bu komut çalışamaz. Bu yüzden
`tools_ufsboot.sh`, INF/SYS/CAT üçlüsünü önce Windows DISM ile offline
`boot.wim` DriverStore/DriverDatabase yapısına kurdurur; ardından
`tools_ufsboot_prepare.sh` servis başlangıcını ve ACPI kritik aygıt eşlemesini
doğrular. Sadece SYS'i `System32\drivers` altına kopyalayan bir imaj geçerli
disk-boot artefaktı sayılmaz.

Paketlenmiş WIM'de ayrıca `\Windows\B2LUFS\manifest.txt` bulunur. Bu dosya
INF/SYS/CAT SHA-256 değerlerini taşır. `cache_esp.sh --win --system`,
`out/b2lufs/b2lufs.sys` bulunduğunda bu manifestteki SYS hash'ini WIM'i geri
okuyarak doğrular; uyuşmazsa sparse `system.img` üretmez. WinPE'de
`startnet.cmd` manifestin içeriğini PnP kurulumundan hemen önce gösterir.
`cache_esp.sh --win --bos`, CACHE'i WinPE içinden formatlamaz; Odin'in
flaşlayacağı tam 600 MiB `cache.img` dosyasını boş FAT32, 4096 bayt sektör ve
4096 bayt küme ile üretir.
Sürücü, CRC ile doğruladığı 4Kn GPT başlığını ve 16 KiB entry dizisini tek
bir CPU snapshot'ında tutar. `partmgr` metadata `READ` istekleri kilitli
`HwStartIo` içinde bu tutarlı snapshot'tan tamamlanır; bir istekte düzeltilmiş
PMBR, başka bir istekte ham Samsung GPT dönmesi engellenir. Stok medyadaki
`CACHE` girdisi bu salt Windows görünümünde Microsoft Basic Data GUID'i ve
temiz GPT attribute alanı taşır; ham GPT'ye yazılmaz.

Renegade/edk2-msm SM7125 karşılaştırmasında firmware tarafı UFS
denetleyicisini `QCOM24A5`, MMIO/IRQ, `_CCA` ve `_ADR=8` alt aygıtıyla
tanıtıp `UFSDxe` ile boot erişimini sağlar; GPT'yi ACPI/UEFI üzerinden
Windows'a ayrıca aktarmaz. Windows tarafındaki fark Qualcomm donanımını
tanıyan inbox `storufs.sys` miniportudur. B2LUFS'ta kapasite, READ ve CRC
doğrulamaları başarılı olduğundan Exynos handoff/MMIO/DMA yolu çalışır;
bölüm tanıma StorPort'a dönen tutarlı blok görünümünde çözülür.
DiskPart'ın `Location Path: UNAVAILABLE` metni UFS için bir I/O veya GPT
başarısızlık koşulu değildir; HBA'nın PnP `LocationPaths` değeri ayrıca
tanıda gösterilir.

Normal otomatik harf ataması Android RAW bölümleri için C:-Z: aralığını
tükettiğinden `startnet.cmd`, B2LUFS kurulmadan önce WinPE mount manager
automount özelliğini kapatır. Ardından stok beyond2lte haritasındaki
`Disk 0 / Partition 28` odağını kurar, bu yüzden `Healthy / Offline` kalan
ilişkili volume'u açıkça çevrimiçi yapar, varsa eski W: mount pointini kaldırır
ve volume'u `W:` olarak bağlar. `CACHE` etiketi bağlama sonrasında doğrulanmadan
dosya yazılmaz. Son kullanıcı çıktısında yalnız kurulum ve yaz/geri-okuma sonucu
gösterilir; ham GPT, DiskPart envanteri, PnP/registry özeti ve ETW dökümü
otomatik akışta çalıştırılmaz. Güvenlik kapısından sonra
`W:\B2LUFS-WRITE-OK.TXT` dosyası yazılıp geri okunur. Bölüm başlangıcı,
uzunluğu ve FAT32 içeriği yeniden oluşturulmaz.
Dolayısıyla tekrar `B2LUFS paketi yok` görülürse çalışan ESP, bu doğrulama
kapısından geçmiş son artefakt değildir; UFS donanım sonucu olarak yorumlanmaz.

GPT partition-entry dizisi 128 x 128 bayt olduğundan toplam 16 KiB'dir. Erken
doğrulama bunu dört 4 KiB PRDT girdisiyle tek `READ(10)` olarak okur. Bu salt
okunur tanı komutu zaman aşımına uğrarsa UTRL doorbell'i sonraki StorPort
taramasını sürekli BUSY bırakmasın diye yalnız sürücünün UTP kuyruğu durdurulup
yeniden başlatılır; HCE, UniPro/PHY ve medya içeriğine dokunulmaz.

UFSHCI `IS` yazmacı `IE=0` iken de durum biriktirir ve bitleri write-one-to-
clear'dır. Eski poll döngüsü önce `IS`, sonra `UTRLDBR` okuyordu; komut bu iki
okuma arasında tamamlanırsa fonksiyon başarıyla dönerken UTR completion biti
sonraki slot-0 kullanımına sarkabiliyordu. B2LUFS artık kapı zilini çalmadan
önce boş slotun gecikmiş durumunu onaylar. Her yeni komutta taze
`UTP_TRANSFER_REQ_COMPL` olayını W1C ile tüketir ve hem bu olay görülmeden hem
de doorbell düşmeden yanıt belleğini kabul etmez. Böylece LBA 0, LBA 1, GPT
giriş dizisi ve ardından StorPort SRB'leri aynı tek UTR slotunu art arda
güvenle kullanır.

Ölçülen ilk HCI durumu: `CAP=1303FF1F`, `VER=00000300`, `HCE=1`,
`1US_TO_CNT=000000A6`. CAP alanında NUTRS `[4:0]` ve NUTMRS `[18:16]`
sırasıyla `31` ve `3` olduğundan denetleyici 32 UTR + 4 UTMR kapasitelidir;
bit 24 de 64-bit adreslemeyi bildirir. B2LUFS bu kapasitenin yalnız bir UTR
yuvasını kullanır; StorPort bu yüzden tek SRB'ye sınırlandırılır. Sürücü
Windows'un rastgele sayfalarını bitişik varsaymaz: en çok 128 KiB'lik isteği
StorPort'un CPU/aygıt ortak uncached DMA tamponuna kopyalar ve bu doğrulanmış
fiziksel alanı PRDT'ye verir.

BusChange öncesinde aynı ortak-DMA yoluyla LBA 0 ve LBA 1 için birer gerçek
`READ(10)` yapılır. `MediaProbeMbrSignature=1`, LBA 0 sonundaki `55 AA`yı;
`MediaProbeGptSignature=1`, LBA 1 başındaki `EFI PART` imzasını doğrular. GPT
başlık CRC'si ve Windows'un desteklediği en çok 128 girişlik partition-array
CRC'si de doğrulanır. Samsung'un 4Kn GPT'sindeki kaynak `HeaderSize=4096`
değeri ve 4096 baytlık kaynak CRC kapsamı bu aşamada korunarak doğrulanır.
PMBR denetimi yalnız `0xEE` kaydına bakmaz: disk imzası
ve rezerv word, kullanılmayan üç MBR kaydı ve 4Kn LBA0'ın `512..4095` rezerv
alanı da UEFI 2.10 gereği sıfır olmalıdır. Geçerli GPT doğrulandıktan sonra
sürücü medyaya yazmadan, disk-class READ yanıtındaki LBA0'ı tam 4096 bayt
kanonik PMBR olarak sunar. Windows'a verilen birincil ve yedek GPT
başlıkları `HeaderSize=92` olarak kanonikleştirilir; `92..4095` rezerv alanı
sıfırlanır ve iki başlığın CRC'si 92 bayt üzerinden yeniden hesaplanır.
Bu yalnız bellekteki Windows görünümünde yapılır ve
`MediaProtectiveMbrNormalized=1` yayımlar. Gerçek GPT başlığı, entry-array,
partition LBA'ları ve içerikleri değiştirilmez. Normal blok tamponlarına CPU erişimi
Microsoft'un program-controlled I/O sözleşmesine göre doğrudan
`StorPortGetSystemAddress` ile alınır. Ham GPT içeriği önbellekten Windows'a
sunulmaz; disk sınıfının kullanacağı normal blok yolu erkenden sınanır. Normal
READ/WRITE CDB blok
sayısı, READ CAPACITY ile alınan mantıksal blok boyutuyla tam olarak
`DataTransferLength` üretmiyorsa ya da aygıt kısa blok aktarımı döndürürse SRB
artık sessiz başarıyla tamamlanmaz. Böylece partition yöneticisi sıfır/eksik
bir GPT okumasını geçerli boş disk olarak yorumlayamaz.

Yerel doğrulama:

```bash
clang -std=c11 -Wall -Wextra -Werror \
  windows/b2lufs/b2lufs_resources.c windows/b2lufs/test_resources.c \
  -o /tmp/b2lufs-resources-test && /tmp/b2lufs-resources-test
```

## Sonraki doğrulamalar

1. DiskPart'ta yaklaşık 119 GiB kapasiteyi ve GPT bölümlerini doğrula; ardından
   yalnız geri alınabilir bir test alanında okuma/yazma/geri-okuma deneyi yap.
2. UEFI'den Exynos başlatması ancak HCE/link devralması kesin olarak
   başarısız olursa ele alınır: VCC/PHY isolation, IO coherency,
   `1US_TO_CNT=166`, saat/refclk/clock-gating, CAL pre-link, standart
   `DME_LINKSTARTUP`, CAL post-link. UTRD/UPIU/SCSI bundan sonra gelir.

Yazma yolu artık açıktır; testten önce stok firmware ve gerekli kullanıcı
verisi yedekleri erişilebilir tutulmalıdır.
Test imza zinciri mevcut `windows/b2lfb/b2l-test.pem` üzerinden korunur;
üretim imzası bu get-up aşamasının kapsamı dışındadır.
