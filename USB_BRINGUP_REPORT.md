# beyond2lte USB/CACHE bring-up devir notları

Son güncelleme: 2 Eylül 2026  
Hedef cihaz: Samsung Galaxy S10+ `beyond2lte`, Exynos 9820  
Mevcut uygulama revizyonu: `B2LUsbTest R16`  
Mevcut commit: `5ff0a6f` (`fix: retry Exynos USB HS PHY startup`)

## Amaç ve mevcut sonuç

Amaç, Windows çalışmadan önce UEFI içinde telefonun `CACHE` adlı 600 MiB FAT32
bölümünü USB Mass Storage/Bulk-Only Transport aygıtı olarak PC'ye sunmak ve
dosya aktarabilmekti.

Bu hedef henüz tamamlanmadı. Bugüne kadarki en ileri cihaz kanıtı R12'de elde
edildi: HS PHY açıldı, DWC3 High-Speed device olarak çalıştı, PC fiziksel aygıtı
gördü, Windows “tanınmayan USB aygıtı” uyarısı verdi ve DWC3 event halkasında
gerçek `USB_RESET` ile `CONNECT_DONE` olayları görüldü. Bu sonuç kablo, USB2
PHY, DWC3 soft-connect ve PC host yolunun en az bir testte çalıştığını kanıtlar.

R13 ile EP0 descriptor, Bulk-Only Transport, SCSI ve CACHE BlockIo kodu eklendi.
R15 cihaz testi CACHE yolunda daha EP0 başlatılmadan, HS PHY açma/doğrulama
aşamasında başarısız oldu. R16 için kullanıcı yeniden başarısız sonuç bildirdi;
ancak yeni hata ekranının PMU/SIDDQ değerleri veya net fotoğrafı olmadığı için
R16'nın kesin kesilme aşaması henüz doğrulanmadı. Windows'un CACHE'i disk olarak
bağladığı veya gerçek bir SCSI read/write yaptığı henüz kanıtlanmadı.

## Kısa durum tablosu

| Bileşen | Durum | Cihaz kanıtı |
|---|---|---|
| `B2LUSB.EFI` çalıştırma ve paketleme | Çalışıyor | Uygulama Windows'tan önce açılıyor |
| UEFI dokunmatik ve dört fiziksel tuş | Çalışıyor | Menü ve seçimler kullanılabildi |
| DWC3/PHY MMIO eşlemeleri | Çalışıyor | Kimlik ve güvenli registerler okunabildi |
| DWC3 device rolü | Doğrulandı | `CURMOD=DEVICE`, xHCI halted |
| USB2 HS PHY fiziksel attach | Bir kez doğrulandı | R12: `PMU_USBPHY=1`, `SIDDQ=0`, reset/connect eventleri |
| PC'nin cihazı algılaması | Kısmen doğrulandı | R12: Windows “tanınmayan USB aygıtı” |
| CACHE bölümünün bulunması ve FAT32 kontrolü | CACHE yolunda geçildi | R15 hata noktası bu kontrollerden sonra |
| EP0 descriptor yanıtları | Kod var, cihazda doğrulanmadı | Mevcut hata EP0 init'ten önce oluşuyor |
| USB MSC/BOT/SCSI | Kod var, cihazda doğrulanmadı | Windows'ta disk oluşmadı |
| CACHE üzerinden dosya aktarımı | Başarısız | Henüz okuma/yazma sayacı artmadı |
| Başlangıç hatasında Windows'a düşmeme | R15'te doğrulandı | R15 fail-stop ekranında kaldı; R16 başarısızlığı ayrıntısız bildirildi |

## Revizyonlara göre ilerleme

