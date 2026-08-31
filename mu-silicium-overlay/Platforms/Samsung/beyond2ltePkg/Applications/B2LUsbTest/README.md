# B2LUsbTest EFI Shell USB tanısı

`B2LUSB.EFI`, beyond2lte USB yolunun EFI tanısı ve tek bölümlük kurtarma USB
diskidir. R13 açılış menüsünde `M`, GPT adı `CACHE` olan 600 MiB FAT32 bölümü
Windows'a USB Mass Storage/Bulk-Only Transport olarak sunar; `D` eski aşamalı
tanı ekranlarını açar. R12, eski pasif
harita sayfalarının ardından tek ekranda iki saniyede bir PMU, DWC3 device/OTG,
xHCI portları ve doğrulanmış ana DRD PHY kayıtlarını yeniler. Önce PC veri
kablosu takılıyken, sonra aynı boot içinde kablo çıkarıldıktan sonra bu ekranın
fotoğrafı alınır. R9 ölçümlerinde yalnız DSTS içindeki serbest akan SOFN sayacı
değiştiği için R10 bu alanı delta hesabından çıkarır. Son, ayrı onaylı sayfa
stok Exynos9820 USB2 PHY açılışını geçici uygular, kontrollü DWC3 event-DMA
halkası kurar ve `DCTL.RUN_STOP` ile 30 saniyelik fiziksel attach probu yapar.
R8 MAX77705 yardımcı kaynağı ağaçta korunur fakat R13 uygulamasına bağlanmaz ve
çalıştırılmaz.

Uygulama yeni MMIO pencerelerini tek tek ve yalnız kullanıcı tuşuyla açar:

1. FSYS0A power-domain içindeki doğrulanmış `CONFIG/STATUS/OPTION` değerleri,
2. PMU içindeki USB PHY kontrol değerleri,
3. DWC3 kimliği/globalleri ile xHCI capability, operational ve port durumu,
4. DWC3 device/OTG rol registerleri,
5. USB DRD PHY ana penceresinin doğrulanmış ilk 64 baytı,
6. combo PHY PCS için önceki abort sonucu; MMIO okuması yok,
7. combo PHY PMA için önceki abort sonucu; MMIO okuması yok,
8. tek ekranlı canlı attach monitörü: iki saniyelik örneklerle anlık ve birikmiş
   `PWR/DWC/DEV/OTG/XHC/PHY` değişim bayrakları, DWC3 bağlantı hızı/link state,
   OTG `VBUS/B-session`, xHCI `CCS/PLS/change` ve DRD PHY `0x00..0x3c`.
9. ayrı onaylı aktif HS attach probu: DT'deki USB2 PHY izolasyon biti, vendor
   UTMI açılış/tuning sırası, DWC3 `DCFG.DEVSPD=High-Speed`, ayrılmış 4 KiB DMA
   halkası, maskeli event interrupt, device-event sayaçları ve geçici
   `RUN_STOP`.

R1 cihaz testinde `FSYS0A + 0x14` (`0x15862214`) erişimi synchronous external
abort üretti. R2 bu erişimi ve aynı bilinmeyen duration grubundaki `+0x10`
okumasını yapmaz. FSYS0A ile PMU USB kontrolleri de ayrı kullanıcı onaylarına
bölünmüştür.

R2 cihaz testinde FSYS0A `CONFIG/STATUS=1`, `OPTION=0x10`; PMU
`USBDP/USBPHY=0` okundu. İlk DWC3 erişimi `GSNPSID @ 0x10C0C120` ise
`ESR=0x96000006` ile level-2 translation fault verdi. R3, merged stok DT'nin
etkin DWC3 ve üç PHY aralığını platform `MemoryMapLib` tablosunda non-secure
Device MMIO olarak eşler. DRAM veya reserved-memory bölgelerini değiştirmez.

R3 cihaz testinde `GSNPSID=0x33313130` ile DWC_usb31 çekirdeği ve geçerli xHCI
1.10 capability alanı okundu. Denetleyici `DEVICE` rolde, xHCI halted ve
hatasız; iki port powered fakat bağlantısızdı. Eski doğrulama yalnız
`0x5533xxxx` DWC_usb3 kimliğini kabul ettiği için sonucu yanlışlıkla
`BEKLENMEDIK` saydı. R4, Linux DWC3 çekirdeğindeki `DWC31_IP=0x3331`
kimliğini kabul eder, DWC_usb31 `VER_NUMBER/VER_TYPE` alanlarını gösterir ve
salt-okuma role/PHY aşamalarını açar.

