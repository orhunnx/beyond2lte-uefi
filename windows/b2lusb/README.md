# Beyond2LTE Windows ARM64 USB bring-up

## Güncel kaynak: `.40` / gerçek opcode yanıtının dağıtımı

MAX `0.1.0.40`, parent/host/function `0.1.0.19`, IOCTL ABI 7.
Koşulsuz HOST kaldırıldı. Gerçek MAX CC/PD değişiklikleri ayrı pasif UCM
kuyruğuna, oradan URS host/function yaşam döngüsüne gider. ACPI rol çocukları
`_ADR=0/1`, PnP kimlikleri `URS\BYN0002&HOST/FUNCTION`; özgün GSIV273.
`status`, `observe`, `maxevents`, `maxscreen` tanı komutlarıdır; `host` yoktur.

Gerçek PDMSG/PS_RDY kesmeleri stok sırada işlenir; hard-reset/source-timeout
VBUS off ve 760/800 ms gecikmeli on işlemini kullanır. On öncesi gerçek
SOURCE rolü yeniden kontrol edilir. Timer CC/PD polling yapmaz. SINK PS_RDY,
APCMD ile tamamlanan `CURRENT_SRCCAP=0x30` / 31 bayt okumasını kuyruğa alır.
Bağlantı nesli eski PDO yanıtının yeni kabloya aitmiş gibi yayımlanmasını
engeller; hatalı başlık ve başarısız istek görünür kalır.

Gerçek SYSMSG IRQ'si üç canlı okuma ile doğrulanır. `APCMD_FAIL=33`
bekleyen aynı opcode'u tekrar gönderir; tamamlanma için gerçek APCMD gerekir.
Ardından stok AFC clock düzeltmesinin dört I2C yazısı uygulanır. Her yazının
hatası ve TKEY relock sonucu korunur. Eski bir bekleme süresinin dolması,
gerçek retry'nin uzattığı deadline'ı yanlışlıkla bitirmez.

HWC1 stok kernelinin gömülü IKCONFIG'i doğrulandı: GRL/AFC açık, PDIC_PD30
kapalı. GRL dalındaki `70`, genel firmware queue-clear olarak kullanılamaz.
POR/WDT, gerçek SYSMSG ile stok reset/mask/init sırasını uygular. Probe
istekleri artık sürücüye ait slotlardadır; reset bunları iptal ederken dönen
bir stack isteği yeni kuyruğu bozamaz. `54/0c` init yanıtları asenkron alınır.
GRL `70 01` yanıtından sonra `62:1f=04` trim yazılır. Reset sırasında
okunan gerçek UIC bitleri sonraki USBC IRQ'da, ham frame'den ayrı dağıtılır.
Yazılım queue-clear, bekleyen MUIC/PD isteklerini iptal eder; başarılı yanıt
veya detach üretmez. Recover listesi yalnız gönderilmemiş `0c/54/55`
yazılarını içerir; GRL derlemesinde firmware queue-clear gönderilmez.

Gerçek CC no-connection dalı eski MUIC/PD/control komutlarını VBUS-off
öncesinde iptal eder. Sonraki OPEN kendi gerçek yanıtıyla doğrulanır. Probe
okuması veya aksesuar durumu bu temizliği üretmez. İptal edilen başlangıç
grubu `INIT_NT=C0000120 / INIT_DONE=0` kalır; recover tamamlanması iptal
edilmiş başlangıç adımlarına başarı üretmez. `DETACH_CLEARS` bu dalın,
`QUEUE_CLEARS` tüm reset/GRL/detach temizlemelerinin sayısıdır.

Gerçek opcode yanıtı artık beklenen komuttan ayrı dağıtılır. Uyuşmazlık
tek istekte hata olarak kalırken stok sıra sonraki komuta ilerler; gerçek
I2C/trim hatası yine kuyruğu iptal eder. GRL gerçek `70` yanıtında bir kez
çalışır. PDO için eksik baytlar üretilmez; MUIC WRITE hatası sonraki doğru
READ ile başarılı sayılmaz. Eksik gerçek tüketiciler `RX_UNHANDLED` ile
bildirilir. `ALT_ACK` manager READY veya DP anlaşması değildir.

`MAX FOTO v13` genel/USB/MUIC/UCM, 35 satırlık PD/PDO, 24 satırlık
SYSMSG ve 32 satırlık başlangıç/reset/GRL sayfasının ardından 29 satırlık gerçek opcode yanıtını gösterir.
Beklenen/alınan opcode, tek istek ve tüketici sonuçları ayrıdır. SYS_VALID/READ_N,
retry sonucu ve AFC relock alanları farklı kanıt katmanlarıdır.
`CAPS_VALID` ham PDO okuması ve sayı/seçim başlığının geçerliliğidir; RDO,
PDO güç semantiği veya OS PD sözleşmesi doğrulaması değildir. UCM kabulü de
enumeration kanıtı değildir. Source Rp, OS PD swap/sözleşmeleri,
alt mod READY/VDM tüketicisi, battery/CHGINSEL, AFC/RAM response
tüketicileri, function sınıfı ve USB3
hâlâ açık. `RESET_NT=0` reset sırasının, `INIT_NT=0 / INIT_Q=0` mevcut
init/kurtarma grubunun tamamlanmasıdır. `ALT_START_REQ=1` alt modun
hazır olması değildir. Reset eski readback doğrulamalarını geçersiz kılar;
`57/0b/1c` tekrar gönderilmediğinde önceki başarı bayrakları geri üretilmez.
Bu sürüm USB çalıştı iddiası taşımaz; kaynak işi flaş beklemeden devam eder.

Build yalnız WSL: `bash yap.sh --touch --usb-test --clean`, ardından
`bash cache_esp.sh --win --system --baseline`. Hedef WIM'de aynı Windows
build'inin ARM64 UCM/URS/UFX bileşenleri gerekir. Function INF gerçek inbox
bölümlerinden üretilir; eski Mobile runtime veya tahmini INF adı kullanılmaz.
Ayrıntı ve test kapsamı: `specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md`, `.40`.

**Aşağıdaki `.34` ve statik `.18` bölümleri önceki ölçüm/tasarım günlüğüdür;
statik HOST komutları ve BYN0003 kimliği güncel build için kullanılmaz.**

**5 Eylül / MAX 0.1.0.34:** Gerçek UIC/BC ve PD FCTID/RID sonuçlarıyla
USB/CDP/OTG/şarj sınıflandırması eklendi. UFP artık koşulsuz OPEN yapmaz;
USB/CDP analog yolu gerçek APCMD/read-back ile doğrulanır. Stok duplicate
OTG yeniden yazısı ve TA `write_vps_regs` no-op davranışı korunur.
`MAX FOTO v7`: genel 40 satır, ayrı MUIC/RID sayfası 34 satır. RAW_VALID,
CLASS_VALID, DETECT_NT, PATH_NT ve DATA_NT farklı kanıt katmanlarıdır.
UCM/URS/function ve özel PD/charger/USB3 kolları hâlâ eksik; WSL/telefon
sonucu yoktur. Ayrıntı Linux karşılaştırmasının `.34` bölümündedir.