| Revizyon | Yapılan değişiklik | Cihaz sonucu ve öğrenilen |
|---|---|---|
| R1 | FSYS0A ve USB güç alanları için ilk aşamalı salt-okuma tanısı | `0x15862214` okuması synchronous external abort verdi. Bilinmeyen duration registerleri güvenli kabul edilemez. |
| R2 | Hata veren FSYS0A `+0x10/+0x14` okumaları kaldırıldı; PMU ayrı aşamaya bölündü | FSYS0A `CONFIG=1`, `STATUS=1`, `OPTION=0x10`; PMU USB kontrolleri sıfırdı. DWC3 `GSNPSID @ 0x10C0C120` erişimi level-2 translation fault verdi. |
| R3 | DWC3 ve DT'deki PHY pencereleri `MemoryMapLib` içinde non-secure Device MMIO eşlendi | DWC3 okunabildi; `GSNPSID=0x33313130` gerçek bir DWC_usb31 çekirdeği çıktı. |
| R4 | DWC_usb31 `0x3331` kimliği kabul edildi; device/OTG ve PHY sayfaları açıldı | DWC3 device rolde, xHCI halted ve hatasızdı. Combo PCS `+0x0c` external abort verdi. |
| R5 | Bilinen PCS `+0x0c` atlandı | PCS'nin ilk `+0x00` erişimi de external abort verdi. |
| R6 | PCS tamamen atlandı, PMA ayrı aşamada denendi | Combo PMA `0x10AF0000 + 0x00` external abort verdi. Exception ekranında ayrıca dokunmatik HSI2C transfer timeout görüldü; bu, abort sonrasında ortaya çıkan ikincil bir belirtiydi. |
| R7 | Combo PCS/PMA MMIO okumaları tamamen kaldırıldı; kablolu güvenli karşılaştırma yapıldı | Kablo takılı ve takılı değil durumlarda PMU, DWC3, xHCI ve ana DRD PHY açısından anlamlı attach farkı görülmedi. |
| R8 | MAX77705 IRQ/GPIO ve HSI2C45 için aşamalı tanı eklendi | MAX77705 IRQ active-low olarak düşük, SCL/SDA yüksek; fakat HSI2C45 clock/timing/FIFO registerleri sıfırdı. I2C/MMIO transferi yapılmadı. |
| R9 | Aynı boot içinde iki saniyelik canlı attach monitörü eklendi | PC, Mac, kablosuz ve şarj adaptörü karşılaştırmalarında gerçek attach deltası çıkmadı. |
| R10 | DSTS SOFN sahte deltası maskelendi; reserved event DMA ve geçici `RUN_STOP` aktif probu eklendi | DWC3 çalıştı fakat yalnız tekrarlanan `0x00000901` EP0 `FIFO_EVENT` görüldü; `USB_RESET/CONNECT_DONE` yoktu. PHY hâlâ izolasyondaydı. |
| R11 | Stok DT ve Samsung vendor kodundan dar PMU/UTMI HS PHY açılışı eklendi | `PMU_USBPHY=1`, `HSP_TEST.SIDDQ=0`; PHY açıldı. Ancak `DCFG.DEVSPD=5` yüzünden DWC3 SuperSpeedPlus bekledi ve link `RX_DET` durumunda kaldı. |
| R12 | Yalnız `DCFG.DEVSPD` alanı High-Speed (`0`) yapıldı; event kod çözümü incelendi | Windows fiziksel aygıtı gördü. Ham `0x00000101` ve `0x00000201` olaylarının sırasıyla `USB_RESET` ve `CONNECT_DONE` olduğu anlaşıldı. Ekrandaki eski kod çözümü bunları yanlış sınıflandırmıştı. |
| R13 | EP0 descriptorları, endpoint 2/3 Bulk IN/OUT, MSC BOT, dar SCSI kümesi ve yalnız CACHE BlockIo arka ucu eklendi | Kod yolu tamamlandı; fakat daha sonraki gerçek CACHE testlerinde EP0 aşamasına ulaşılamadı. |
| R14 | Büyük hedefli dokunmatik menü, salt-okuma/okuma-yazma seçimi ve ikinci yazma onayı eklendi | Dokunmatik kullanılabildi. GOP renk blokları ile SimpleTextOut ölçeği uyuşmadığı için metin küçük/yanlış hizalı göründü. |
| R15 | `CACHE`/`cache` tam eşleşmesi düzeltildi; CACHE seçildikten sonraki bütün başlangıç hataları fail-stop yapıldı | Yanlışlıkla Windows'a devam etme düzeldi. Gerçek hata `EFI_DEVICE_ERROR`; alttaki tanı mesajı HS PHY enable doğrulamasının başarısız olduğunu gösterdi. |
| R16 | PMU biti poll edildi, 1 ms settle eklendi, vendor POR dizisi üç kez denenir hâle getirildi; son PMU/SIDDQ değerleri hata ekranına taşındı | Kullanıcı testi yine başarısız olarak bildirdi. Yeni hata ekranındaki kesin aşama ve `PMU/HSP_TEST` değerleri henüz kayda alınmadı. Yalnız gecikme/retry eklemek hedefi çalıştırmadı. |

