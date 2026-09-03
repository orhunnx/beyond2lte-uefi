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

03 Eylül ölçümü bu core resetin gerçekten uygulandığını gösterdi:
`DWC3_VER_NUMBER=0x3137302A` (DWC_usb31 1.70a), `resets=1`,
`CAPLENGTH/HCIVERSION=0x01100020`, `HCSPARAMS1=0x02000240`,
`HCCPARAMS1=0x0228FFED` ve `PAGESIZE=1`. URS parent ve HOST rolü çalışmaya
devam ederken `USBXHCI` yine `Code 10 / STATUS_DEVICE_POWER_FAILURE` verdi;
son durumda `USBCMD=0`, `USBSTS.HCHalted=1` kaldı. `PNP0D15` inbox adayı
custom host INF tarafından outrank edilmiştir; etkin servis yine doğru şekilde
`USBXHCI`'dır. Dolayısıyla kalan sınır temel xHCI capability görünürlüğünden
sonraki reset/DMA/resource/interrupt başlatmasıdır.

`0.1.0.3` tanı turunda ARM64 DMA niteliği host ve function ACPI child'larına
da açık `_CCA=Zero` olarak eklendi; değer parent ile aynıdır ve yalnız URS PDO
miras belirsizliğini kaldırır. Yeni status HCSPARAMS2/3, HCCPARAMS2,
DBOFF/RTSOFF, CRCR, DCBAAP, CONFIG, iki PORTSC ve xECP başlangıcından sınırlı
sekiz DWORD basar. `startnet.cmd` host geçişini USBXHCI+UCX ETW ile sarar,
host child kaynaklarını ayrı kısa rapora alır ve `logman.exe` mevcutsa ETL ile
USB loglarını doğrulanmış CACHE köküne kopyalar. Bu tur fiziksel başarı iddiası
değil; USBXHCI'nin DMA ringlerini programlamadan mı yoksa interrupt aşamasında
mı döndüğünü tek boot ile ayırmak içindir.

USB çalışmadan CACHE içindeki ETL'nin dışarı alınamayacağı test sözleşmesine
işlendi. `--usb-test` finalinde konsol temizlenir ve ayrı host resource raporu
ile register özeti ekranda kalır; bir sonraki iterasyon yalnız bu ekranın
fotoğrafıyla yapılabilir. Hata veren native OSK otomatik açılmaz ve tanının
üstünü kapatmaz; touch kurulumu ile `b2lmt.exe` pakette kalır.

Son `0.1.0.3` cihaz fotoğrafı capability tablosunu daha da doğruladı:
`DBOFF=0x4A0`, `RTSOFF=0x440`, iki `PORTSC=0x2A0` (power-on/RxDetect) ve ilk
xECP blokları okunuyor. Buna karşılık başarısız başlangıcın son durumunda
`CRCR=0`, `DCBAAP=0`, `CONFIG=0`; USBXHCI'nin kalıcı DMA ring kurulumu yok.
`pnputil /resources` raporunda memory/interrupt satırı görünmedi, ancak Code 10
sonrası PnP kaynakları serbest bırakılmış olabileceğinden bu çıktı URS aktarım
hatası için kesin kanıt sayılmadı.

`0.1.0.4` bu belirsizliği USB gerektirmeden kaldırmak için host PDO'ya pasif
`b2lusbmon.sys` KMDF lower-filter ekler. Filtre donanıma yazmaz ve callback'ler
sonunda başarı döndürür; USBXHCI'ye gelen gerçek raw/translated kaynak
sayılarını, ilk memory base/length'i, interrupt vector/level/flags değerlerini
ve Add/PrepareHardware/D0/Release aşamalarını device registry'sinde kalıcı
tutar. Aynı filtre 64 KiB common buffer'ı önce 64-bit, ardından 32-bit duplex
profil ile dener; iki NTSTATUS/device-logical adresini kaydeder ve buffer'ları
donanıma vermeden hemen serbest bırakır. Bu canary, normal BCD'de kapalı olan
eski `azram/guvenli1g` deneylerinden bağımsız olarak, hâlâ aktif MemoryMap banka
guard'ları altında 4 GiB altı DMA tahsisinin mümkün olup olmadığını ölçer.
Guard'lar değiştirilmez. `startnet.cmd` recursive registry özetini final ekrana
basar ve CACHE'e aktarır. xECP read-only snapshot'ı da 8'den 12 DWORD'a
genişletildi; sıradaki
fotoğraf hem `0x10C00000/0x10000` + GSIV 273 teslimini hem USB3 Supported
Protocol capability'sini gösterecek.