**5 Eylül / MAX 0.1.0.33:** BYP/APCMD/WATER/CC/PD uygulanan işleyicileri
özgün MAX pasif ISR sırasında çalışır. Runtime MUIC komutları asenkron;
tanı işi veya APCMD yanıtı BYP/CC/PD'yi bekletmez. İlk CC/su/PD kontrolü
GPIO unmask öncesine taşındı. `MAX FOTO v6` 40 satır, MUIC_NT/Q/OK ile
istek ve gerçek read-back sonucunu ayırır. Tek atımlık timeout yalnız hata
iptali yapar, donanım olayı üretmez. UCM/URS host/function, PD/SYSMSG
recovery ve USB3 açık; WSL/WDK/telefon doğrulaması yapılmadı. Ayrıntılar
Linux karşılaştırmasının başındaki `.33` bölümündedir.

**5 Eylül / MAX 0.1.0.32:** CC SOURCE yalnız güç rolünü/VBUS'u yönetir;
MUIC USB_AP gerçek PD DATAROLE=DFP olayına bağlıdır. Güç/veri rolleri,
WATER kapısı, BYP aşırı akım-off ve Linux VBUSDet detach kontrolü eklendi.
`MAX FOTO v5` 40 satırda PWR_ROLE/DATA_ROLE/DATA_NT gösterir. UFP function
yolu henüz uygulanmadığı için açıkça STATUS_NOT_SUPPORTED raporlanır.
Dinamik DWC3/xHCI ve asenkron notifier/kurtarma işleri hâlâ açık; WSL/WDK
ve telefon doğrulaması yapılmadı. Ayrıntılar karşılaştırmanın `.32` bölümünde.

**5 Eylül / MAX 0.1.0.31:** Özgün GPA1-6 / GSIV51 pasif kesmesi ve APCMD
istek/yanıt kuyruğu eklendi; bağlantı/opcode polling kaldırıldı. ACPI BYN0004
beş MMIO + iki IRQ ister (HSI2C114, MAX51). `MAX FOTO v4` genel sayfası
37 satırdır; MAX_GSIV/IRQ/NT, OP_DONE ve MFD_FAULT alanlarını içerir.
Normal probe PD/VDM maskeleri 1c/ff korunur. Initial SOURCE kontrolü VBUS
başlatmaz; gerçek CCSTAT gerekir. WSL/WDK ve telefon doğrulaması yapılmadı.
PD/charger hata işleyicileri ve dinamik DWC3/xHCI rol zinciri hâlâ eksiktir;
statik HOST profili Linux akışının tamamı değildir. Ayrıntılar Linux
karşılaştırma dosyasının başındadır; aşağıdaki sürüm notları tarihseldir.

**5 Eylül / MAX 0.1.0.30:** HSI2C45 transferi özgün GSIV114 üzerinden
ISR/DPC ile yürür; pasif FIFO polling yolu kaldırıldı. ACPI ve SYS birlikte
güncellenmeli. `MAX FOTO v3` I2C_GSIV/IRQ/DONE gösterir. Gerçek MAX GPIO
kesmesi ve MAX -> DWC3 rol zinciri sıradaki eksik işlerdir; `.30` henüz WSL
ve cihazda doğrulanmadı. Aşağıdaki `.29` notu tarihsel değişiklik kaydıdır.

**5 Eylül / MAX 0.1.0.29:** HSI2C FIFO tüketimi ve Linux'taki
`CC_SOURCE -> VBUS -> MUIC USB_AP` sırası düzeltildi. Yeni `MAX FOTO v2` ve
`USB FOTO v2` sayfaları RX hatalarını, port bağlantısını ve USB/HID kimliklerini
klavyesiz gösterir. `b2lusbctl filter` WinPE'deki eksik findstr bağımlılığını
kaldırır. WSL build ve telefon doğrulaması bekleniyor. Gerçek IRQ ve MAX'tan
DWC3/xHCI'ye rol bildirimi hâlâ eksik; aşağıdaki statik HOST uygulaması tam
Linux akışı olarak kabul edilmiyor. Güncel kanıt ve açık işler:
[Linux karşılaştırması](../../specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md).

Bu dizin Exynos9820 `DWC_usb31` denetleyicisini sabit USB2 HOST olarak
Windows'un inbox xHCI yığınına bağlayan güvenli bring-up paketidir.

## Bu sürümün kesin kapsamı

- `b2lusb.sys`, `ACPI\BYN0002` üzerinde KMDF 1.15 platform sürücüsüdür; PHY'yi
  açar ve DWC3'ü HOST moduna getirir.
- ACPI üst aygıtı ana DRD PHY (`0x10B00000`) ile USB2 PHY izolasyon bitini
  (`0x1586072C`) taşır. Ayrı `ACPI\BYN0003` çocuk aygıtı DWC3
  (`0x10C00000`) ve GSIV 273 kaynaklarının tek PnP sahibidir.
- `ACPI\BYN0003`, `b2lusb-host.inf` aracılığıyla Microsoft
  inbox `USBXHCI.SYS` yığınına bağlanır. USB klavye/fare için özel HID sürücüsü
  yazılmaz; başarılı xHCI/root-hub numaralandırmasından sonra Windows'un
  `USBHUB3` ve HID class sürücüleri kullanılır.
- Host child vendor `_HID="BYN0003"` yanında debug capability bulunmadığını
  belirten standart `_CID="PNP0D15"` kimliğini yayınlar. Önceki URS host-child
  düzeninden kalan `_ADR=0` statik host üzerinde kullanılmaz; adres sıfır,
  USBXHCI'nin numaralandıracağı root hub'a aittir.
- `b2lusbmon.sys`, yalnız bring-up tanısı için host child altında çalışan,
  MMIO'ya yazmayan pasif KMDF upper-filter'dır.
- `b2lmax77705.sys`, `ACPI\BYN0004` üzerinde stock HSI2C45'i tek sahibi
  olarak açar. Samsung MFD sırasıyla TOP (`0x66`), USBC (`0x25`) ve charger
  (`0x69`) istemcilerini doğrular; CC state `SOURCE` olduğunda Samsung'un
  CNFG_00/INT_MASK geçişiyle VBUS/OTG'yi açar. Ayrı PD DATAROLE=DFP olayı
  `CONTROL1_WRITE(COM_USB=0x09)` ile D+/D- hatlarını AP USB PHY'ye bağlar
  ve `CONTROL1_READ` ile yolu doğrular. Ayrılmada önce VBUS'u kapatır ve sonra
  `COM_OPEN=0x3f` readback'ini zorunlu tutar.