## Karşılaşılan önemli hatalar

### 1. Güvenli görünmesine rağmen abort veren MMIO alanları

- `FSYS0A + 0x14` (`0x15862214`) synchronous external abort verdi.
- Combo PCS `0x10AE0000 + 0x0c` ve daha sonra `+0x00` abort verdi.
- Combo PMA `0x10AF0000 + 0x00` abort verdi.
- Bu adreslerin page table'da eşlenmiş olması, saat/güç/secure erişim şartlarının
  sağlandığı anlamına gelmiyor.
- Sonuç olarak combo PHY pencereleri tanı ve MSC yollarından tamamen çıkarıldı.

### 2. DWC3 MMIO translation fault

R2'de `GSNPSID @ 0x10C0C120` erişimi `ESR=0x96000006` ile level-2 translation
fault verdi. DT'de bulunan gerçek DWC3/PHY pencereleri platform memory map'e
eklendiğinde R3'te aynı register başarıyla okundu.

### 3. Yanlış DWC3 kimlik varsayımı

İlk kontrol yalnız eski `0x5533xxxx` DWC_usb3 kimliğini bekliyordu. Cihazdaki
çekirdek DWC_usb31 ve `GSNPSID=0x33313130`. `0x3331` maskesi kabul edilince
çekirdeğin 1.70a sürümü ve device registerleri okunabildi.

### 4. Pasif kablo algılamasının hiçbir katmanda görünmemesi

Kablo PC, Mac veya adaptöre takıldığında pasif PMU/DWC3/OTG/xHCI/ana PHY
registerlerinde anlamlı değişiklik oluşmadı. `DEV=1` görünen tek delta gerçek
attach değil, sürekli ilerleyen DSTS `SOFN[16:3]` sayacıydı.

### 5. MAX77705 yolunun hazır olmaması

IRQ ve pin seviyeleri anlamlı olsa da HSI2C45 controller'ın clock, timing ve
FIFO registerleri sıfırdı. Bu durumda MAX77705'e I2C transferi yapmak güvenli
değildi. `B2LMax77705.c` kaynakta tutuldu, fakat aktif uygulamaya bağlanmadı.
MAX77705 için UCSI uyumlu EC/ACPI transport kanıtı da bulunmadı.

### 6. `FIFO_EVENT` olayının yanlış başarı sayılması

R10'daki `0x00000901`, EP0 `RXTXFIFOEVT/FIFO_EVENT` olayıydı. Bu olay host
resetini veya enumeration'ı kanıtlamaz. Gerçek fiziksel kanıt R12'de görülen
`0x00000101 USB_RESET` ve `0x00000201 CONNECT_DONE` olaylarıdır.

### 7. High-Speed PHY açıkken yanlış hız seçimi

R11'de yalnız USB2 PHY açık olmasına rağmen `DCFG.DEVSPD=5` SuperSpeedPlus
seçiyordu. Link bu nedenle `RX_DET` durumunda kaldı. Alan High-Speed'e çekilince
R12'de PC reset/connect üretti.

### 8. Windows “tanınmayan USB aygıtı”

Bu mesaj tamamen başarısızlık değildi: fiziksel attach ve host resetinin
ulaştığını gösterdi. R12 descriptor/EP0 sunmadığı için Windows'un aygıtı
tanımlayamaması bekleniyordu. PC tarafında özel bir “DWC3 sürücüsü” gerekmez;
PC hosttur. Telefon UEFI'sinin device olarak geçerli descriptor ve endpoint
cevaplarını sağlaması gerekir.

### 9. CACHE MSC yolunda HS PHY `Device Error`

R15'te CACHE bulundu, BlockIo/FAT32 hazırlığı ve DWC3 preflight geçildi; ardından
`EnableHsPhy` doğrulaması başarısız oldu ve `EFI_DEVICE_ERROR` üretildi. R16,
PMU/SIDDQ poll, settle ve üç POR denemesi ekledi fakat kullanıcı sonucu yine
başarısız bildirdi. R16 için yeni kesin hata aşaması görüntülenmedi.