03 Eylül depo denetimi iki platform kopyasının eş olmadığını da ortaya çıkardı:
kökteki `beyond2ltePkg/` eski ve guardsızdır, fakat `yap.sh` onu kullanmaz.
Gerçek derleme girdisi `mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/`
altındadır ve alt/ilk üst/son üst banka guard'ları burada aktiftir. Git
geçmişinde guard'ların kaldırıldığı bir değişiklik yoktur; `DEVIR.md` içindeki
kaldırma maddesi uygulanmamış deney planı olarak kalmıştır. WinPE'nin kararlı
açılışı bu korumaya bağlı olduğundan USB turunda bellek haritası korunur.

`0.1.0.4` cihaz ölçümü guard/DMA ayrımını sonuçlandırdı. KMDF canary'nin
64-bit enabler ve buffer çağrıları başarı döndürdü; device-logical adres
`0x00000009_7FE9F000` oldu. Ayrı 32-bit profil de başarıyla
`0x00000000_CFEFF000` adresini aldı. Dolayısıyla guard'lar açıkken 4 GiB altı
64 KiB common buffer sağlanabiliyor; banka guard'ları mevcut USBXHCI başlatma
hatasının doğrudan nedeni değildir ve kaldırılmayacaktır.

Aynı ölçüm yeni ve daha erken bir sınır gösterdi: host child bu turda
`Code 14 / CM_PROB_NEED_RESTART` kaldı. Filter'ın `AddSeen=1` ve
`DmaProbeSeen=1` kayıtlarına karşılık `PrepareSeen=0`, `D0EntrySeen=0` olması,
binary'nin yüklendiğini fakat filter eklenen mevcut devnode'un StartDevice
aşamasına geçirilmediğini kanıtlar. `0.1.0.5` cihaz testi ayrıca sıralama
hatasını görünür kıldı: parent `NONE` isteğini başarıyla aldı ve
`requested=NONE` yazdı, fakat Code 14 durumundaki HOST child kaldırılamadığı
için 10 saniye sonunda `current=HOST` kaldı. Akış burada çıktığından hedefli
PnP restart satırına hiç ulaşılmadı.

`0.1.0.6` bu bağımlılığı tersine çevirir. Otomatik test rol olayı göndermeden
önce zaten var olan `URS\BYN0002&HOST` devnode'una yalnız hedefli
`pnputil /restart-device /deviceid` uygular. Komutun stdout/stderr ve dönüş
sonucu ayrı `B2LUSB-HOST-RESTART.TXT` kaydında final ekrana ve CACHE'e taşınır;
başarısızlık halinde de ETW, kaynak ve filter kayıtları toplanır. `/reboot` ve
`/force` kullanılmaz. Amaç Code 14'ü temizleyip `PrepareHardware` aşamasının
ilk kez çalışıp çalışmadığını ölçmektir; bu turda guard veya MMIO yazımı
değiştirilmez.

`0.1.0.6` cihaz sonucu hedefli komutun doğru devnode'a ulaştığını, fakat
PnPUtil'in `System reboot is needed to complete configuration operations!`
döndürdüğünü gösterdi. Child yine Code 14 kaldı; lower filter kaydında
`AddSeen=1`, `PrepareSeen=0` ve `D0EntrySeen=0` değişmedi. Böylece runtime
restart/rol çevrimi ekseni kapandı: çalışan WinPE oturumuna yeni filter
bağlamak reboot gerektiriyor.