- `b2lusbctl.exe status|observe|host`, durum/gözlem ve idempotent HOST yeniden
  hazırlama aracıdır. Bu sürümde DEVICE/NONE rol değişimi bilerek kapalıdır.
- Samsung vendor USB2 PHY sırası ve device/host tune sütunları uygulanır.
  Kaynak revizyonu:
  `Android-Artisan/android_kernel_samsung_exynos9820@9a0a792d227c8ae6cddac0c9d68a8bb9782c44cb`.

Bu sürüm bilerek **USB2-only** çalışır. `LINK_PORT.U3_DISABLE` set edilir;
`0x10AE0000` combo PCS ve `0x10AF0000` combo PMA ne ACPI'ye verilir ne de
sürücüden okunur. Güncel cihaz ölçümünde bu pencereler synchronous external
abort ürettiği için onları açan bir paket güvenli değildir.

## WSL derleme ve paketleme

Normal tam tur:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

`--touch` mevcut dokunmatik profilini korur; hem `--touch` hem de `--usb-test`
ACPI tablolarını aynı turda yeniden derler. `--usb-test`, UEFI `B2LUSB.EFI`
çıktısına ek olarak `tools_b2lusb.sh` çağırır.
Bu açık test seçimi ayrıca WIM'e `auto-host.test` işareti koyar. HOST hazırlığı
parent `PrepareHardware` içinde, ACPI child başlatılmadan önce otomatik yapılır;
işaret rol değiştirmez. WinPE işaretli turda xHCI gözlemini kurar fakat gerçek
`ACPI\BYN0003` child'ını ilk-boot durumunda bırakır; hedefli restart çalışan
root-hub/HID zincirini kesmesin diye uygulanmaz. USB, HIDClass ve Mouse PnP
sonuçları ayrı dosyalara yazılır. Normal doğrudan `tools_b2lusb.sh`
çağrısı bu otomatik tanı/tarama turunu açmaz; gerektiğinde
`B2L_USB_AUTO_HOST=1` açıkça verilir. `--usb-test` turunda dahi
`B2L_USB_AUTO_HOST=0` ile kapatılabilir.

İlk cihaz çalıştırmasında parent `Code 31`, problem status `0xC0000079`
(`STATUS_INVALID_SECURITY_DESCR`) verdi. Bunun nedeni güvenlik tanımı
atanmadan önce URS FDO için ad özelliğinin ayarlanmamasıydı. Sürücü artık
`WdfDeviceInitSetCharacteristics(..., FILE_AUTOGENERATED_DEVICE_NAME, ...)`
çağrısını SDDL atamasından önce yapar. Ayrıca USB kurulumu/otomatik HOST
başarısız olsa bile `startnet.cmd` touch ve UFS kurulumlarına devam eder.

İkinci cihaz ölçümünde parent `Started` oldu, URS rol callback'i
`requested=HOST current=HOST last_ntstatus=0` sonucunu verdi ve host child
oluştu. Ancak `USBXHCI` child `Code 10`, problem status `0xC000009E`
(`STATUS_DEVICE_POWER_FAILURE`) ile durdu; xHCI `USBSTS.HCHalted` set kaldı.
Bu, rol isteğinden sonraki DWC_usb31-to-USBXHCI handoff aşamasını işaret eder.
Sürücü artık PHY açıldıktan ve eski controller halt edildikten sonra
`GCTL.CORESOFTRESET` bitini 100 ms tutarak DWC_usb31 internal clocklarını
senkronize eder, biti temizler ve ancak ardından `PRTCAP` rolünü yazar.
Bu süre DWC_usb31 rol değişimi için upstream DWC3 akışındaki muhafazakâr
bekleme ile aynıdır. Durum çıktısı ayrıca core reset sayısını, DWC3 version/
GUSB PHY ayarlarını ve xHCI capability/command/status/page-size değerlerini
gösterir. Otomatik test, host child'ın servis/stack/driver/resource ayrıntısını
da kaydeder; böylece sonraki hata reset mi interrupt mı ayrıştırılabilir.

Üçüncü cihaz ölçümü core-reset yolunu doğruladı: DWC_usb31 version registeri
`0x3137302A` (1.70a), `resets=1`, xHCI capability `0x01100020` ve
`PAGESIZE=1` okundu. Buna rağmen host child yine `Code 10 / 0xC000009E`
verdi; son snapshot `USBCMD=0`, `USBSTS.HCHalted=1` gösterdi. Bu sonuç rol,
MMIO erişimi ve temel capability başlığından sonraki USBXHCI başlatmasına
daralır; tek başına son `USBCMD` değeri controller resetine hiç yazılmadığını
kanıtlamaz.

`0.1.0.3`, ARM64 DMA politikasını URS parent'tan devralmaya bırakmamak için
aynı non-coherent `_CCA=Zero` niteliğini host/function ACPI çocuklarında da
açıkça tekrarlar. Durum çıktısı HCSPARAMS2/3, HCCPARAMS2, DBOFF/RTSOFF,
CRCR/DCBAAP/CONFIG, iki PORTSC ve xECP'den sınırlı sekiz DWORD okur. Otomatik
HOST turu ayrıca USBXHCI+UCX ETW kaydı başlatır; `logman.exe` WinPE'de varsa
ETL, kısa host resource raporu ve USB metin logları doğrulanmış CACHE
bölümünün köküne kopyalanır. Böylece sonraki turda DMA tablolarının kurulup
kurulmadığı, interrupt/MMIO kaynağının child'a ulaşıp ulaşmadığı ve Microsoft
yığınının döndürdüğü iç hata aynı boot'tan alınabilir.

Dördüncü ölçümde xHCI capability zinciri ve iki port güvenilir biçimde okundu:
`DBOFF=0x4A0`, `RTSOFF=0x440`, portlar power-on/RxDetect durumundaydı. Buna
karşılık başarısız başlangıcın son snapshot'ında `CRCR=0`, `DCBAAP=0` ve
`CONFIG=0` kaldı; USBXHCI'nin kalıcı bir command ring/DCBAA kurulumu
görülmedi. `pnputil /resources` çıktısında kaynak satırı yoktu, fakat PnP
başarısız başlangıçtan sonra atamayı serbest bırakabileceği için bu tek başına
URS resource aktarım hatasını kanıtlamaz.