Bu sonuç, “yalnız biraz daha beklemek gerekir” varsayımını zayıflatıyor. Aynı
temel PHY dizisinin R11/R12 tanı yolunda çalışıp CACHE yolunda çalışmaması,
farkın adres veya kablodan çok yürütme durumu/ownership/sıralama ile ilişkili
olabileceğini gösteriyor.

### 10. Hata sonrasında Windows'a istemsiz devam

İlk dokunmatik menü sürümünde CACHE başlatma hatası üst çağırana dönüyor ve
`startup.nsh` Windows'u açabiliyordu. R15'ten itibaren CACHE seçildikten sonraki
başlangıç hataları sonsuz fail-stop ekranında kalıyor. Bu davranış cihaz
fotoğrafıyla doğrulandı.

## Kesin olarak öğrendiklerimiz

1. Cihazdaki USB çekirdeği Synopsys DWC_usb31'dir; tabanı `0x10C00000`, kimliği
   `0x33313130` ve testlerde device modundadır.
2. Ana USB DRD PHY `0x10B00000` penceresi güvenli biçimde okunabiliyor; combo
   PCS/PMA pencereleri aynı koşullarda güvenli değil.
3. USB2 PHY izolasyonu PMU `0x1586072c[0]` ile kontrol ediliyor. Samsung vendor
   kodunda DT'deki `pmu_mask=0`, `BIT(0)` olarak yorumlanıyor.
4. HS PHY için kullanılan `LINK_CTRL`, `CLKRST`, `UTMI`, `HSP`, `HSP_TUNE` ve
   `HSP_TEST` sırası vendor sürücüyle eşleştirildi.
5. Sadece DWC3 `RUN_STOP` yapmak fiziksel attach için yeterli değil; PMU ve HS
   PHY de açılmalı.
6. Yalnız HS PHY açıkken DWC3 hızı açıkça High-Speed seçilmeli.
7. R12, donanımın en az bir boot/test yolunda PC'ye device olarak ulaşabildiğini
   kanıtladı. Sorun “port fiziksel olarak hiç çalışmıyor” kadar genel değil.
8. Son ayrıntılı R15 kanıtında engel descriptor veya Windows sürücüsü değil;
   yürütme EP0 başlatılmadan önce PHY doğrulamasında kesiliyor. R16'nın yeni
   kesilme aşaması ayrıca kaydedilmelidir.
9. Windows PC'nin xHCI/USB Mass Storage host sürücüleri ile telefon tarafındaki
   DWC3 function/device kontrolü farklı görevlerdir. Host-mode DWC3 belgeleri
   telefon UEFI'sindeki gadget kodunun yerini tutmaz.
10. MMIO erişiminin page table'da olması yeterli değildir; power domain, clock,
    secure ownership ve reset durumu ayrıca sağlanmalıdır.
11. R16'nın başarısızlığı, kör gecikme ve tekrarların tek başına çözüm olmadığını
    gösterdi. Bir sonraki tanı her PHY yazımını aşama aşama karşılaştırmalıdır.

## Uygulanan kod ve güvenlik sınırları

Başlıca dosyalar:

- `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LUsbTest.c`
  — aşamalı tanı ve ana dokunmatik menü.
- `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LDwc3Probe.c`
  — güvenli DWC3 event probu, HS PHY akışı, EP0, MSC/BOT/SCSI ve CACHE arka ucu.
- `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LTouchUi.c`
  — yalnız EFI Absolute Pointer/GOP kullanan dokunmatik arayüz.
- `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Applications/B2LUsbTest/B2LMax77705.c`
  — korunmuş fakat uygulamaya bağlanmamış salt-okuma MAX77705 yardımcısı.
- `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib.c`
  — DT'den doğrulanan USB Device MMIO eşlemeleri.
- `tests/test_uefi_usb_safety.sh` — allow-list, CACHE geometrisi, fail-stop ve
  kapsam genişlemesini denetleyen statik güvenlik testi.

Korunan sınırlar:

- Depolama hedefi yalnız GPT adı tam `CACHE` veya `cache`, blok boyu 4096,
  son LBA 153599 ve toplam boyu 629145600 bayt olan bölümdür.