R4-R6 ölçümleri kablo takılı değilken yapıldı. Ana DRD PHY okunabildi; combo
PCS `+0x00/+0x0C` ve combo PMA `+0x00` erişimleri synchronous external abort
verdi. R7 bu iki combo penceresini hiç okumaz. Kablo etkisi önce PC veri
kablosu boot öncesinde takılarak güvenli PMU, DWC3/xHCI ve ana DRD PHY
sayfalarında karşılaştırılır.

R7 kablolu ölçümünde kablosuz ölçüme göre anlamlı bir attach farkı oluşmadı:
PMU USB kontrolleri yine sıfır, DWC3 `DEVICE`, USB3 PHY suspend, iki xHCI
portunda da `CCS=0` ve DRD PHY ilk 64 baytı aynı kaldı. Bu, kablo bilgisinin
DWC3'e ulaşmadığını ve sonraki sınırın HSI2C45/MAX77705 olduğunu gösterir.
R8 bu nedenle GPIO, controller ve aktif veri adımlarını üç ayrı onaya böler.
HSI2C45 clock/timing kalıtımı uygun değilse uygulama MAX77705 transferini
otomatik atlar. Kullanıcı da her onay ekranında `Esc`/`Q` ile durabilir.

R8 cihaz ölçümünde MAX77705 IRQ hattı active-low olarak düşük, HSI2C45 SCL ve
SDA yüksek bulundu; buna karşılık HSI2C45 clock, timing ve FIFO kayıtlarının
tamamı sıfırdı. Aktif MAX77705 veri aşaması bu yüzden güvenli biçimde
başlatılmadı. Çalışma yönü yeniden USB debug'a çevrildi. R9 farklı bootları
karşılaştırmak yerine aynı bootta canlı değişimi yakalar: monitor açıldığında
kablo PC'ye takılı tutulur ve ilk fotoğraf çekilir; kablo çıkarılır, `ORNEK`
sayacı arttıktan sonra ikinci fotoğraf çekilir. `Q` veya `Esc` monitörü kapatır.

R9'un PC, Mac, kablosuz ve şarj adaptörü fotoğraflarında PMU, DWC3 global/OTG,
xHCI port ve DRD PHY değerleri aynı kaldı. Her karede görülen `DEV=1`, DSTS
`SOFN[16:3]` sayacından kaynaklanan sahte deltadır; sabit durumun maskeli değeri
`0x00D20004` idi. R10 monitörü SOFN'yi ayrıca gösterir fakat delta saymaz.

R10 aktif sayfası yalnız şu preflight koşulları sağlanırsa yazmaya geçer:
DWC_usb31 kimliği, device/OTG capability, device current mode, `RUN_STOP=0`,
`DEVCTRLHLT=1`, `DALEPENA=0`, EP0/EP1 command inactive, `DEVTEN=0` ve boş event
count. DMA için `EfiReservedMemoryType` 4 KiB sayfa ayırır; yalnız event adresi,
event size/count, `DEVTEN` ve `DCTL` allow-list üzerinden yazılabilir. PMU, PHY,
rol, reset ve endpoint registerleri yazılmaz. Normal çıkışta controller halt
edilir, kalan olaylar tüketilir, registerler geri yüklenir ve sayfa bırakılır.
İki saniyede halt olmazsa DMA sayfası reserved tutulur ve uygulama Windows'a
geçişi bilerek engeller; cihaz donanım tuşuyla yeniden başlatılmalıdır.

R10 cihaz sonucunda event DMA ve `RUN_STOP` çalıştı, fakat `USB_RESET` ve
`CONNECT_DONE` sıfır kaldı. Tekrarlanan `0x00000901`, DWC3 endpoint 0
`RXTXFIFOEVT` (ekranda `FIFO_EVENT`) olayıdır; fiziksel attach kanıtı değildir.
Bu sırada PMU USB2 PHY kontrolünün sıfır ve ana PHY'nin izolasyonda olması,
bir sonraki sınırın endpoint değil Exynos PHY açılışı olduğunu doğruladı.

R11, merged DT ve Samsung Exynos9820 vendor PHY sürücüsünden çıkarılan dar HS
akışını uygular. Preflight ayrıca ana PHY kimliğini `0x00030000`, PMU USB2 PHY
bitini kapalı ve DWC3 `GUSB2PHYCFG` soft-reset/suspend bitlerini temiz bekler.
Yalnız PMU `0x1586072c[0]` ile ana PHY'nin `LINK_CTRL`, `LINK_PORT`, `CLKRST`,
`UTMI`, `HSP`, `HSP_TUNE` ve `HSP_TEST` kayıtları ayrı runtime allow-list'lerden
yazılabilir. Combo PCS/PMA, USB3 PMU, DWC3 rol/core-reset ve endpoint kayıtları
yazılmaz. Cihaz-modu tune değerleri stok DT'nin ilk hücreleridir: `tx_vref=12`,
`tx_pre_emp=3`, `tx_res=3`, `rx_sqrx=3`, `compdis=7`. Normal çıkışta önce DWC3
halt edilir, sonra PHY güvenli güç-kapama durumuna alınır, PMU izolasyonu ve
kalıtılmış tüm ana-PHY değerleri birebir geri yüklenir.