`0.1.0.4`, `b2lusbmon.sys` adlı pasif KMDF lower-filter'ı host PDO'ya ekler.
Filtre MMIO'ya dokunmaz ve PnP sonucunu değiştirmez; USBXHCI'ye gerçekten
ulaşan translated memory/interrupt descriptorlarını ve
Add/PrepareHardware/D0/Release aşamalarını device registry'sine kaydeder.
Ek olarak 64 KiB common buffer'ı önce AC64 ile uyumlu 64-bit duplex, sonra
32-bit duplex profil ile ayrı ayrı dener. İki NTSTATUS ve device-logical
adresi kaydettikten sonra buffer'ları donanıma hiç vermeden hemen serbest
bırakır. 32-bit deneme başarılı ve address-high sıfırsa, aktif banka
guard'larını kaldırmadan 4 GiB altı sınırlı bir DMA penceresi sağlanabildiği
kanıtlanır; başarısızsa düşük DMA havuzu yönüne geçmeden önce doğrudan kanıt
elde edilir.
`startnet.cmd` bunları `B2LUSB-HOST-MONITOR.TXT` üzerinden final ekrana basar.
Bu sayede USB veya klavye olmadan bir sonraki fotoğrafta `0x10C00000/0x10000`
MMIO ile GSIV 273'ün child'a ulaşıp ulaşmadığı kesinleşir. Aynı sürüm xECP
penceresini 12 DWORD'a çıkararak legacy, USB2 ve eksik kalan USB3 Supported
Protocol bloklarını birlikte gösterir.

`0.1.0.4` cihaz ölçümünde iki common-buffer denemesi de başarılı oldu.
64-bit device-logical adres `0x00000009_7FE9F000`, 32-bit adres ise
`0x00000000_CFEFF000` olarak döndü; ikisinde de enabler/buffer NTSTATUS
başarıydı. Böylece aktif MemoryMap guard'larının DMA tahsisini engellediği
veya 4 GiB altında kullanılabilir DMA havuzu bulunmadığı hipotezleri kapandı.
Buna
karşılık host child `Code 14 / CM_PROB_NEED_RESTART` durumundaydı;
`B2LMonAddSeen=1` olmasına rağmen `PrepareHardware=0`, `D0Entry=0` kaldı.

`0.1.0.5` cihaz sonucu bu sıranın çalışmadığını gösterdi: `NONE` IOCTL'i
başarıyla `requested=NONE` yaptı, ancak Code 14 bekleyen mevcut HOST child
kaldırılamadığı için `current=HOST` kaldı ve hedefli restart satırına
ulaşılamadı.

`0.1.0.6` otomatik testte mevcut child'ı önce ve yalnız
`pnputil /restart-device /deviceid "URS\BYN0002&HOST"` ile yeniden başlatır;
bu turda otomatik `NONE/HOST` rol çevrimi yapılmaz. Komutun gerçek çıktısı
`B2LUSB-HOST-RESTART.TXT` dosyasına, final ekrana ve CACHE'e yazılır. Restart
başarısız olsa bile `/reboot` veya `/force` kullanılmaz; ETW, PnP kaynakları,
filter aşamaları ve parent durumu toplanmaya devam eder. Böylece sonraki
fotoğraf Code 14'ün temizlenip temizlenmediğini ve `B2LMonPrepareSeen` değerini
klavye gerektirmeden gösterecektir.

Cihazdaki `0.1.0.6` sonucu PnPUtil'in doğru HOST instance'ı için
`System reboot is needed to complete configuration operations!` döndürdüğünü
ve Code 14 ile `B2LMonPrepareSeen=0` durumunun sürdüğünü gösterdi. Bu nedenle
`0.1.0.7` runtime INF kurulumunu kaldırır. `stage-winpe-driverstore.ps1`, WIM'i
Windows tarafındaki geçici NTFS alanına kopyalar, DISM ile parent ve host
paketlerini offline DriverStore'a ekler, `oem*.inf` sonuçlarını doğrular ve
commit başarılıysa WIM'i geri yazar. Gerekli yönetici yetkisi tek UAC istemiyle
alınır. `startnet.cmd` yalnız tarama, hedefli restart ve tanı toplar; PnP
stack'i lower filter hazır halde ilk boot sırasında oluşur.

`--touch --usb-test` birlikte kullanılırsa touch/VHF ve USB paketleri tek DISM
mount/commit işleminde stage edilir. UFS, çalışan CACHE yolu ve `mountvol /N`
sıralaması korunmak için bu erken-boot değişikliğinin dışındadır.

`0.1.0.7` cihaz testi bu offline yolu doğruladı: touch ilk WinPE ekranından
itibaren çalıştı ve VHF/HID touch devnode'ları `Started` oldu. USB child için
hedefli restart da başarıyla tamamlandı; Code 14 temizlendi. Kalan hata
`USBXHCI` üzerinde `Code 10 / 0xC000009E` oldu. Filter hâlâ
`PrepareSeen=0/D0EntrySeen=0`, xHCI ise son durumda halted ve
`CRCR/DCBAAP/CONFIG=0` kaldığı için hata USBXHCI'nin erken başlangıç yolundadır.

`0.1.0.8` parent içinden 12 saniyelik, 1 ms yüksek çözünürlüklü ve salt-okuma
xHCI gözlemi yapar. `b2lusbctl.exe observe` ile kurulan gözlemci HCRST, CNR,
Run, HCHalted-clear, HSE ile geçici CRCR/DCBAAP/CONFIG programlamasını status
çıktısında saklar. `--usb-test` bu komutu HOST child restartından hemen önce
otomatik çağırır. Gözlemci controller/PHY/PMU registerlerine veya MemoryMap
guard'larına yazmaz ve 12 saniye sonunda durur.

`0.1.0.8` cihazında gözlemci 7.878 örnek aldı; `USBCMD=0`,
`USBSTS.HCHalted=1`, `CRCR=0`, `DCBAAP=0` ve `CONFIG=0` değerlerinde tek geçiş
görmedi. Hedefli PnP restartı başarılı olmasına rağmen child yine
`Code 10 / 0xC000009E` verdi ve lower-filter `PrepareSeen=0` kaldı. Bu, inbox
USBXHCI'nin controller resetini veya command-ring kurulumunu hiç başlatmadan
çıktığını doğrular.

`0.1.0.9`, aynı lower-filter'a PnP resource-requirements gidiş/dönüş
callback'lerini ekler. Filtre listeleri değiştirmeden logical-configuration ve
descriptor sayılarını, ilk MMIO aralığını ve ilk interrupt aralığını
`B2LMonReq*` değerlerine kaydeder. `B2LMonRemoveAddedSeen` ile raw/translated
descriptor sayıları ise PnP'nin atanmış kaynak listesi üretme aşamasına ulaşıp
ulaşmadığını gösterir. Böylece DWC3 `0x10C00000/0x10000` ile GSIV 273 child
gereksinimlerinde yoksa URS aktarımı; ikisi de mevcutsa USBXHCI'nin sonraki
erken başlatma sözleşmesi araştırılacaktır. Bu sürüm de MMIO, PHY, PMU,
resource listeleri ve MemoryMap guard'ları üzerinde hiçbir değişiklik yapmaz.