- GPT ve başka partition LBA'ları yazılmaz.
- Yazma modu ayrı ikinci kullanıcı onayı ister.
- Combo PHY, DWC3 rol/core-reset ve endpoint 4+ write allow-list'e eklenmedi.
- Event/transfer DMA belleği 4 GiB altında `EfiReservedMemoryType` olarak ayrılır.
- DWC3 iki saniyede halt olmazsa olası DMA hedefi serbest bırakılmaz ve cihaz
  Windows'a devam etmez.
- Başarılı MSC oturumu sonrasında da firmware FAT cache durumunu yeniden
  kullanmamak için Windows'a dönülmez; donanım reboot istenir.
- Bootloader güvenliği gereği yalnız kontrollü geri dönüş yolları uygulamadan
  çıkabilir; CACHE başlangıç hatası uygulamadan çıkamaz.

## Mevcut yürütme noktası

R16 CACHE kod yolu aşağıdaki sıradadır. `GEÇİLDİ/BAŞARISIZ` işaretleri son
ayrıntılı R15 cihaz fotoğrafına dayanır; R16 için yalnız genel başarısızlık
bildirimi vardır:

```text
CACHE BlockIo bul                 GEÇİLDİ
Erişim modunu seç                GEÇİLDİ
FlushBlocks                      GEÇİLDİ
Reserved DMA ayır                GEÇİLDİ
FAT32 imzası/geometri doğrula    GEÇİLDİ
DWC3 güvenli preflight           GEÇİLDİ
PHY/PMU kalıtılmış durumu kaydet GEÇİLDİ
HS PHY aç ve doğrula             BAŞARISIZ
DCFG/event halkası               ULAŞILMADI
EP0 initialize                   ULAŞILMADI
USB_RESET/GET_DESCRIPTOR         ULAŞILMADI
Bulk endpoint + MSC/BOT/SCSI     ULAŞILMADI
CACHE dosya aktarımı             ULAŞILMADI
```

Dolayısıyla mevcut test, R13'teki descriptor veya SCSI uygulamasının doğru ya
da yanlış olduğunu henüz söylemiyor.

## Devam edilecekse en değerli sonraki ölçümler

1. R16 son hata ekranındaki `PMU`, `ON`, `HSP_TEST` ve `SIDDQ` değerlerini net
   kaydetmek. Kullanıcının son bildirimi başarısızlığı doğruluyor fakat bu dört
   değer henüz elde değil.
2. Tek bir son doğrulama yerine her vendor aşamasından sonra beklenen/okunan
   `PMU`, `LINK_CTRL`, `CLKRST`, `UTMI`, `HSP`, `HSP_TUNE` ve `HSP_TEST`
   değerlerini kaydetmek.
3. Başarılı R12 event-probe ile başarısız CACHE yolunu aynı ortak PHY state
   machine fonksiyonuna indirmek; çağrı öncesi farkları ayrıca yazdırmak.
4. CACHE BlockIo/FAT32 erişiminden önce ve sonra FSYS0 clock/power ownership'in
   değişip değişmediğini yalnız önceden doğrulanmış güvenli registerlerle
   karşılaştırmak.
5. UEFI dokunmatik timer/poll olayını PHY açılışı sırasında geçici quiesce edip
   sonuç değişiyor mu kontrol etmek. Bu şu an yalnız bir hipotezdir.
6. PHY tekrar kararlı açıldıktan sonra önce salt-okuma modunda yalnız device ve
   configuration descriptorlarını doğrulamak; daha sonra BOT/SCSI'ye geçmek.

## Derleme, paketleme ve test notu

Firmware build bu macOS makinesinde yapılmamalıdır. Derleme WSL'de, flash
Windows'ta yapılır:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

`--usb-test` artık aynı turda `tools_b2lusb.sh` üzerinden Windows ARM64
KMDF/URS paketini de derleyip `boot.wim`e enjekte eder. Yalnız Windows paketini
yenilemek için `bash tools_b2lusb.sh` kullanılabilir. `--baseline`, UEFI
`B2LUSB.EFI` tanısını ESP'ye koymaz; WIM içindeki imzalı Windows paketini ve
`startnet.cmd` kurulumunu korur.

Üretilen USB tanı paketi `out/beyond2lte-usb-diag.tar` dosyasıdır. Odin'de AP
alanından ve **Re-Partition kapalı** olarak kullanılır.