PHY sırası, `Android-Artisan/android_kernel_samsung_exynos9820` deposunun
`9a0a792d227c8ae6cddac0c9d68a8bb9782c44cb` revizyonundaki
`phy-exynos-usb3p1.c`, `phy-exynos-usb3p1-reg.h` ve
`phy-exynos-usbdrd.c` dosyalarıyla sabitlenmiştir. DT `phy_version=0x301`
olduğu için vendor sürücünün minor-1 reset kolu kullanılır; MMIO `CTRL_VER`
kaydının bununla karıştırılmaması için preflight değeri ayrı tutulur.

R11 de henüz descriptor veya EP0 sunmaz. Başarı ölçütü kablo PC'ye takılıyken
`USB_RESET` ve/veya `CONNECT_DONE` sayacının artmasıdır. `FIFO_EVENT` tek başına
başarı değildir. Fiziksel attach kanıtından sonraki aşama EP0 descriptor
yanıtları, ardından bulk endpoint ve dosya aktarım protokolüdür.

R11 cihaz sonucunda USB2 PHY gerçekten açıldı (`PMU_USBPHY=1`, `SIDDQ=0`) ve
DWC3 çalıştı, fakat `DCFG=0x00080005` içindeki `DEVSPD=5` denetleyiciyi
SuperSpeedPlus modunda bıraktı. Yalnız UTMI/HS PHY açıkken link bu nedenle
`RX_DET` durumunda kaldı; `USB_RESET` ve `CONNECT_DONE` yine sıfırdı. R12,
halted preflight sonrasında yalnız `DCFG[2:0]` alanını `0` (High-Speed) yapar,
sonra PHY/event/RUN_STOP sırasını çalıştırır. DCFG'nin diğer bütün bitleri
korunur ve çıkışta kalıtılmış DCFG değeri birebir geri yüklenir. Endpoint,
core reset, rol ve combo-PHY yazma sınırları değişmemiştir.

R12 cihaz sonucunda Windows fiziksel aygıtı gördü ve “tanınmayan USB aygıtı”
uyarısı verdi. Ham event halkasındaki tekrarlanan `0x00000101` ve `0x00000201`
değerleri sırasıyla device `USB_RESET` ve `CONNECT_DONE` olaylarıdır; R12 ekranı
bit 0 anlamını ters çözdüğü için bunları yanlışlıkla endpoint olayı saymıştı.
Bu sonuç HS PHY, DWC3 soft-connect ve PC veri yolunun çalıştığını; eksik sınırın
EP0 descriptor yanıtı olduğunu kanıtlar. R13 olay ayrımını düzeltir ve vendor
durdurma sırasındaki `DCTL.ULSTCHNGREQ=RX_DETECT` isteğini RUN/STOP öncesinde
ayrı, maskeli RMW yazılarıyla uygular.

R13 `M` yolu EP0 üzerinde device/configuration/string descriptorlerini, MSC
class `GET_MAX_LUN`/reset isteklerini ve fiziksel endpoint 2/3 üzerinde Bulk
OUT/IN sağlar. BOT arkasındaki dar SCSI kümesi Windows'un FAT disk bağlaması
için inquiry, sense, capacity, mode-sense, read/write(10), verify, synchronize
cache, prevent/allow ve start/stop komutlarını içerir. Depolama hedefi yalnız
`EFI_PARTITION_INFO_PROTOCOL` içinde GPT adı `CACHE`, blok boyu 4096, son LBA
153599 ve toplam boyu 629145600 bayt birlikte doğrulanan BlockIo handle'ıdır;
GPT/LBA dışı disk erişimi yoktur. `W` okuma/yazma, `R` salt-okuma seçer.

Firmware FAT sürücüsünün eski cache durumunu yeniden kullanmaması için MSC
oturumu Windows'a dönmez. Windows'ta “Güvenle çıkar” yapıldığında SCSI eject ve
`FlushBlocks` işlenir; DWC3 endpoint transferleri bitirilip RX_DETECT üzerinden
halt edilir, DMA/event/PHY/PMU durumu geri alınır ve ekranda donanım tuşlarıyla
yeniden başlatma istenir. Halt başarısızsa DMA reserved bırakılır ve uygulama
fail-stop ekranından çıkmaz.