`0.1.0.7`, parent ve host INF'lerini Microsoft DISM ile `boot.wim` içindeki
DriverStore'a offline ekler. Eski aynı adlı paketler önce offline imajdan
kaldırılır, yeni `oem*.inf` kayıtları geri okunarak doğrulanır ve yalnız
başarılı mount/commit sonrasında WSL'deki WIM değiştirilir. `startnet.cmd`
artık USB INF kurmaz; ilk PnP stack'i filter hazırken oluşur. Aynı altyapı
touch `0.2.0.1` paketini ve mevcut `vhf.sys` için demand-start servis kaydını
da offline hazırlar. Standart `--touch --usb-test` turunda ikisi tek DISM/UAC
işleminde eklenir.

`0.1.0.7` cihaz ölçümü offline staging'i iki tarafta doğruladı. Touch, WinPE
açıldığı andan itibaren çalıştı; VHF HID Device ile HID-compliant touch screen
devnode'ları `Started` oldu. USB HOST child için hedefli PnP restart da bu kez
`Device restarted successfully` döndürdü ve önceki Code 14 kapandı. Buna
rağmen `USBXHCI` yine `Code 10 / 0xC000009E (STATUS_DEVICE_POWER_FAILURE)`
verdi. Lower filter `AddSeen=1` ve iki DMA canary başarısı kaydederken
`PrepareSeen=0`, `D0EntrySeen=0` kaldı. Son xHCI snapshot'ında `USBCMD=0`,
`USBSTS.HCHalted=1`, `CRCR=0`, `DCBAAP=0`, `CONFIG=0` görüldü. Artık sorun
runtime INF/restart, MemoryMap guard veya DMA tahsisi değil; USBXHCI'nin
fonksiyon sürücüsü başlangıcında, lower filter `PrepareHardware` çağrılmadan
önce dönen erken bir controller başlangıç hatasıdır.

`0.1.0.8`, bu kısa ve sonradan sıfırlanan register hareketini tek fotoğrafla
ayırt etmek için parent sürücüye 12 saniyelik salt-okuma xHCI gözlemcisi ekler.
`startnet.cmd`, HOST child restartından hemen önce `b2lusbctl.exe observe`
çağırır. Yüksek çözünürlüklü KMDF timer yalnız `USBCMD`, `USBSTS`, `CRCR`,
`DCBAAP` ve `CONFIG` okur; HCRST, CNR, Run, HCHalted-clear, host-system-error ve
ring/DCBAA/config programlamasının herhangi bir örnekte görülüp görülmediğini
son status çıktısında özetler. Timer 12 saniye sonunda kendini durdurur;
controller, PHY, PMU ve bellek guard'larına yeni yazım eklenmemiştir.

`0.1.0.8` cihaz ölçümünde gözlemci 7.878 örnek boyunca hiçbir geçiş görmedi:
ilk/son/OR değerleri `USBCMD=0`, `USBSTS=1`, `CRCR=0`, `DCBAAP=0` ve
`CONFIG=0` kaldı. HCRST, Run, HCHalted-clear, CNR, HSE ve ring/DCBAA/config
bayraklarının tamamı sıfırdı. Hedefli restart başarılı olduğu halde child yine
`Code 10 / 0xC000009E` oldu. Dolayısıyla USBXHCI controller registerlerini hiç
programlamadan, lower-filter `PrepareHardware` almadan önce çıkıyor.

`0.1.0.9`, bunun öncesindeki PnP kaynak sözleşmesini salt-okuma biçiminde
yakalar. Lower-filter resource-requirements IRP'sinin aşağı/yukarı geçişlerini,
logical-configuration ve descriptor sayılarını, ilk MMIO min/max/length ile
interrupt min/max/flags alanlarını `B2LMonReq*` değerlerine yazar. Ayrıca
`EvtDeviceRemoveAddedResources` çağrısının görülüp görülmediğini ve bu aşamadaki
raw/translated descriptor sayılarını kaydeder. Callback'ler descriptor eklemez,
silmez veya güncellemez; MMIO ve MemoryMap guard'larına dokunmaz. Sonraki cihaz
ölçümü `0x10C00000/0x10000` ile GSIV 273 child gereksiniminde yoksa URS kaynak
aktarımını, ikisi de varsa inbox USBXHCI'nin daha sonraki erken kontrolünü hedef
gösterecektir.