`0.1.0.9` cihaz ölçümünde requirements aşağı geçişinde tek descriptor olarak
DWC3 MMIO (`0x10C00000-0x10C0FFFF`, length `0x10000`) görüldü; yukarı dönüşte
descriptor sayısı ikiye, atanmış raw/translated kaynak sayısı da ikiye çıktı.
Dolayısıyla URS host child'a MMIO ve ACPI child kaynağını teslim ediyor;
safe-memory guard veya eksik PnP kaynağı Code 10 nedeni değil.

`0.1.0.10`, kanıtlanan kaynak gözlemcisini USBXHCI'nin üstüne taşır. Filtre
yalnız `IRP_MN_START_DEVICE` için WDM preprocess/completion kaydı tutar;
IRP status'unu değiştirmez, geciktirmez veya tamamlamaz. `B2LMonStartIrpSeen`,
`B2LMonStartCompletionSeen` ve `B2LMonStartCompletionStatus`, USBXHCI'nin
START_DEVICE isteğini aşağı stack'e iletip hangi NTSTATUS ile döndürdüğünü
gösterir. Cihaz ölçümünde `StartIrpSeen=1`, `StartCompletionSeen=1` ve
`StartCompletionStatus=0xC000009E` elde edildi; `PrepareSeen=0` ve
`D0EntrySeen=0` kaldı. USBXHCI böylece kaynak atamasından sonra kendi
START_DEVICE işleyişinde, üst filtrenin KMDF `PrepareHardware` aşamasına
gelmeden `STATUS_DEVICE_POWER_FAILURE` döndürüyor. Aynı görüntüde raw ve
translated atanmış descriptor sayıları ikiydi, DMA64/DMA32 probları başarılıydı
ve xHCI operasyonel registerlerinde geçiş yoktu.

`0.1.0.11`, rol değişimindeki doğrulanmış tek DWC3 farkını düzeltir. Cihaz
ölçümünde `GUSB3PIPECTL=0x01021102` ile USB3 `SUSPHY` biti açık kalmıştı.
Synopsys upstream ve sabitlenmiş Exynos9820 vendor başlangıç sırası, DRD
core/rol başlangıcından önce hem `GUSB2PHYCFG.SUSPHY` hem de
`GUSB3PIPECTL.SUSPHY` bitlerini temizler. Parent sürücü artık aynı iki standart
DWC3 bitini temizleyip readback ile doğrular; Samsung wrapper'daki USB3 portu
kapalı kalır ve combo-PHY, PMA, guard ya da memory-map alanlarına dokunulmaz.
`0.1.0.10` START_DEVICE/SetupAPI/ETW ölçümleri aynen korunur. Atanmış raw ve
translated listelerin ilk MMIO base/length ile IRQ level/vector/affinity/flags
alanları da salt-okuma biçiminde `B2LMonAssigned*` değerlerine kaydedilir.

`0.1.0.12`, `0.1.0.11` cihaz kanıtındaki son sınırı kaldırır: URS tarafından
üretilen sentetik HOST PDO ve hedef WinPE'den farklı build'e ait UrsCx runtime
artık kullanılmaz. Stok Android DT/initcall sırası gibi platform/PHY hazırlığı
önce `ACPI\BYN0002` üzerinde tamamlanır; ardından gerçek `ACPI\BYN0003` çocuk
DWC3 MMIO+GSIV 273 ile doğrudan inbox USBXHCI'ye bağlanır. PHY PMU ve SIDDQ
readback'leri bounded poll ile doğrulanmadan çocuk stack'e hazır durum
bildirilmez. Offline DriverStore ilk boot stack'ini kurduğu için otomatik test
child'ı yeniden başlatmaz. Hedef WIM'de `HIDUSB`, `MOUHID` ve `MOUCLASS`
bileşenleri yoksa paketleme durur; cihazda USB/HID/Mouse yığınları ayrı
raporlanır. ACPI child ayrıca Windows'un debug özelliği olmayan standart SoC
xHCI kimliği `PNP0D15` ile uyumludur. Bu paket sabit USB2 HOST'tur;
function-role ve SuperSpeed yoktur.

`0.1.0.12` cihaz sonucu bu mimariyi doğruladı: `ACPI\BYN0003` üzerinde
USBXHCI başladı ve `USB\ROOT_HUB30` inbox `usbhub3.inf` ile `Started` oldu.
Dolayısıyla önceki Code 10 kapısı kapanmıştır. Aynı boot'ta iki `PORTSC`
değeri de `0x2A0` kaldı (`PP=1`, `PLS=RxDetect`, `CCS=0`, `PED=0`); host
çalışırken konektörde elektriksel aygıt görülmedi. `0.1.0.13` çalışan PnP
child'ını yeniden başlatmayan akışı ayrı sürümle görünür kılar, her port için
`CCS/PED/PP/OCA/PLS/speed/change` çözümlemesi basar ve root hub/alıcı özetini
final ekrana taşır. PHY/DWC3 yazma sırası değişmez; tahmini Type-C/PMIC yazması
eklenmez.

Telefonun 1440x3040 panelinde WinPE oturumu başlamadan önce sistem ölçeği
%175 (`LogPixels=168`) yapılır ve konsol ızgarası 64x41'e ayarlanır. Offline
DEFAULT hive ve WIM geri-okuma doğrulaması `tools_winpe_ui_scale.sh` içindedir.
Bu betik ayrı `build-b2lusb.ps1` ile Windows MSVC/WDK ARM64 derlemesini yapar,
üç SYS ile EXE'yi ve üç INF'yi ortak katalogla test-imzalar, `boot.wim`e ekler ve her
dosyayı hash ile geri okur. `--baseline`, UEFI tanı uygulamalarını SYSTEM
ESP'den çıkarır fakat WIM içindeki Windows sürücülerini korur.

Yalnız Windows paketini tekrar üretmek için:

```bash
bash tools_b2lusb.sh
bash cache_esp.sh --win --system --baseline
```