MSC taşıması USB-IF'in Bulk-Only sözleşmesindeki 31 bayt CBW, isteğe bağlı veri
aşaması ve 13 bayt CSW sırasını uygular:
[USB Mass Storage Class Bulk-Only Transport 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
Descriptor sabitleri UEFI'nin USB descriptor düzeniyle uyumludur:
[EDK II IndustryStandard/Usb.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/IndustryStandard/Usb.h).

Her sayfa sonraki erişimden **önce** fotoğraflanmalıdır. Bir pencere saat veya
güç kapalı olduğu için sistemi kilitlerse son görülen sayfa erişim sınırını
belirler. `Esc` veya `Q` sonraki pencereyi okumadan uygulamadan çıkar.

Derleme ve paketleme:

```bash
bash yap.sh --touch --usb-test --clean
bash tools_b2ltouch.sh
bash cache_esp.sh --win --system --baseline
```

R3'te BOOT sayfa tablosu ile SYSTEM ESP'deki uygulama birlikte değişir.
Paketleyici ikisini `out/beyond2lte-usb-diag.tar` içinde birleştirir. Odin'de
yalnız bu dosya AP alanından, **Re-Partition kapalı** olarak flashlanır.
Uygulama SYSTEM ESP kökünde `B2LUSB.EFI` adıyla bulunur ve `startup.nsh` dosya
mevcutsa onu Windows'tan önce çalıştırır.

R13 kullanımında açılış menüsünde `M`, ardından dosya yazılacaksa `W` seçilir.
Windows diski gördüğünde CACHE FAT32 üzerinde aktarım yapılır; iş bitince önce
Windows “Güvenle çıkar”, ardından telefonda donanım reboot yapılır. Ekrandaki
`YAZILAN` sayacı artmadan ve güvenli çıkarma tamamlanmadan kablo çekilmez.

## Windows'ta kalıcı dosya aktarımı mimarisi

Microsoft'un Synopsys dual-role mimarisi DWC3 registerlerine doğrudan bağlanan
tek bir özel sürücüden ibaret değildir. ACPI'de URS üst aygıtı; `_ADR=0` host
ve `_ADR=1` function çocukları gerekir. Synopsys denetleyiciler için in-box
`UrsSynopsys.inf`, function controller tarafı için `UfxSynopsys.inf` Include/
Needs yoluyla eşlenebilir. Type-C CC/PD attach ve data-role bilgisi ise donanımı
okuyan ayrı UCM/UcmCx connector client tarafından URS'e bildirilir. Mevcut
`DSDT_minimal.dsl` ve Windows sürücü paketinde bu URS/UFX/UCM düğüm ve
sürücüleri henüz yoktur.

Kaynaklar:

- [USB dual-role controller bring-up](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/dual-role-controller-bringup-for-a-usb-type-c-system)
- [USB dual-role driver stack architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-dual-role-driver-stack-architecture)
- [USB Type-C connector client driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/bring-up-a-usb-type-c-connector-on-a-windows-system)
- [Developing USB function-controller drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-usb-function-controllers)
- [Windows USB Type-C architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/architecture--usb-type-c-in-a-windows-system)
- [Windows UCSI driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/ucsi)

Güncel UFX özeti `UfxSynopsys.sys`, `Ufx01000.sys` ve user-mode istekleri için
`GenericUSBFn.sys` bileşenlerini in-box olarak listeler. Daha eski dual-role
tablosu desktop function-class desteği konusunda daha kısıtlıdır; bu nedenle
hedef Windows ARM64 imajında bu üç bileşenin gerçekten bulunduğu WSL'de ayrıca
doğrulanmadan özel function-class sürücüsü yazılmayacaktır. Dosya aktarımı
hedefi şu sırayla ilerleyecek: R12 fiziksel attach/event kanıtı; Samsung
PHY/Type-C handoff; ACPI URS + Synopsys UFX controller; in-box GenericUSBFn
varsa kontrollü user-mode aktarım, yoksa özel function protocolü. Windows
dokunmatik paketi bu zincirden bağımsız olarak korunur.

UCSI yolu yalnız UCSI-compliant bir embedded controller ve ACPI transport
varsa `UcmUcsiCx.sys`/`UcmUcsiAcpiClient.sys` ile kullanılabilir. MAX77705 için
bu firmware/ACPI sözleşmesini gösteren kanıt bulunmadığından UCSI uyumluluğu
varsayılmaz; mevcut donanım bulguları doğrudan HSI2C45 üzerindeki PMIC/CC-PD
aygıtını gösterdiği için ilk Windows connector adayı özel UCM client'tır.