## Windows ARM64 sürücü durumu (02 Eylül 2026)

İlk host bring-up katmanı kaynak ağacına eklendi; henüz cihaz üzerinde
doğrulanmadı:

- `ACPI\BYN0002` URS parent, `_ADR=0` host ve `_ADR=1` function çocukları;
- DWC3, ana DRD PHY ve USB2 PMU bitini kesin fiziksel adres/boyut allow-list'i
  ile eşleyen `b2lusb.sys`;
- `URS\BYN0002&HOST` kimliğini inbox `USBXHCI` kurulumuna bağlayan INF;
- `status|host|device|none` komutlu ARM64 `b2lusbctl.exe`;
- WSL -> Windows PowerShell/MSVC/WDK -> katalog/test-imza -> WIM hash geri-okuma
  derleme zinciri.

02 Eylül 2026 WSL çalıştırması ARM64 derleme, link ve imzalama aşamalarını
geçti; ilk engel çıplak ADK WinPE'nin `UrsCx01000.sys` içermemesiydi. WDK bu
bileşenin yalnız başlık ve import library'sini sağlar. Paketleme zinciri artık
eksik Microsoft runtime'ını aynı build'deki ARM64 UUP/Windows ISO
`install.wim`/`install.esd` kaynağından alır. Kaynak
`Services\UrsCx01000` ağacı da offline SYSTEM hive'dan birebir aktarılır;
servis değerleri tahmin edilmez. Binary, registry ve manifest WIM'e yazıldıktan
sonra geri okunup doğrulanmadan `cache_esp.sh --baseline` kapısı geçmez.

ISO otomatik bulunamazsa WSL turu şu biçimde çalıştırılır:

```bash
B2L_WINDOWS_ARM64_SOURCE="/mnt/c/.../Windows11_ARM64.iso" \
  bash yap.sh --touch --usb-test --clean
```

Tam build eşleşmesi tercih edilir. Eldeki `10.0.26200` UUP kaynağındaki
sürümlenmiş `UrsCx 1.0`, `10.0.28000` WinPE'ye deneysel uyumluluk olarak kabul
edilir; daha yeni kernel runtime'ının eski WinPE'ye eklenmesi reddedilir.
Offline registry aktarımı WSL'de `libhivex-bin` ve `libwin-hivex-perl`
paketlerini gerektirir.

Telefonda manuel `b2lusbctl.exe host` yazmak için önce çalışan bir klavye
gerekmesi nedeniyle `yap.sh --usb-test` artık açık test işareti olarak WIM'e
`auto-host.test` ekler. `startnet.cmd`, URS parent kurulumu tamamlandıktan sonra
yalnız bu işaret varsa HOST geçişini otomatik yapar; sonuç ve USB/HID PnP
dökümü `X:\B2LUSB-AUTOHOST.TXT` içine yazılır. Normal doğrudan paketleme
varsayılanı manuel kalır. MAX77705 VBUS source henüz uygulanmadığı için ilk
fiziksel HID deneyi yine haricen beslenen USB2 hub gerektirir.

SM-G975F'nin 1440x3040 ekranı için WinPE kullanıcı ölçeği boot öncesi DEFAULT
hive'da %175 (`LogPixels=168`, `Win8DpiScaling=1`) yapılır; konsol ızgarası da
112x72'den 64x41'e indirilir. Runtime ve offline hive değerleri WIM'e yazılıp
geri okunarak doğrulanır.

İlk otomatik HOST cihaz ölçümünde `ACPI\BYN0002` parent kuruldu fakat PnP
`Code 31 / CM_PROB_FAILED_ADD`, problem status `0xC0000079` verdi ve device
interface oluşmadı. Kod `STATUS_INVALID_SECURITY_DESCR` olarak çözüldü:
`WdfDeviceInitAssignSDDLString` isimsiz bir WDF device üzerinde çağrılmıştı.
FDO artık SDDL'den önce `FILE_AUTOGENERATED_DEVICE_NAME` ile işaretlenir.
CLI de SetupAPI temizliği sırasında `GetLastError()` değerini korur; sonraki
arayüz hatası artık yanıltıcı `Win32=0` göstermez. USB hatasının ana akışı
`:Finish` etiketine atıp touch kurulumunu engellemesi de kaldırıldı; touch ve
UFS tanıları USB sonucundan bağımsız ilerler.