Aynı `0.1.0.9` test görüntüsü artık `setupapi.dev.log` içinden yalnız BYN0002,
HOST child, USBXHCI ve `CM_PROB_FAILED_START / 0xC000009E` ile ilişkili satırları
final konsola taşır. Bu özet kaynak logu değiştirmez ve CACHE dışa aktarımına
bağlı değildir; USB hâlâ çalışmıyorken Code 10'un SetupAPI bağlamı fotoğrafla
alınabilir.

`0.1.0.9` cihaz ölçümünde requirements aşağı geçişinde DWC3 MMIO descriptorı
tam aralıkla (`0x10C00000-0x10C0FFFF`, length `0x10000`) görüldü. Yukarı
dönüşte descriptor sayısı iki, atanmış raw ve translated kaynak sayıları da
iki oldu. Böylece URS host child kaynak aktarımı ve safe-memory guard hipotezi
kapandı; USBXHCI hâlâ MMIO'yu programlamadan Code 10 döndürüyor.

`0.1.0.10`, aynı pasif gözlemciyi USBXHCI'nin upper-filter'ı yapar ve yalnız
`IRP_MN_START_DEVICE` için preprocess/completion gözlemi ekler. Completion
rutininde IRP status'u değiştirilmez; sonuç pasif bir work item üzerinden
registry'ye yazılır. SetupAPI Code 10 özeti final ekrana alınır, `tracerpt.exe`
varsa mevcut USBXHCI/UCX ETL de hata/status satırlarına indirgenir. Cihaz
ölçümünde upper-filter `START_DEVICE` isteğini gördü ve completion
`0xC000009E (STATUS_DEVICE_POWER_FAILURE)` döndü; `PrepareSeen=0` ve
`D0EntrySeen=0` kaldı. Aynı anda raw/translated atanmış descriptor sayıları
iki, DMA64/DMA32 common-buffer probları başarılı ve xHCI operasyonel register
geçişleri sıfırdı. Böylece hata kaynak atamasından sonra USBXHCI'nin kendi
START_DEVICE işleyişinde, upper-filter'ın KMDF PrepareHardware evresinden önce
sınırlandı.

Statik vendor karşılaştırmasında `0.1.0.9` ölçümündeki
`GUSB3PIPECTL=0x01021102` değerinin `SUSPHY` bitini açık bıraktığı görüldü.
Hem upstream Synopsys rol değiştirme yolu hem de sabitlenmiş Exynos9820
`dwc3_core_soft_reset()` sırası, core/rol başlangıcından önce USB2 ve USB3
`SUSPHY` isteklerini temizliyor. `0.1.0.11` bu iki standart DWC3 bitini
temizleyip readback ile doğrular. USB3 wrapper portu kapalı kalır; daha önce
external abort veren combo-PHY/PMA adresleri, güvenli bellek guard'ları ve
ACPI kaynakları değişmez. Aynı paket, USBXHCI başlamadan serbest bırakılan raw
ve translated listelerin ilk MMIO ve IRQ ayrıntılarını da salt-okuma biçiminde
`B2LMonAssigned*` değerlerine kaydeder. Böylece aynı tur hem START_DEVICE
sonucunu ve gerçek kaynak teslimini ölçer hem de kanıtlanan DWC3 handoff farkını
giderir.

UFS bu değişikliğe bilinçli olarak dahil edilmedi. Mevcut runtime UFS kurulumu
CACHE yazma/geri-okuma testini başarıyla geçiriyor ve `mountvol /N` sonrasında
çalışıyor. UFS'yi daha erken PnP'ye taşımak Android RAW bölümlerinin harfleri
tüketmesine ve çalışan depolama/boot sırasının değişmesine yol açabilir.