Önceden derlenmiş, kaynaklardan yeni olmayan ARM64 çıktıları yeniden
paketlenecekse mevcut proje sözleşmesi kullanılır:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lusb.sh
```

Betik `Ucx01000.sys`, `USBXHCI.SYS`, `USBHUB3.SYS`, `HIDCLASS.SYS`,
`HIDPARSE.SYS`, `HIDUSB.SYS`, `MOUHID.SYS` ve `MOUCLASS.SYS` dosyalarının
hedef WinPE WIM'inde bulunduğunu doğrulamadan paket üretmez. UrsCx
binary/header/import-lib ve harici Windows runtime kaynağı gerekmez. Farklı
bir WDK kökü gerekiyorsa
`B2L_WDK_ROOT='/mnt/.../Windows Kits/10'` verilebilir.
WIM içindeki Microsoft dosya adları farklı harf büyüklükleri taşıyabildiği
için bu denetim Windows semantiğine uygun olarak case-insensitive yapılır;
`HIDCLASS.SYS` yolunun yalnız harf büyüklüğü nedeniyle eksik raporlanması
engellenir.

## İlk cihaz testi

INF'ler offline DriverStore'dan ilk boot sırasında bağlanır; `startnet.cmd`
ilk durumu basar. Manuel bring-up oturumunda rol ve gözlem açıkça seçilebilir:

```text
X:\Windows\B2LUSB\b2lusbctl.exe status
X:\Windows\B2LUSB\b2lusbctl.exe observe
X:\Windows\B2LUSB\b2lusbctl.exe host
pnputil /enum-devices /deviceid "ACPI\BYN0003"
```

`0.1.0.16`, pasif KMDF zamanlayıcısını tek atımlı çalıştırıp her örnekten
sonra yeniden kurar. Böylece `EvtDeviceAdd` sırasında görülen
`CM_PROB_FAILED_ADD / STATUS_NOT_SUPPORTED` kaldırılır. HSI2C software reseti
sonrasında controller modu ve timing yeniden kurulur; ilk PMIC kimlik okuması
timeout'u için bounded retry/recovery eklenir. Bu eski sürümdeki `CTL` değeri,
`0.1.0.21` ile universal9820 kaynağındaki tam `MASTER=0x8` değerine
düzeltilmiştir.
Bus veya kimlik okuması yine geçici olarak başarısız olursa ACPI parent D0
başlangıcı düşürülmez: pasif timer bus/kimlik/CC/OTG zincirini yeniden dener ve
`_DEP` altındaki USB host'un başlamasını engellemez. İlk doğrudan konektör
testi, MAX77705'in bildirdiği CC durumu ve charger OTG readback'iyle birlikte
yapılır.

`0.1.0.17`, stok Exynos HSI2C sürücüsündeki kurtarma sınırını Windows'a
taşır: software reset çevresinde firmware'in programladığı bütün HS/FS/SLA
timing değerleri korunur, bus-idle beklenir, transfer-done yanında
abort/no-ack/no-device/timeout durumları ayrıştırılır ve 100 us aralıklı üç
tam işlem denemesi yapılır. Küçük yazmalar `MASTER_RUN` öncesinde TX FIFO'ya
yüklenir. Kimlik doğrulandıktan sonra Samsung USBC sürücüsünün başlangıçta
yaptığı `SAMSUNG_FACTORY_TEST(0x54, 0x00)` komutu 0x21/0x41 opcode
penceresinden gönderilir; APCmdRes beklenip 0x51 cevabı doğrulandıktan sonra
CC=SOURCE için charger OTG geçişi uygulanır. Firmware güncelleme, PD policy,
VCONN ve mux sahipliği bu sabit HOST sürücüsüne alınmaz.

`0.1.0.18`, cihaz çıktısında görülen `CLK_CTRL=0` değerini sahte bir bus
hazırlık hatası saymayı bırakır. Stok Exynos9820 HSI2C45 sürücüsü gibi hazır
olmayı FS timing ve `AUTO_MODE` üzerinden doğrular; fast-mode adres alanına
master kimliği 7'yi ekler. MAX77705 kimliği okunduktan sonra Samsung'un
başlangıç sırası tamamlanır: `FACTORY_TEST(0x54, 0x00)` ile firmware Auto-VBUS
kapatılır, ardından `FW_AUTOIBUS(0x57, 0x01)` ile USBC firmware'i açık
bırakılıp giriş-bus otomasyonu işletim sistemine devredilir. Snapdragon UEFI
günlüğündeki `0x57,0x03` kullanılmaz; o değer CC firmware'ini de kapattığı için
bu Windows HOST/CC gözlem modeline uygun değildir. CTRL/CONF/AUTO_CONF/ADDR
ve iki otomasyon durumunun tanıları bir sonraki cihaz testine eklenmiştir.

`0.1.0.19`, MAX77705 akışını beyond2lte Samsung probe sırasıyla tamamlar.
HSI2C45 stock DT'de kesmeyle çalışır; Windows ACPI'sinde GPA1-6 kesmesi henüz
yayınlanmadığı için aynı register sözleşmesinin bounded polling dalı kullanılır
ve Samsung adapteri gibi toplam iki işlem denemesi yapılır. HSI2C sayfasındaki
ikinci `USI_CON` reset kapısı da her kurtarmada bırakılır. Kimlik doğrulamasının
ardından `CHG_CNFG_06.CHGPROT` en fazla on kez, 20 ms aralık ve son readback ile
açılır; koruma kanıtlanmadan `INT_MASK` veya `CHG_CNFG_00` değiştirilmez.
USBC tarafında stock sıra `AUTOIBUS(0x57,0x01)`, `PD_INT_M=0x1C`,
`VDM_INT_M=0xFF`, `AUTO_VBUS_OFF(0x54,0x00)` ve beyond2lte'nin
`support-audio` profiline ait `CCCTRL1_W(0x0C,0x1F)` olarak uygulanır.
Fiziksel MAX77705 kesmesi ACPI'de bulunmadığından UIC/CC/PD/VDM read-to-clear
registerları sınırlı hızlı pencere ve 200 ms pasif sağlık döngüsüyle boşaltılır. Kilit, maskeler, CCCTRL1 cevabı,
dört kesme örneği, gerçek `CNFG_00` OTG donanım durumu ve başarısız HSI2C fazı
`B2LUSB-MAX77705.TXT` dosyasına yazılır. Bootloader'dan A/E/F modu devralınsa
bile CC değeri SOURCE değilse OTG kapatılır; böylece tek flash sonucu bus,
USBC firmware, charger koruması ve OTG aşamalarına ayrılabilir.

`0.1.0.23`, tek aktif CPU'lu WinPE için MAX77705 hata yolunu boot'tan ayırır.
İlk kimlik/CC işlemi artık KMDF `D0Entry` içinde yapılmaz; 1 ms sonra başlayan
PASSIVE_LEVEL tek-atımlı timer'a bırakılır. HSI2C polling döngüsü CPU'yu
`KeStallExecutionProcessor` ile tüketmek yerine zaman tabanlı 1 ms uykularla
100 ms içinde tamamlanır. Başarısız probe 200 ms'de yeniden saldırmak yerine
2 saniye backoff uygular; başarılı CC takibi yine 200 ms'dir. Böylece MAX77705
cevap vermese bile PnP/USBXHCI/WinPE boot yolu ilerleyebilir.

`0.1.0.28`, henüz cihazda doğrulanmamış olay tanısıdır. Opcode öncesi ve
cevap bekleme UIC okumaları dahil ilk 16 ve son 48 olay, SYSMSG/CC/PD ham
durumları ve zamanları sabit 3.128 baytlık bir kayıtta tutulur. Tanı snapshot'ı
güç rolü kararını değiştirmez; ForceError veya firmware reset eklenmez.
`b2lusbctl.exe maxevents` tek registry okumasıyla kaydı gösterir; BYN0002
başlamamış olsa da çalışır. Otomatik toplama `B2LUSB-MAX77705-EVENTS.TXT`
dosyasına iki raporu ekler; olay özeti mevcut kritik MAX ekranına girer.
Kayıt formatı, hata yorumlama ve WSL/flaş A/B deneyi için
`specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md` başındaki `.28` bölümüne bakın.

USB/klavye gerektirmeyen ana kanıt yöntemi `maxscreen` fotoğraf görünümüdür.
`startnet.cmd` otomatik HOST testi sonunda bu görünümü açar; genel MAX durumu
ve 12 kayıtlık olay sayfaları 20 saniyede bir değişir, turlar otomatik tekrarlanır.
Her yeni turda registry kaydı yenilenir; takma/çıkarma sonraki turdan fotoğraflanır.
`maxevents` ve TXT dosyaları isteğe bağlı yardımcı yol olarak kalır.

`0.1.0.27`, Linux MAX77705 probe sonundaki tek-seferlik manual CC-open bırakma
adımını tamamlar. Çalışma maskeleri kurulduktan sonra `CONTROL3_R(0x1c)` okunur;
`LockErrorEn` biti açıksa diğer bitler korunarak `CONTROL3_W(0x1d)` ile yalnız
bit 1 temizlenir ve ikinci `CONTROL3_R` ile doğrulanır. Bu işlem rol veya VBUS
zorlamaz. `CCOPEN_OK`, `CTRL3_BEFORE` ve `CTRL3_AFTER` tanı alanları bu hipotezi
tek flaşta sınar. Kalıcı agresif polling de kaldırılmıştır: MAX ayrık durumunda
20 ms aralık yalnız ilk 50 örnek, sonrasında 200 ms'dir; xHCI gözlemcisi ise
yüksek çözünürlüklü 1 ms/12 s yerine normal 20 ms/5 s pencere kullanır.

`0.1.0.26`, `.25` cihaz kanıtındaki ilk canlı fark olan Type-C olay teslimini
Linux parent/nested IRQ sırasına yaklaştırır. Her örnekte önce TOP `INTSRC`
okunur, ardından `UIC/CC/PD/VDM` read-to-clear baytları tek blok transferle
alınır ve CC durum registerları başka charger erişimi araya girmeden toplu
okunur. Canlı kesme baytlarının OR-birikimi, önceki/geçerli CC rolü, örnek ve
geçiş sayaçları ile 100 ns zaman damgaları tanıya eklenmiştir. Doğrulanmış
GPA1_6 ACPI kesme kaynağı hâlâ bulunmadığından gerçek ISR uydurulmaz: bağlantı
beklerken pasif örnek aralığı 20 ms, rol yerleşince 200 ms olur; ayrık durumda
ayrıntılı charger/registry tanısı yine 200 ms temposunda tutulur. `CC=SOURCE`
kanıtı olmadan MUIC USB_AP veya VBUS/OTG açılmaz.

`0.1.0.25`, Linux karşılaştırmasında eksik olduğu kesinleşen MAX77705 probe
kesme düzenini tamamlar. Dört mantıksal kesme grubu için önce opcode-probe
vektörü `UIC/CC/PD/VDM = 00/20/1c/ff`, init opcodes tamamlandıktan sonra
çalışan Linux örneğinde görülen vektör `00/20/00/f0` yazılır; dört register da
readback ile doğrulanır. `CCCTRL1_W(0x0c,0x1f)` artık yalnız APCMD cevabıyla
başarılı sayılmaz: `CCCTRL1_R(0x0b)` sonucu gerçekten `0x1f` olmalıdır. Böylece
6. probe/init adımı ile daha sonraki fiziksel CC attach kararı birbirinden
ayrılır. HSI2C tamamlanması, doğrulanmış ACPI/GIC kesme kaynağı olmadığı için
tek CPU'yu meşgul etmeyen bounded polling ile kalır; Linux'taki `IRQ 114`
sanal numarası Windows'a sabitlenmez.

`0.1.0.22`, güncel cihaz çıktısında kesinleşen son HSI2C transport farkını
kaldırır. Stock `hsi2c_45` düğümünde `samsung,reset-before-trans` yoktur ve
`exynos5_i2c_xfer()` eski `TRANS_STATUS.MASTER_BUSY` değerini mesaj öncesi
boşaltmayı beklemez; ADDRESS/AUTO_CONF'u programlayıp doğrudan `MASTER_RUN`
verir. Windows yolu artık ilk 0x66 adres yazımından önce veya STOP sonrasında
stockta olmayan BUS_IDLE kapısına takılmaz; reset/retry yalnız gerçek
NACK/arbitration/transfer timeout'undan sonra yapılır. Mesaj polling sınırı da
Samsung'un bir saniyelik `EXYNOS5_I2C_TIMEOUT` değeriyle eşlenmiştir. Bu uzun
busy-poll sınırı tek aktif CPU'lu WinPE'de boot'u yavaşlattığı için 0.1.0.23'te
Windows'a uygun uyuyan 100 ms sınırla değiştirilmiştir.

`0.1.0.21`, Samsung universal9820 kaynağıyla yapılan tam sıra karşılaştırmasını
iki güç/transport düzeltmesiyle tamamlar. HSI2C45 reset/recovery sonunda ortak
IP'de tanımlı fakat bu SoC yolunda kullanılmayan `FUNC_MODE_I2C` biti artık
yazılmaz; `HSI2C_CTL` Samsung `exynos5_i2c_init()` ile aynı `MASTER=0x8`
durumuna getirilir. UEFI handoff sürücüsü ayrıca DTS'de `boot-on` olan S2MPS19
LDO10M/LDO11M/LDO12M raylarını voltajlarına dokunmadan `NORMAL/ON` tutar ve
Samsung `exynos_usbdrd_phy_conn()` gibi DWC3 başlangıcından önce 1 ms bekler.
Windows'ta henüz ACPM/S2MPS19 regulator sağlayıcısı olmadığından raylar
D0Exit'te doğrudan PMIC yazmasıyla kapatılmaz.

Paket manifesti parent, host ve MAX77705 INF sürümlerini artık ayrı alanlarda
INF dosyalarından türetir. Böylece yalnız MAX77705 değiştiğinde eski sabit paket
sürümü cihaz fotoğrafını yanlış build ile ilişkilendiremez.

Otomatik testin final ekranı, uzun `B2LUSB-MAX77705.TXT` dökümünü ekrana
basmak yerine `B2LUSB-MAX77705-SUMMARY.TXT` içindeki kritik probe zincirini en
sona koyar. `STAGE/STATUS`, HSI2C kontrol-transfer-adres-retry-fazı, PMIC ve CC
kimliği, MUIC yol doğrulaması, charger kilidi/modu ve yazılım/donanım OTG sonucu
tek fotoğrafa sığar. Eksik bir özet satırı ilgili probe aşamasına hiç
ulaşılmadığını gösterir; ayrıntılı PnP ve register dökümü yine CACHE'e aktarılır.

`0.1.0.20`, Samsung MUIC attach yolunda eksik kalan USB2 analog anahtarını
tamamlar. OTG/RpRd SOURCE olayında `CONTROL1_WRITE(0x06)` ile
`COM_USB=0x09` gönderilir; `CONTROL1_READ(0x05)` sonucu 0x09 olmadan charger
OTG/VBUS açılmaz. SOURCE dışındaki ilk örnekte ve detach'te VBUS kapatıldıktan
sonra `COM_OPEN=0x3f` yazılıp okunur. İstenen yol, gerçek CONTROL1 readback'i,
doğrulama sonucu ve AP USB yolu durumu `B2LUSB-MAX77705.TXT` içine eklenmiştir.
Bu adım MAX77705 CC/VBUS yönetiminden ayrı olan D+/D- -> AP PHY bağlantısını
kurar; xHCI'nin power-on halde sürekli `CCS=0 / RxDetect` kalmasına karşı
Samsung kaynakta kullanılan doğrudan donanım karşılığıdır.

`cache_esp.sh --win --system`, sürücü ekleme/çıkarma turlarının standalone
`boot.wim` içinde bıraktığı kullanılmayan akışları `wimlib-imagex optimize`
ile temizler ve `verify` sonrasında gerçek boyuta göre SYSTEM imajını hesaplar.
Bu işlem her turda görülen kümülatif imaj büyümesini engeller; WIM içeriğini
ve sıkıştırma türünü değiştirmez.

Başarı sırası:

1. `current=HOST`, `GCTL.PRTCAP=HOST`, `GSTS.CURMOD=HOST` ve `resets>=1`;
2. `ACPI\BYN0004` için `B2LMaxAutoVbusDisabled=1`,
   `B2LMaxAutoIbusConfigured=1`, `B2LMaxAutoIbusMode=1`,
   `B2LMaxChargerUnlocked=1`, `B2LMaxUsbcMasksConfigured=1`,
   `B2LMaxUsbcRuntimeMasksConfigured=1`,
   `B2LMaxUicInterruptMask=0`, `B2LMaxCcInterruptMask=0x20`,
   `B2LMaxPdInterruptMask=0`, `B2LMaxVdmInterruptMask=0xf0`,
   `B2LMaxCcControlConfigured=1`, `B2LMaxCcControlReadBack=0x1f`,
   `B2LMaxCcStatus0 & 7 = 2`,
   `B2LMaxOtgEnabled=1`, `B2LMaxOtgHardwareActive=1` ve charger mode `A/E/F`;
3. `ACPI\BYN0003` için `USBXHCI` servisinin başlaması;
4. root hub ve USB mouse alıcısının numaralandırılması;
5. USB alıcının HIDClass altında, fare çocuğunun Mouse sınıfında görünmesi;
6. `HidUsb -> mouhid -> mouclass` zinciri üzerinden imleç hareketi ve tıklama.

Otomatik testten sonra CACHE kökünde `B2LUSB-HOST-RESOURCES.TXT`,
`B2LUSB-HOST-MONITOR.TXT`, `B2LUSB-USB.TXT`, `B2LUSB-USB-SUMMARY.TXT`, `B2LUSB-HID.TXT`,
`B2LUSB-MOUSE.TXT`, `B2LUSB-MAX77705.TXT`, `B2LUSB-MAX77705-SUMMARY.TXT`,
`B2LUSB-ETW.TXT` ve destekleniyorsa
`B2LUSB-USBXHCI*.etl` bulunur. ETL,
Windows Performance Analyzer'da USBXHCI ve UCX olaylarıyla açılabilir.
USB henüz çalışmadığı sürece bu dosyalar test girdisi sayılmaz: `--usb-test`
final ekranı otomatik temizleyip kısa host resource ve xHCI status özetini
ekranda bırakır; sonraki karar için bu ekranın fotoğrafı yeterlidir. Başarısız
native OSK penceresinin özeti kapatmaması için OSK bu test profilinde otomatik
başlatılmaz. Touch sürücüsü ve `b2lmt.exe` yine kurulur.

Bu paket sabit HOST profilidir. `b2lusbctl` DEVICE/NONE rol isteğini kabul
etmez; güç kapanışı ve yeniden açılışı yalnız PnP/power callback'leri üzerinden
parent-child sırasıyla yapılır.

## Henüz tamamlanmayan kapılar

1. **USB 3.x PHY:** FSYS0 ownership/clock düzeltildikten sonra combo PCS/PMA
   salt-okuma preflight, PLL/CDR lock ve ancak sonra ACPI kaynak genişletmesi.
2. **Tam Type-C/UcmCx profili:** Bu build yalnız sabit HOST için mevcut
   MAX77705 firmware CC state'ini izler ve Samsung charger VBUS/OTG geçişini
   uygular. Windows UcmCx rol takası, orientation/data mux, VCONN ve tam PD
   policy gelecek ayrı dual-role profiline aittir.
3. **Function stack:** Sabit HOST ACPI düzeninden ayrı bir gelecek profilde
   hedef WIM'deki `UfxSynopsys.inf` bölümünün ve function policy'nin
   doğrulanması.
4. **KDNET-EEM-USB:** USB3 device yolu kararlı olduktan sonra `DBG2` içinde
   Network debug port type `0x8003`, DWC3 tabanı ve uygun gelecek function
   namespace yolu; ardından hedefte `kdnet.exe` ile destek ve transport
   doğrulaması. Microsoft örneğindeki subtype `0x5143` Qualcomm vendor
   kimliğidir ve Exynos için kullanılamaz. Inbox taşıyıcı Samsung DWC3'ü
   tanımıyorsa Microsoft ile ayrılmış bir Samsung/Exynos subtype ve importsuz
   özel `kd_<PortType>_<PortSubtype>.dll` gerekir. Bu tablo, çalışan dual-role
   controller ve doğru taşıyıcı kanıtlanmadan Windows'a ilan edilmeyecektir.

Microsoft mimari referansları:

- <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-dual-role-driver-stack-architecture>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/bring-up-a-usb-type-c-connector-on-a-windows-system>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-kernel-mode-debugging-over-usb-eem-arm-kdnet>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table>