Sonraki cihaz ölçümünde bu düzeltme doğrulandı: `ACPI\BYN0002` parent
`Started`, interface erişilebilir ve URS rol sonucu
`requested=HOST current=HOST last_ntstatus=0` oldu. Host child
`URS\VEN_BYN&DEV_0002&HOST` oluşturuldu fakat Microsoft `USBXHCI` yığını
`Code 10 / CM_PROB_FAILED_START`, problem status `0xC000009E`
(`STATUS_DEVICE_POWER_FAILURE`) verdi. Son snapshot'ta `USBSTS=1`, yani xHCI
halted kaldı. Bu yeni sınır rol isteğinin değil DWC_usb31 core/xHCI handoff'un
başarısız olduğunu gösterir.

DWC_usb31 rol değiştirme akışına eksik core-clock senkronizasyonu eklendi:
eski controller halt edildikten ve USB2 PHY açıldıktan sonra
`GCTL.CORESOFTRESET` 100 ms tutulur, readback ile doğrulanıp temizlenir ve
`PRTCAP` ancak bundan sonra HOST/DEVICE yapılır. Süre upstream DWC3'ün eski
DWC_usb31 rol değiştirme yoluyla eşleşir. Yeni `0.1.0.2` tanısı core reset
sayısını, DWC3 version/GUSB2/GUSB3 registerlerini, xHCI capability, HCSPARAMS1,
HCCPARAMS1, USBCMD, USBSTS ve PAGESIZE değerlerini basar. `startnet.cmd` ayrıca
host child servis/stack/driver/resource listesini toplar; yeni deneme yine
`Code 10` verirse core-reset ve interrupt/resource hipotezleri fotoğraftan
ayrıştırılabilir.

Bu aşama HID için gereken Windows host yazılım zincirini hazırlar ama fiziksel
başarı iddiası değildir. SuperSpeed portu combo PHY abort'u nedeniyle sürücüde
kapalıdır. MAX77705/UcmCx olmadığı için ilk HID testi haricen beslenen USB2 hub
ile yapılmalıdır. Function child, UFX ve KDNET DBG2 henüz bağlanmamıştır.
Detaylı test sözleşmesi `windows/b2lusb/README.md` içindedir.

## Kaynaklar

- Samsung Exynos9820 vendor PHY referansı, sabitlenen revizyon:
  `Android-Artisan/android_kernel_samsung_exynos9820@9a0a792d227c8ae6cddac0c9d68a8bb9782c44cb`
  (`phy-exynos-usb3p1.c`, `phy-exynos-usb3p1-reg.h`, `phy-exynos-usbdrd.c`).
- Qualcomm DWC3 sürücü genel bakışı:
  <https://docs.qualcomm.com/doc/80-88500-4/topic/119_USB_DWC3_driver.html>
- Microsoft USB dual-role mimarisi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-dual-role-driver-stack-architecture>
- Microsoft USB function controller/UFX mimarisi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-usb-function-controllers>
- USB Mass Storage Bulk-Only Transport 1.0:
  <https://www.usb.org/sites/default/files/usbmassbulk_10.pdf>

## İlgili commitler

```text
222759e  feat: add staged EFI USB diagnostics
5ddc74e  fix: avoid faulting FSYS0A USB diagnostic reads
5cf1810  fix: map USB MMIO windows for EFI diagnostics
edbeb36  fix: recognize DWC USB31 in EFI diagnostics
347c097  fix: isolate combo PHY abort in USB diagnostics
a60dd04  fix: skip faulting combo PCS window
30f380d  fix: make USB diagnostics safe without cable
db6c0dd  feat: add staged MAX77705 diagnostics
7119edb  feat: add live USB attach monitor
7e8567c  feat: add guarded DWC3 event probe
ead6639  feat: add guarded Exynos HS PHY attach probe
a87e6f3  fix: force DWC3 high-speed attach probe
1338b05  fix: decode and stop DWC3 attach safely
5a280a1  feat: expose CACHE as guarded USB mass storage
0ae7249  feat: add touchscreen UI for CACHE USB
f4b22f4  fix: fail stop CACHE USB startup errors
5ff0a6f  fix: retry Exynos USB HS PHY startup
```