`0.1.0.12` sonraki flaş için mimariyi sabit USB2 HOST'a çevirir. Depodaki stok
Android DTB'lerinde `exynos-usbdrd-phy` platformu, `dwc3` ve `xhci`'den önce
başlatılır; DWC3 `0x10C00000/0x10000`, SPI 241 (Windows GSIV 273) ve ana PHY
`0x10B00000` olarak tarif edilir. Windows ACPI düzeni artık aynı sırayı izler:
`ACPI\BYN0002` PHY/PMU ve HOST handoff'unu tamamlar, gerçek `ACPI\BYN0003`
çocuğu DWC3+IRQ kaynaklarıyla doğrudan inbox USBXHCI'ye bağlanır. Önceki
sentetik URS child ve farklı-build UrsCx runtime bağımlılığı kaldırılmıştır.
Vendor `_HID` yanında debug capability bulunmayan standart SoC xHCI için
`_CID="PNP0D15"` yayınlanır; eski URS host child'dan kalan `_ADR=0` kaldırılır,
çünkü adres sıfır USBXHCI'nin oluşturacağı root hub'a aittir.
PMU/SIDDQ readback doğrulaması bounded poll ile yapılır. MAX77705 yazmaları
depodaki güvenli salt-okuma sınırı nedeniyle eklenmemiştir; VBUS için ilk test
hala haricen beslenen USB2 hub ile yapılmalıdır. Offline DriverStore ile ilk
boot'ta kurulan statik child otomatik olarak yeniden başlatılmaz; bu sayede
başarılı olabilecek root-hub/HID zinciri tanı uğruna kesilmez. Paketleyici
hedef WIM'de `HIDUSB`, `MOUHID` ve `MOUCLASS` dosyalarını da zorunlu tutar;
çalışan sistemde USB, HIDClass ve Mouse PnP sonuçları ayrı kaydedilir.

`0.1.0.12` cihaz kanıtında platform ve host child `Started` oldu; USBXHCI
çalışır duruma (`USBCMD.RUN=1`, `USBSTS.HCH=0`) geçti ve inbox
`USB\ROOT_HUB30`/`usbhub3.inf` root hub'ı numaralandırıldı. Bu, Code 10'un
kapanıp Windows host zincirinin çalıştığını doğrular. Aynı boot'ta iki port da
`PORTSC=0x2A0` gösterdi: port power açık, link `RxDetect`, fakat `CCS=0` ve
`PED=0`. Kalan sınır controller başlangıcı değil, konektörde elektriksel attach
ve VBUS/CC rol zinciridir. `0.1.0.13`, çalışan child'ı restart etmeden korur;
root hub/alıcı PnP özetini ve PORTSC alan çözümünü final ekrana ekler. Donanım
yazma yolu değişmediğinden ilk alıcı testi haricen beslenen USB2 hub ile
yapılmalıdır.

Stok Android dosyaları VBUS sınırını da açıklıyor: DWC3 ve PHY düğümlerinde
`is_not_vbus_pad=1` bulunuyor; rol/attach zinciri `max77705_usbc_init` ve
`usb_notifier_init` üzerinden, `hsi2c_45` üzerindeki `max77705@66` ile
kuruluyor. Depodaki HSI2C45 ölçümünde clock/timing/FIFO registerleri sıfır
olduğu ve doğrulanmış bir OTG-source komut dizisi bulunmadığı için Windows
sürücüsüne tahmini PMIC/I2C yazması eklenmedi.

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
- Microsoft USBXHCI/UCX ETW yakalama yöntemi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/how-to-capture-a-usb-event-trace>
- Microsoft KMDF high-resolution timer sözleşmesi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdftimer/ns-wdftimer-_wdf_timer_config>
- Microsoft KMDF resource-requirements akışı:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/introduction-to-hardware-resources>
- Microsoft KMDF FDO resource callback sözleşmesi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdffdo/ns-wdffdo-_wdf_fdo_event_callbacks>
- Microsoft KMDF WDM IRP preprocess/completion sözleşmesi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/preprocessing-and-postprocessing-irps>
- Microsoft `Code 14 / CM_PROB_NEED_RESTART` açıklaması:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/install/cm-prob-need-restart>
- Microsoft hedefli `pnputil /restart-device` sözdizimi:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax>
- Microsoft offline imaja DISM ile sürücü paketi ekleme:
  <https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/add-and-remove-drivers-to-an-offline-windows-image>
- Microsoft ARM/ARM64 ACPI `_CCA` common-buffer davranışı:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pallocate_common_buffer_ex>
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
