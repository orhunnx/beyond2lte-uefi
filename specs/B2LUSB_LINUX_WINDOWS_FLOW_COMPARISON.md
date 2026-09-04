# Beyond2LTE USB host akışı: Samsung Linux ve Windows karşılaştırması

## 5 Eylül 2026: `.30` — HSI2C45 gerçek aktarım kesmesi

**Hedef değişmedi:** cihazın özgün USB olay/rol/güç akışını Windows'ta gerçek
kaynaklarıyla çalıştırmak. `.30`, bu hedefin HSI2C kesme adımını uygular;
MAX GPA1_6 kesmesi ve CC/PD -> DWC3/xHCI yaşam döngüsü henüz tamamlanmadı.
Telefonun son kanıtı hâlâ paylaşılan `.28` fotoğraflarıdır. `.29/.30` için
WSL build, gerçek GSIV teslimi ve USB bağlantısı cihazda doğrulanmış değildir.
Bu bölüm aşağıdaki `.29` durum tablosundaki HSI2C maddelerini günceller.

### Kaynak ve uygulanan aktarım sözleşmesi

Birleşik DT `hsi2c@15D60000` düğümündeki `<0 0x52 0>` SPI82 -> GSIV114
(`0x72`) kaynağı ACPI `USBC/BYN0004` `_CRS`'ye level/active-high olarak
eklendi. MAX'ın ayrı GPIO kesmesi GSIV51 ile karıştırılmıyor. KMDF nesnesi
atanan interrupt kaynağını kullanır; raw GSIV, translated vector, eksik/çift
kaynak ve edge/MSI uyumsuzluğu sürücü tarafından denetlenir. GIC yönlendirme
ve EOI işlemleri Windows HAL/KMDF'ye aittir; sürücü GIC dağıtıcısına yazmaz.
GIC SPI için Samsung sürücüsünün desteklediği seviye türü active-high'dır.
[Samsung GIC türü](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/irqchip/irq-gic-v3.c#L297)

`b2lmax77705_hw.c` içindeki eski pasif RX/TX polling döngüsü kaldırıldı:

1. Mesaj, register adresi, repeated START/STOP seçimi ve FIFO eşikleri
   interrupt kilidi altında hazırlanır. Buffer ve uzunluk MASTER_RUN'dan
   önce yayımlanır. TX preload yolu da kaldırıldı; Samsung IRQ yolundaki gibi
   FIFO'yu TX almost-empty kesmesi doldurur.
2. RX için almost-full + trailing, TX için almost-empty; ikisi için
   `TRANSFER_DONE` ve `CHK_TRANS_STATE=0xf00` açılır. Hata grubu artık
   polling dalındaki dar kontrolle sınırlandırılmaz.
3. Gerçek `EvtInterruptIsr`, enabled/pending kesişimini kontrol eder.
   ISR bir çağrıda en fazla 16 FIFO baytını işler; bekleme, registry veya
   MAX opcode çağrısı yapmaz. FIFO işleme sonunda status yeniden okunur;
   geç gelen hata DONE'a üstün tutulur. Captured status W1C ile temizlenir.
4. RX, bütün istenen baytlar **ISR tarafından** alındığında tamamlanır;
   ayrı DONE koşulu yoktur. TX, tüm veri kuyruğa konduktan sonra gerçek
   DONE ve boş FIFO ister. Tamamlanma durumu ISR'de kaydedilir; DPC yalnız
   `KeSetEvent` ile pasif bekleyiciyi uyandırır.
5. Bekleyici her uyanışta kilit altında mesaj durumunu kontrol eder.
   Eski/gecikmiş DPC yeni mesaja başarı veremez, toplam süreyi uzatamaz.
   Timeout ile ISR yarışında buffer ancak interrupt kilidi altında ayrılır;
   geç kesme önceki stack buffer'ına erişemez.
6. DT'deki `samsung,scl-clk-stretching` nedeniyle gerçek tamamlanmadan sonra
   master INIT durumu en fazla 100 ms beklenir. Bu, transfer başlamadan
   firmware'den kalan BUSY üzerinde bekleyen eski hatalı kapı değildir.
   INIT'e dönülmezse başarısızlık bildirilir.

Samsung kaynakta `EXYNOS5_I2C_TIMEOUT=msecs_to_jiffies(1000)` kullanır;
`.30` IRQ bekleme sınırı bu nedenle 1 saniyedir. `.29`'un 100 ms pasif FIFO
okuma sınırı sürdürülmedi. Bu değişiklik kayıp kesmeyi gizlemez: testte RX
FIFO'da sekiz bayt hazır olsa dahi IRQ teslim edilmezse **0 bayt / timeout**
sonucu alınır; hiçbir polling fallback bulunmaz. Mevcut hata sonrası controller
recovery/retry sınırı korunur.
[Samsung IRQ ve mesaj sırası](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/i2c/busses/i2c-exynos5.c#L616),
[özgün timeout](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/i2c/busses/i2c-exynos5.c#L206)

### Güç ve sürücü yaşam döngüsü

HSI2C saat/pin/denetleyici hazırlığı `D0Entry` içinde, donanım kesmesi
etkinleştirilmeden önce yapılır. Başarısız adapter hazırlığı PnP'ye hata döner.
MAX istemci/kimlik/opcode işi ancak `D0EntryPostInterruptsEnabled` sonrasında
başlar. Pasif işçi D0 girişinde artık IRQ hazır olmadan yarışamaz.

Kapanışta timer durdurulup çalışan callback'in bitmesi beklenir. OTG off ve
MUIC OPEN işlemleri `D0ExitPreInterruptsDisabled` içinde, HSI2C kesmesi hâlâ
çalışırken yapılır. Bu işlemler hata verirse hata saklanır ve callback'e
iletilir; başarısız OTG-off, yazılımla donanım kapandı şeklinde işaretlenmez.
`ReleaseHardware` kesme ayrıldıktan sonra yeni I2C mesajı göndermez.
[KMDF başlangıç sınırı](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry_post_interrupts_enabled),
[KMDF kapanış sınırı](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit_pre_interrupts_disabled),
[ISR/DPC iş bölümü](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/porting-interrupt-functionality)

### MAX olay kesmesinin sonraki bağımlılığı

Samsung pinctrl kaynağı tekrar incelendi. GPA1 bankının offset'i `0x20`,
EINT offset'i `0x04`; DT wakeup-controller türü `exynos7-wakeup-eint`.
Bu tanımlardan EINT CON/MASK/PEND register offset'leri `0x704/0x904/0xa04`
çıkar. Pin6 için CON alanı `[27:24]`, MASK/PEND biti `6`dır. Linux MAX IRQ
isteği `IRQF_TRIGGER_LOW | IRQF_ONESHOT`; pin EINT işlevi `0xf`, PUD/DRV `0`.
Linux seviye kesmesinde unmask'ten hemen önce yalnız ilgili pending bitini
W1C ile temizler. Diğer pinlerin maskelerini/pending bitlerini değiştirmek
özgün kaynak sahipliğini ihlal eder.
[9820 bankı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/pinctrl/samsung/pinctrl-exynos-arm64.c#L579),
[EINT mask/ack/unmask](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/pinctrl/samsung/pinctrl-exynos.c#L64),
[MAX IRQ isteği](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/mfd/max77705-irq.c#L449)

**Bunlar `.30`'da MAX EINT uygulanmış anlamına gelmez.** Sonraki değişiklikte
EINT/pin sahipliği, mask/ack ve MFD UIC/CC/PD/VDM dispatcher birlikte
bağlanmalı. Mevcut `B2lMaxEvtPollTimer` tüm servis/opcode çağrısı boyunca
`BusLock` tutuyor; opcode ise APCMDRES'i okuyarak bekliyor. Ayrı MAX IRQ
işleyicisine aynı kilidi aldırıp bekleyiciyi değiştirmemek, yanıtın gelmesini
engeller. Gerçek IRQ portu bu kilit/yanıt düzenini de değiştirmelidir.
HSI2C IRQ'sının çalışması tek başına bu MAX olay portunu sağlamaz.

### Tanı ve doğrulama

MAX sürümü `0.1.0.30`, foto arayüzü `MAX FOTO v3`; USB foto sayfası v2 ve
binary olay şeması v1. Genel sayfa 32 satırdır. Yeni alanlar:

- `I2C_GSIV`: PnP raw kaynağı; beklenen `00000072`.
- `I2C_IRQ`: enabled/pending kaynağı gerçekten işleyen ISR sayısı.
- `I2C_DONE`: ISR'nin sonuçlandırdığı mesaj sayısı; hatalı mesajları da
  içerir, tek başına başarı değildir. STATUS/RX_NT ile birlikte okunur.

Translated vector ve son interrupt status ayrıca registry'de saklanır.
`I2C_IRQ=0`, veri hazır olsa dahi gerçek kesme tesliminin kanıtlanmadığını
söyler; artan sayaçlar da USB enumeration kanıtı değildir.

Yerel kontroller geçti: gerçek C ISR/DPC/transfer fonksiyonlarıyla 1–64 bayt
RX/TX, trailing, hata + DONE, FIFO sırasında geç NACK, masked status, kayıp
IRQ, 3/8 RX, eksik TX DONE, geç/eski DPC, sabit deadline, güç kapatma ve
clock-stretch; gerçek Prepare/D0/Release callback'leriyle kaynak ve güç
sırası; mevcut CC/opcode/olay/foto/log testleri ve üç kaynak güvenlik testi.
Bu host fixture'ları fiziksel GIC teslimi ve WDK derlemesi değildir.

WSL yöntemi değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

ACPI de değiştiği için aynı turdan **boot.img ve system.img birlikte** gerekir.
Cihazda ilk beklenen kanıt `I2C_GSIV=72`, artan IRQ/DONE, başarılı kimlik ve
`v=1/nt=0` snapshot'lardır. Hedefin tamamlanması için hâlâ özgün MAX/CC/PD
kesmeleri, gerçek rol/güç bildirim zinciri, USB enumeration/veri aktarımı ve
takma/çıkarma çevriminin cihaz kanıtı gerekir. Hedef aktif kalır.


## 5 Eylül 2026: `.28` fotoğraf sonucu ve `.29` kaynak düzeltmesi

**Güncel cihaz kanıtı artık `.28` olay tanısını içeren flaştır.** Kullanıcının
12 fotoğrafı incelendi. Bazıları aynı çıktının devamı/tekrarıdır; 12 ayrı
başarılı test değildir. Olay ekranlarının iki turunda da `D0=448517796`
bulunur: aynı oturumda sayaç 13'ten 15'e ilerlemiştir. MAX INF sürümü bu
fotoğraflarda görünmediği için `.28` tanımlaması olay şeması ve `MAX FOTO v1`
çıktısına dayanır; host INF `0.1.0.18` açıkça görünür.

**Çalışma ölçütü:** Linux/Android'in gerçek donanım ve olay sırası uygulanacak.
Bir aşama başka bir aşamanın başarılı görünmesiyle tamamlanmış sayılmayacak.
Özellikle pin aktifliği SOURCE rolü değildir; `STAGE=70` USB hazır anlamına
gelmez; `_DEP` MAX bağlantı durumunu beklemez; xHCI `PP=1` konektörde VBUS
ölçümü değildir. Bu bölüm alttaki tarihsel `.27/.28` planlarının önüne geçer.

### Fotoğrafların söylediği ve söylemediği

| Kanıt | Okunan değer | Yorum |
|---|---|---|
| Fotoğraf 1/3/4/5, host | `ACPI\BYN0003\0 Started`, `USBXHCI`, MMIO `10c00000..10c0ffff`, raw IRQ `273` | PnP start başarılı. Root-hub/alıcı/HID enumeration kanıtı değildir. |
| Fotoğraf 3/5, kaynak ve DMA tanısı | START completion `0`, DMA64 ve DMA32 buffer/enabler status `0`; raw IRQ `111`, translated vector `a00`, level `a` | Kaynak ataması ve buffer ayırma başarılı. Gerçek xHCI interrupt teslimini veya tamamlanan bir USB transferini kanıtlamaz. |
| Fotoğraf 4/6/7, controller | `CMD=2005`, `STS=0`, `CFG=40`, `DCBAAP=000000097feb0000`, `CRCR=0`; observer `161` örnek, `0` transition | Controller çalışır durumda görülmüş. CRCR sıfır okunmasını tek başına “command ring kurulmamış” diye yorumlamıyoruz. |
| Aynı fotoğraflar, port | USB2 `000002a0`, diğer port `0e0002a0`; `CCS=0`, `PED=0`, `PP=1`, `RxDetect` | Fiziksel USB bağlantısı xHCI tarafında görünmüyor. |
| Fotoğraf 2/6/7/10, MAX init | `STAGE=70`, `STATUS=0`, PMIC `15/rev2`, HW/FW `1a/1a`, minor `10`, maskeler `00/20/00/f0`, `CCCTRL1=1f` | Kimlik, kısa I2C okumaları ve init/read-back işlemleri tamamlanmış. Bütün I2C uzunluklarının çalıştığı sonucu çıkarılamaz. |
| CC-open | `CCOPEN_OK=1`, CONTROL3 `64 -> 64` | LockErrorEn zaten kapalı; bu önceki hipotez tekrar doğrulanmıyor. |
| Genel ekran tur 1 / tur 2 | CC `80/00 -> 00/00`; örnek sayısı `aca -> b6b`; rol/geçiş/source sayaçları `0` | CC2 pin durumu değişiyor; güç rolü örnekleri hâlâ No Connection. |
| Bağlantı yolu | MUIC `3f`, OTG SW/HW `0/0`, charger mode `15` | SOURCE görülmediğinden güç ve veri yolu açılmamış. HID servislerini değiştirmek bu katmanı onarmaz. |
| Fotoğraf 4 | `'findstr' is not recognized...` | Boş root-hub/SetupAPI özetleri “aygıt/hata yok” kanıtı değildir. Özetleme aracı çalışmamış. |
| Fotoğraf 8/9/11/12 | `count=13 -> 15`, `FAIL=13 -> 15`, her kayıtta `v=0 nt=c00000b5` | **Bütün 8 baytlık status snapshot'ları zaman aşımına düşmüş.** Sıfırlanmış `r` alanları donanım verisi değildir. |

`UIC=B2`, SYSMSG interrupt biti `0x40` içermiyor. `SYS=0`, yakalanan olaylarda
bu bitin görülmediğini söyler; geçerli SYSMSG register okuması bulunmadığı
için “firmware hatası yok” denemez. `ERR=1`, tek ham AttachSrcErrI örneğidir;
CC maskesindeki bit 5 de set olduğundan Linux nested IRQ'sının çalıştığı veya
hatanın o anda sürdüğü çıkarılamaz. CC_STATUS1 bit 2'yi geçerli okumak gerekir.

### Aynı D0 oturumundaki olay zaman çizgisi

Zamanlar MAX D0 başlangıcına göre milisaniyedir. `p`, ayrı ve başarılı
politika CC okumasıdır; `r` bütün satırlarda geçersizdir.

| Sıra | t (ms) | Kaynak / opcode / IRQ | Politika CC |
|---:|---:|---|---|
| 1 | 2331 | baseline, `FF`, `00000000` | yok |
| 2 | 2675 | opcode öncesi, `57`, UIC `32` | yok |
| 3 | 3020 | opcode bekleme, `57`, UIC `80` | yok |
| 4 | 3850 | opcode bekleme, `54`, UIC `80` | yok |
| 5 | 4273 | opcode bekleme, `0c`, UIC `80` | yok |
| 6 | 4696 | opcode bekleme, `0b`, UIC `80` | yok |
| 7 | 5400 | opcode bekleme, `1c`, UIC `80` | yok |
| 8 | 5823 | opcode bekleme, `1c`, UIC `80` | yok |
| 9 | 6309 | normal CC, `00000000` | `0080` |
| 10 | 6872 | opcode bekleme, `06`, UIC `80` | yok |
| 11 | 7279 | opcode bekleme, `05`, UIC `80` | yok |
| 12 | 819188 | normal CC, `00000902` | `0000` |
| 13 | 820018 | normal CC, `00002802` | `0080` |
| 14 | 1532845 | normal CC, `00000802` | `0000` |
| 15 | 1533675 | normal CC, `00000100` | `0000` |

12–15 numaralı kayıtlar pin/CC olaylarının sonradan da geldiğini gösteriyor.
Fotoğraflarda fiziksel takma/çıkarma anları etiketli olmadığından hangi
satırın hangi elle yapılan işleme karşılık geldiği kesinleştirilmedi.
Hiçbir başarılı politika örneğinde `CCStat=2` yok. Genel ekrandaki son
`I2C_RETRY=0/I2C_PHASE=0`, önceki snapshot hatalarını çürütmez: son başarılı
kısa transfer bu alanları yeniden yazmıştır.

### Kodda doğrulanan iki hata ve `.29` değişikliği

**1. HSI2C hazır FIFO verisi her baytta uyunarak tüketiliyordu.**
`B2lMaxTransferMessage()` yalnız bir RX baytı alıp 1 ms bekleme istiyordu;
mesajın toplam sınırı 100 ms. Windows beklemesinin ardından thread'in hemen
çalışacağı garanti edilmez. Bu, hazır 8 baytın bile süre içinde alınamamasına
neden olabilir. [Windows bekleme sözleşmesi](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kedelayexecutionthread)

Samsung interrupt işleyicisi hazır RX FIFO'yu boşaltır, TX FIFO'nun boş
yerlerini doldurur. Windows `.29` da hazır veriyi aynı ziyaret içinde
alır/yazar; yalnız FIFO boş/dolu olup ilerleme bekleniyorsa uyur. İstenen
uzunluk, mevcut 100 ms timeout, error kontrolleri ve tekrar sınırı korunur.
Timeout'u büyüterek ya da sıfır veri döndürerek başarı üretilmez.
[Samsung FIFO işleyicisi](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/i2c/busses/i2c-exynos5.c#L616)

Gerçek transfer fonksiyonuyla yapılan host testi, ilk baytların ilk
zamanlayıcı dönüşünde geldiği 15,625 ms'lik gecikme modelinde eski kodun
**7/8 baytta timeout** verdiğini, yeni kodun tek FIFO boşaltımıyla okuduğunu
gösterdi. Bu telefonda ölçülmüş zamanlayıcı periyodu değildir; kaynak
hatasının yeniden üretimidir. Cihazdaki 8 baytlık hatanın bu düzeltmeyle
kapanması WSL build + flaşla doğrulanacaktır.

`RX_FAIL`, `RX_NT`, `RX_WANTED`, `RX_READ` tanıları eklendi. Başarısız fiziksel
okuma denemelerinin sayısını ve son hatada istenen/alınan bayt sayısını
saklar; sonraki başarılı kısa okumalar bunları silmez. D0 başlangıcında
sıfırlanır. Retry sayıldığı için RX_FAIL ile olay başına artan FAIL birebir
aynı sayaç değildir.

**2. Windows MUIC ve VBUS sırasını ters kuruyordu.**
Linux `max77705_ccstat_irq_handler()` gerçek `cc_SOURCE` dalında VBUS'u açar;
MUIC OTG/RpRd akışı ardından `06 09` gönderir. Windows'taki “önce MUIC
read-back, sonra VBUS” kapısı Linux'a ait değildi. `.29`, bu iki adımı
`CC_SOURCE -> VBUS/OTG -> MUIC USB_AP` sırasına aldı. VBUS çağrısı başarısızsa
MUIC dalına geçilmez. SOURCE dışına çıkınca güç kapatma ve OPEN yolu sürer.
[Samsung CC source dalı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L683)

Gerçek CC çözümleme ve bağlantı fonksiyonlarını çalıştıran testler; pin
aktifliğinin SOURCE'a dönüşmediğini, bütün source dışı durumları,
`CC -> VBUS -> MUIC` sırasını, detach'i ve hata/yeniden deneme yollarını
kontrol eder. **Bu sıra hatası tek başına mevcut CCStat=0'ın nedeni olarak
kanıtlanmadı.** Çalışan Linux logunun yerel kopyasındaki ilk gerçek host
bildirimi `461.183854`, USB_HOST_ATTACHED `461.183916`, MUIC seçim kararı
`461.185836`dır; erken CC/VBUS zamanları aşağıdaki tarihsel tablodan gelir.

### Tanı ekranı ve toplama düzeltmeleri

- Beş `findstr` çağrısı kaldırıldı. Aynı WIM'de bulunan `b2lusbctl filter`
  büyük/küçük harf duyarsız sabit metin eşlemesi yapar. BOM'lu UTF-8/UTF-16LE
  UCRT Unicode modunda, BOM'suz çıktı normal metin modunda okunur.
  Eşleşmeme, açma/okuma hatası, desteklenmeyen UTF-16BE ve aşırı uzun satır
  ayrı `LOG_FILTER_*` sonuçlarıdır; hata boş özet gibi sunulmaz.
  [UCRT BOM davranışı](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen)
- Tam MAX registry dökümü için 86 ayrı `reg.exe` yerine tek key sorgusu
  kullanılır. Kısa özetin 55 eski alanı ve dört yeni RX alanı korunur.
  Toplam toplama süresinin ne kadar azalacağı cihazda ölçülmedi.
- `MAX FOTO v2`: geçersiz olaylarda sıfır ham registerlar yerine
  `r=OKUNAMADI`; genel ekranda kalıcı RX hata bilgileri ve STAGE açıklaması.
- Yeni `USB FOTO v2` sayfası, mevcut salt-okuma GET_STATUS arayüzünden
  port CCS/PED/PP değerlerini ve PnP'den mevcut USB/HID kimliklerini gösterir.
  Yeniden tarama, host restart veya rol komutu göndermez. Her enumerator için
  ilk 6 kimliğin ilk 48 karakteri ve toplam sayı gösterilir. HID listesi
  başka giriş aygıtlarını da kapsayabilir; sayının artması tek başına USB
  fare kanıtı değildir, alıcının VID/PID'si aranır.
- Sayfalar 20 saniye kalır: genel MAX, canlı USB/PnP, sonra aynı snapshot'ın
  olay sayfaları. Tam 64 kayıtla tur 8 sayfa/160 saniyedir. MAX alanları,
  USB/PnP ve olay kaydı birbirleriyle atomik anlık görüntü sayılmaz.

### 16 aşamanın yeniden denetimi ve açık işler

| Aşama | `.28` kanıtı ve `.29` sonrası durum |
|---|---|
| 1. Sahiplik | MAX ve host ACPI aygıtları var; bu iki sürücünün başlaması donanım olay zincirini bağlamıyor. |
| 2. Platform | CMGP, USI ve pinctrl hazırlığı kimlik/kısa transferlerle doğrulanmış. |
| 3. I2C mesajı | **Kısmen çalışıyor.** Sekiz bayt timeout kanıtı nedeniyle “taşıma katmanı tamamlandı” kararı geri alındı; FIFO düzeltmesinin cihaz doğrulaması bekleniyor. |
| 4. Kimlik | PMIC/HW/FW okunuyor; firmware değiştirilmedi. |
| 5. Kesme | Read-to-clear register olayları var; gerçek HSI2C/MAX ISR teslimi uygulanmış değil. Aşağıdaki DT kaynak bağlantısı bulundu. |
| 6. Init | Init opcode/read-back'leri başarılı, fakat bütün firmware/FSM hata durumları dışlanmış değil; snapshot'lar geçersiz. |
| 7. Attach/rol | CC pin olayı var; `CC_SOURCE` yok. SOURCE'un yerine pin/Rp/Rd tahmini kullanılmaz. |
| 8. VBUS | Source dalı var; cihazda açılmadı. `.29` Linux'taki VBUS-önce sırasını düzeltir. |
| 9. MUIC | OPEN read-back başarılı; gerçek attach üzerinde USB_AP henüz cihazda doğrulanmadı. |
| 10. Rol bildirimi | **Eksik.** BYN0002 hâlâ sabit HOST başlatıyor; MAX CC/PD bildirimine bağlı başlama/durma sözleşmesi yok. `_DEP` bunun yerine sayılamaz. |
| 11. PHY/runtime PM | Host PHY registerları açık görülüyor; Linux'taki bağlantı temelli runtime resume/suspend bütünü doğrulanmış değil. |
| 12. xHCI | Started/RUN var. IRQ273, root-hub veya alıcı transferlerinin tamamlandığı anlamına gelmez. |
| 13. Port | CCS/PED sıfır; bağlantı henüz yok. Yeni foto sayfası sonradan takma/çıkarma sonrası güncel sonucu gösterir. |
| 14. Enumeration | Logitech USB kimliği bu Windows kanıtında yok. |
| 15. HID | Servislerin Start=3 olması yeterli değil; mevcut fare PnP sorgusu aygıt bulmamış. |
| 16. Detach | OPEN/OTG-off başlangıcı var; önce başarılı attach görülmediği için tamamlanmış bir detach çevrimi doğrulanmadı. |

**IRQ kaynağı konusunda önceki ifade düzeltilmiştir.** Depodaki birleştirilmiş
`mu-silicium-overlay/Resources/DTBs/beyond2lte.dts` içinde:

- `hsi2c@15D60000` interrupt tuple'ı `<0 0x52 0>`: SPI 82, GIC GSIV 114
  (`0x72`). Bu, Linux `/proc/interrupts` satırını Windows'a kopyalayarak
  değil, DT'nin SPI tanımından `+32` ile türetilmiştir.
- MAX `irq-gpio=<0x135 6 1>`; `0x135` GPA1 bankıdır. GPA1 interrupt
  listesinin 6. pini SPI `0x13`/19'a gider: GIC GSIV 51 (`0x33`). Bank,
  `pinctrl@15850000` altındadır; pin işlevi `0xf` olarak tanımlanmış.
- Bu bağlantılar **kaynak düzeyinde bellidir**; mevcut ACPI BYN0004 `_CRS`
  bunları henüz taşımıyor. EINT türü, maskesi, pending/ack, pinctrl sahipliği
  ve Windows interrupt yaşam döngüsü kurulmadan yalnız iki numara eklemek
  bir IRQ portu sayılmaz. Fiziksel MAX pininin active-low olması GIC uç
  polaritesini tek başına belirlemez.

Tam Linux eşdeğerliği için 5 ve 10. aşamalar açık kalır: gerçek interrupt
sahipliği/işleme, CC/PD veri-güç rolü ayrımı, MAX'tan DWC3'e olay bildirimi,
xHCI child'ın bağlantı/power yaşam döngüsü ve detach'te ters sıra gerekir.
`.29` bunların yapıldığını iddia etmez. Statik HOST, sürekli polling veya
registry'deki READY'yi okuyan bir kapı nihai Linux akışının yerine kabul
edilmez. Fiziksel attach tamamlanmadan host'u yeniden başlatmak, SOURCE
uydurmak, maskeli AttachSrcErrI'yi başarı olayı saymak veya tek SYSMSG örneği
üzerinden firmware resetlemek bu eksikleri kapatmaz. Linux SYSMSG işleyicisi
karar öncesi registerı üç kez eşit okuma koşuluyla değerlendirir; `.28`
geçersiz snapshot'ları böyle bir kurtarma kararını desteklemiyor.

### WSL build ve flaşta aranacak kanıt

MAX sürümü `0.1.0.29`, parent/host `0.1.0.18`; olay binary şeması v1 kalır,
foto arayüzü v2 olur. macOS üzerinde WDK/UEFI/WIM derlenmedi. Aynı WSL repo
kökünde normal yöntem:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

Aynı turda üretilen `boot.img` ve `system.img` kullanılır. MAX `.sys`, kontrol
EXE'si ve startnet'in birlikte WIM'e girmesi gerekir. İlk kabul ölçütü
`MAX FOTO v2`, `RX_FAIL=0`, olaylarda `v=1/nt=0` ve gerçek ham registerlardır.
Bunlar beklenen sonuçtur, gerçekleşmiş test sonucu değildir. Hata sürerse
RX_NT/WANTED/READ ve ilgili olay sayfası birlikte fotoğraflanır.

USB işlevinin kabulü ayrıca gerçek SOURCE `2`, OTG SW/HW `1/1`, MUIC `09`,
USB portunda CCS/PED, USB kimliklerinde `VID_046D&PID_C52E`, ardından ilgili
HID arayüzü ve fiziksel fare hareketidir. Sonra çıkarma ile OTG off/MUIC OPEN
ve port detach görülmelidir. SOURCE hiç oluşmazsa sonraki adım geçerli
CC/SYSMSG/PD verisini yorumlamaktır; diğer aşamalar tamamlandı sayılmaz.
Klavye veya TXT dışa aktarımı önkoşul değildir.

Host doğrulaması: gerçek C fonksiyonlarıyla FIFO/timing/hata testi, CC/OTG/
MUIC sıralama testi, olay/registry/33 satır–64 sütun foto testi ve log filtresi
testi; mevcut üç kaynak güvenlik testi. Bunlar WDK derlemesi, GIC/IRQ donanım
ölçümü veya gerçek USB veri aktarımı yerine geçmez.

## Tarihsel: 4 Eylül kaynak incelemesi ve `.28` tanı tasarımı

### USB olmadan kanıt: otomatik fotoğraf ekranı

USB bağlantısı henüz çalışmadığı için TXT dışa aktarımı kullanıcı testinin
önkoşulu değildir. `.28` kontrol aracı güncellendi: `startnet.cmd`, otomatik
HOST testi sonunda `b2lusbctl.exe maxscreen` çalıştırır. Komut yazmak, klavye
bağlamak veya dosya çıkarmak gerekmez.

Ekranda önce **MAX FOTO v1 / GENEL DURUM**, sonra olay sayfaları görünür.
Her ekran **20 saniye** kalır; son sayfadan sonra yeni tur otomatik başlar.
Genel ekranda STAGE/STATUS, CC0/CC1, rol/geçiş/source sayaçları, CONTROL3,
MUIC ve OTG değerleri bulunur. Olay sayfaları en fazla 12 kayıt/33 satırdır;
mevcut 64 sütun × 41 satırlık konsola göre düzenlenmiştir. Her kaydın iki
satırında zaman, opcode, IRQ, politika CC örneği, snapshot geçerliliği/hatası
ve sekiz ham register baytı yer alır.

Bir turdaki bütün olay sayfaları aynı registry snapshot'ından gelir; yeni
olaylar bir sonraki turda alınır. 64 kayıt doluysa genel ekran + 6 olay sayfası,
yani yaklaşık **2 dakika 20 saniyelik** bir tur vardır. Genel ekrandaki alanlar
ayrı registry okumalarıdır; olay snapshot'ı ile atomik eşzamanlılık iddiası yoktur.
Bu ekran döngüsü test sonunda bilerek devam eder; donma değildir. Bir klavye
mevcutsa Q/Esc ile terminale dönülebilir, fakat fotoğraf testi bunu gerektirmez.
Görüntüleyici yalnız kullanıcı modunda uyur/registry okur; I2C veya PnP taraması
başlatmaz. Kernel MAX sürümü `.28` ve kayıt şeması v1 değişmedi; **yeni EXE ve
startnet içeren WIM** aynı WSL build yöntemiyle yeniden hazırlanmalıdır.

İstenecek kanıt artık **fotoğraflardır**:

1. A açılışında alıcı baştan takılıyken GENEL DURUM ve aynı turun bütün
   numaralı olay sayfalarını fotoğraflayın.
2. B açılışında alıcı ayrıkken GENEL DURUM ekranında `STAGE=00000070`,
   `STATUS=00000000` görülmesini bekleyin ve fotoğraflayın. Alıcıyı takın;
   tur numarası ilerledikten sonra GENEL DURUM ve bütün olay sayfalarını çekin.
3. Alıcıyı çıkarın; sonraki turda aynı ekranları tekrar çekin. Hangi fotoğraf
   grubunun takılmadan önce, takıldıktan sonra ve çıkarıldıktan sonra olduğunu
   belirtin. Sayfalardaki tur/D0 kimlikleri grupları eşleştirmeyi sağlar.

TXT dosyaları yerel yardımcı kayıt olarak kalır. Aşağıdaki terminal/TXT
komutları yalnız terminal veya kalıcı depolama erişimi mümkün olduğunda
kullanılabilecek ek yöntemlerdir; USB çalışmadan bunları dışarı çıkarma talebi yoktur.

**Son cihaz kanıtı hâlâ `.27`dir. `.28` aşağıda açıklanan gözlem değişikliğidir;
henüz WSL/WDK ile derlenmiş veya telefonda doğrulanmış değildir.** Alttaki
16 adımlı tablo ve zaman çizgisi `.27` devir kaydını korur. ForceError ve
bir sonraki değişiklik hakkındaki yorumlar için bu bölüm önceliklidir.

### Linux kaynak incelemesinin düzelttiği teşhis

Karşılaştırma, `SAMSUNG_ORIGINAL_USB_STACK.md` içinde sabitlenen
`ianmacd/d2s@d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39` üzerinden yapıldı.
Bu GPL referansının çalışan recovery çekirdeğiyle aynı derleme olduğu iddia
edilmez; canlı log ve kaynak incelemesi ayrı kanıtlardır.

- `max77705_ccpinstat_irq()` CC pin durumunu okur/kaydeder; `CC1_ACTIVE`
  görünce yazılımla `cc_SOURCE` üretmez.
- `max77705_ccstat_irq_handler()` önce `CC_STATUS0 & 7` değerini okur.
  Sıfır olmayan durum için `PLUG_ATTACHED` kaydeder; `cc_SOURCE=2` dalına
  girdikten sonra güç rolü ve VBUS işlerini yapar. Bu yüzden
  `PLUG_ATTACHED -> SOURCE` bir donanım programlama tarifi değildir.
  [CC işleyicisi](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_cc.c#L539)
- `max77705_set_CCForceError()`, incelenen USBC kaynağında manuel CC-open
  **etkinleştirme** dalından çağrılır. CC-open bırakma dalı yalnız
  `LockErrorEn` temizler. Normal attach için eksik bir ForceError çağrısı
  bulunduğu doğrulanmadı; önceki “attach sonrasında ForceError koordinasyonu”
  teşhisi geri çekildi.
  [CC-open iş akışı](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L1017)
- Windows `.27` opcode bekleme yolu `UIC_INT` registerını komuttan önce ve
  cevap beklerken okuyup temizler; APCMDRES dışındaki bitleri bu yolda
  korumaz. `B2LMaxUicInterruptSeen` yalnız normal taramayı kapsadığı için
  bütün init olaylarının kaydı değildi. Windows ayrıca `USBC_STATUS2/SYSMSG`
  okumuyordu. Linux SYSMSG işleyicisi bu alanı okur; WDT/POR durumlarında
  ayrı kurtarma yolları vardır. `.28` bunları yalnız gözler; reset/kurtarma
  davranışı eklemez.
  [SYSMSG işleyicisi](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/drivers/ccic/max77705_usbc.c#L2526)
- `.27` `CC_SEEN=0x6d` birikimi `AttachSrcErrI` bit 5'i de içerir. Ancak
  `CC_INT_MASK=0x20` aynı olayı maskeler; ham okunması Linux'ta nested ISR
  çalıştığını veya hatanın hâlâ aktif olduğunu kanıtlamaz. İlgili olayda
  `CC_STATUS1` bit 2 ile birlikte değerlendirilmelidir.
  [Register/bit tanımları](https://github.com/ianmacd/d2s/blob/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39/include/linux/ccic/max77705.h#L112)

Önemli kanıt sınırı: bu makinedeki `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
dosyasının AFTER_PLUGGED log bölümü `461.183842` ile başlıyor. Devirdeki
`461.169117 / PLUG_ATTACHED / cc_SOURCE` satırları bu kopyada bulunmuyor.
Toplama betiği filtrelenmiş logun son 300 satırını saklıyor; erken attach
kayıtları kesilmiş olabilir. Aşağıdaki milisaniye tablosunun erken bölümü
önceki devirden aktarılmıştır, bu yerel dosyayla yeniden doğrulanmış değildir.
MUIC `06 09`, host açılışı ve alıcının enumeration kanıtı dosyada mevcut.

Linux betiği önce alıcı ayrıkken, sonra takıldıktan sonra snapshot alıyor;
`.27` Windows testi ise alıcı baştan takılıyken yapıldı. Aynı alıcı kullanımı
aynı başlangıç durumu demek değildir. `.27` sayaçları, **başarılı politika
örneklerinde** SOURCE görülmediğini kanıtlar; init öncesi veya iki örnek
arasındaki bütün geçici donanım durumlarını dışlamaz.

### `.28` neyi kaydeder?

`b2lmax77705.sys=0.1.0.28`; parent ve host INF sürümleri `0.1.0.18` kalır.

| Olay `o` | Yakalama noktası | `irq` / `policy` anlamı |
|---|---|---|
| 1 | Başarılı kimlik probe'undan sonra, ilk init opcode'undan önce | IRQ okunmadı; `policy=ffffffff` |
| 2 | Opcode öncesi eski UIC olaylarını temizleyen okuma | Yalnız UIC geçerli; `op` sıradaki komuttur, olayın nedeni olduğu iddia edilmez |
| 3 | Opcode cevabı beklenirken sıfır olmayan UIC okuması | APCMDRES ile aynı bayttaki SYSMSG dahil bütün bitler korunur |
| 4 | Normal taramada ilk CC örneği, ham CC0/CC1 değişikliği veya sıfır olmayan child IRQ | Politikanın CC örneği ve dört IRQ baytı korunur |
| 5 | IRQ temizlendikten sonraki politika CC okuması başarısız | IRQ korunur; eski CC kullanılmaz, `policy=ffffffff` |

Her olayda 100 ns saat kaynağından zaman, sıra numarası ve `0x06..0x0d`
registerlarının sekiz baytlık salt okunur snapshot'ı saklanır:

```text
raw = USBC_STATUS1 / SYSMSG / BC_STATUS / FW_MINOR / CC0 / CC1 / PD0 / PD1
irq = VDM PD CC UIC                 (hex DWORD, yüksek bayttan düşüğe)
policy = CC1 CC0                    (yalnız o=4; diğerlerinde ffffffff)
v=1: raw geçerli                    v=0: raw yorumlanmaz; nt okuma hatası
```

Snapshot, IRQ ve politika örneğinden **sonra** okunur; atomik donanım kaydı
değildir. `policy` ile `raw` CC alanlarının farklı olması bir geçişi veya
örnekler arası farkı araştırmaya yarar. Snapshot SOURCE gösterse bile bu veri
politikanın yerine geçip VBUS açmaz. Tanı okuması başarısızsa `SNAP_FAIL`
artar; asıl opcode/politika çağrısının hata veya başarı sonucu korunur.
`o=5` yolunda ek I2C okuması yapılmaz; `nt` asıl politika CC okuma hatasıdır.
Bu durumda `v=0` ve `SNAP_FAIL` artışı, snapshot'ın alınamadığını belirtir;
mevcut taşıma hata ayrıntıları korunur ve `STAGE/STATUS` da yayımlanır.

Sabit 3.128 baytlık kayıt alanı D0 başına ilk 16 olayı saklar, sonraki 48
slotta son olayları döndürür. `overwritten`, son bölümde üstüne yazılmış olay
sayısıdır; sıra numaralarındaki boşluk bu kaybı görünür kılar. Yeni D0'da kayıt
sıfırlanır; `MAX_EVENT_START_100NS` oturumları ayırır. Sıfır UIC bekleme
örnekleri ve değişmeyen, olaysız CC taramaları ek I2C okuması/kayıt üretmez.
`o=5` dışındaki olay başına ek bir sekiz baytlık I2C okuması vardır; gecikmeye etkisi cihazda
ölçülmelidir. Timer aralıkları ve güç/rol işlemleri değiştirilmedi.

Kayıt, mevcut tanı yayınında ve yalnız değiştiğinde tek `REG_BINARY`
`B2LMaxEventLog` değeriyle yazılır. Opcode döngüsünde registry yazılmaz.
`b2lusbctl.exe maxevents` bu değeri tek okumada alır; BYN0002 arayüzüne
bağımlı değildir. Son yayımlanmış olayları gösterir, canlı donanım sorgulamaz.
Normal tarama o ana kadar olay görmediyse `MAX_LAST` güncel CC ölçümü değildir.

`startnet.cmd` iki mevcut MAX toplama çağrısında bu çıktıyı alır:

- `X:\B2LUSB-MAX77705-EVENTS.TXT`: iki toplamanın tarih/saat başlıklı birleşik kaydı;
- `X:\B2LUSB-MAX77705-EVENTS-LAST.TXT`: son toplama;
- mevcut `X:\B2LUSB-MAX77705-SUMMARY.TXT`: yalnız kısa `MAX_` satırları.

Mevcut CACHE doğrulamalı dışa aktarma yolu `B2LUSB-*.TXT` dosyalarını zaten
kapsar; CACHE yolu başarılı değilse kalıcı dışa aktarım varsayılmaz. SYSMSG
sayaçları firmware reset sayısı değildir: ham kesme gözlemlerini sayar.
Linux'taki üçlü SYSMSG tutarlılık kontrolü/kurtarma işlemleri bu tanı sürümüne
taşınmadı. Tek bir `SYSMSG=03` veya `05`, sırasıyla WDT/POR hipotezini araştırma
gerekçesidir; zaman sırası ve tekrar okunmuş kayıtlarla desteklenmelidir.

### WSL build ve flaş sonrası test sözleşmesi

Bu makinede Windows sürücüsü/UEFI/WIM build'i yapılmaz. `DEVIR.md` bölüm
0.5–0.9'daki WSL2 Ubuntu + Windows ARM64 MSVC/WDK iş bölümü kullanılır.
WSL'deki güncel repo kökünde, kaynaklar bu değişikliği içerirken:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

İlk tar yalnız `boot.img`, ikincisi yalnız `system.img` içermelidir. Aynı
turun BOOT ve SYSTEM çıktıları Odin AP ile, Re-Partition kapalıyken kullanılır.
Bu baseline profilinde eski birleşik `beyond2lte-usb-diag.tar` kullanılmaz.
`B2L_SKIP_MSVC_BUILD` / `B2L_ALLOW_STALE_SYS` ile eski çıktılar atlatılmaz;
`b2lusbctl.exe` de değiştiği için yeniden derlenip WIM'e girmelidir.

İlk kontrol: manifestte `B2LUSB_MAX77705_DRIVER_VERSION=0.1.0.28`,
parent/host için `.18`; olay özetinde `MAX_EVENTS v=1`, `count>0`,
`SNAP_FAIL=0` beklenir. `MAX_EVENT_READ_ERROR` veya `FORMAT_ERROR` başarılı
tanı sayılmaz. İlk kimlik/init başarısızsa boş olay kaydı mümkün olabilir;
`STAGE/STATUS` ile birlikte değerlendirilir.

İki ayrı açılış karşılaştırılır; her açılışın logu/fotoğrafı yeniden
başlatmadan önce ayrı saklanır:

1. **A — baştan takılı:** aynı adaptör/alıcı açılış boyunca takılı kalır.
   `.27` ile aynı koşulda olay kaydı, kritik MAX özeti ve xHCI çıktısı alınır.
2. **B — hazır olduktan sonra takılı:** Windows alıcı ayrıkken açılır.
   MAX `STAGE=0x70, STATUS=0` görüldükten sonra aynı alıcı takılır; 10 saniye
   beklenir, olay kaydı yeniden alınır. Sonra çıkarılır, 10 saniye beklenir
   ve ikinci kayıt alınır. Otomatik iki toplama bittikten sonraki fiziksel
   değişiklikler için aşağıdaki komutlarla yeni dosyalar alınmalıdır.

WinPE terminalinde, uygun fiziksel adımın ardından:

```bat
X:\Windows\B2LUSB\b2lusbctl.exe maxevents > X:\B2LUSB-MAX77705-B-ATTACH.TXT
X:\Windows\B2LUSB\b2lusbctl.exe status > X:\B2LUSB-B-ATTACH-STATUS.TXT
rem Aliciyi cikardiktan ve 10 saniye bekledikten sonra:
X:\Windows\B2LUSB\b2lusbctl.exe maxevents > X:\B2LUSB-MAX77705-B-DETACH.TXT
```

Terminal girdisi mümkün değilse, ilk otomatik MAX dökümündeki READY'den
sonra alıcı takılıp ikinci otomatik toplamaya kadar takılı tutulabilir.
Bu durumda testin tam hangi aşamasında takıldığı fotoğrafla belirtilmelidir;
kontrollü sonradan çıkarma kaydı ayrıca alınana kadar detach doğrulanmış sayılmaz.

Bu sürümün ilk başarı ölçütü **nedenin görünürleşmesidir**; fareyi çalıştırdığı
iddia edilmez. SOURCE oluşursa politika `CC_STATE=2`, `CC_SRC_EV>0`, ardından
MUIC `0x09`, OTG aktif, xHCI `CCS=1` ve `046d:c52e` beklenir. Ayrık durumdan
takma senaryosunda `CC_TRANS>0` da beklenir; ilk politika örneği zaten SOURCE
ise mevcut sayaç tasarımı gereği `CC_TRANS=0` kalabilir. SOURCE hâlâ yoksa
özellikle `o=1/2/3` kayıtları, SYSMSG, CC1 bit 2 ve A/B farkı paylaşılmalıdır.

17 dakikalık tanı gecikmesinin faz ölçümü ve MAX READY → HOST kapısı ayrı
işlerdir. Bu değişiklik yalnız gözlem ekler; konsoldaki eski “observer 12
saniye” mesajını gerçek 5 saniyelik ayarla eşleştirir.

## Kapsam ve kanıt sürümleri

Bu belge, Logitech `046d:c52e` alıcısının Samsung Linux/Lineage altında fiziksel olarak takılmasından HID klavye ve farenin oluşmasına kadar çalışan akışı, mevcut Windows sürücüleriyle yan yana karşılaştırır.

- Linux kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
- Son Windows cihaz kanıtı ve cihazda doğrulanmış kaynak: ekrandaki `b2lmax77705` sürümü `0.1.0.27`
- `.27` cihaz kanıtı: kullanıcının son flaştan sağladığı sekiz ekran görüntüsü. Logitech `046d:c52e` alıcısı, çalışan recovery/Linux analizindekiyle aynı biçimde bütün Windows testi boyunca fiziksel olarak takılıydı.
- Gözlenen duvar saati: ilk fotoğraf ile test sonunu gösteren son fotoğraf arasında yaklaşık 17 dakika. Scriptte aşama zaman damgaları bulunmadığından bu süre tek bir komuta kesin olarak yüklenemez.

## `.27` cihaz sonucunun özeti

`.27`, 6. probe/init adımını artık cihaz üzerinde baştan sona doğrulamıştır. Sürücü `STAGE=0x70`, `STATUS=0`, `PLATFORM_OK=1` durumuna ulaşmış; Linux çalışma maskeleri, `CCCTRL1=0x1f` ve probe-final CC-open işlemi tamamlanmıştır. Yeni kanıt `CCOPEN_OK=1`, `CTRL3_BEFORE=0x64`, `CTRL3_AFTER=0x64` değerlerini verir. `0x64 & 0x02 == 0` olduğundan `LockErrorEn` zaten kapalıdır: kod ve read-back çalışmıştır, fakat donanımda değiştirilecek bir bit yoktur. Böylece 6. adım kapanmış, “attach'i LockErrorEn engelliyordu” hipotezi yanlışlanmıştır.

İlk işlevsel uyuşmazlık artık kesin biçimde 7. Type-C attach/rol adımıdır. Fiziksel Logitech alıcı takılıyken `CC0/CC1=0x40/0x00`, yani `CCPinStat=CC1_ACTIVE` görülür; buna karşın `CC_STATE=CC_PREV=0`, `CC_TRANS=0` ve `CC_SRC_EV=0` kalır. `.27` boyunca `CC_SAMPLES=0x71f` (1.823), `CC_IRQS=4` ve `CC_SEEN=0x6d` elde edilmiştir. Olay registerları canlıdır, fakat MAX77705 durum makinesi fiziksel CC1_ACTIVE bilgisini Linux'taki `cc_SOURCE` rolüne çevirmemektedir.

Kesme registerlarının okunabilmesi, MAX77705 GPIO kaynaklı kesmenin Windows'a donanımsal IRQ olarak teslim edildiğini kanıtlamaz. Ekrandaki IRQ `273`, BYN0003/USBXHCI kaynağıdır; Linux'ta görünen HSI2C IRQ114 veya MAX77705 parent GPIO IRQ318 değildir. GPA1_6 için doğrulanmış ACPI/GIC kaynağı bulunmadığından `.27` sahte ISR kurmaz; TOP `INTSRC`, dört read-to-clear USBC baytı ve hemen ardından CC status timer ile okunur.

Polling azaltımı cihazda doğrulanmıştır. xHCI gözlem sayacı önceki yaklaşık 7.887 değerden 160'a inmiştir; bu yaklaşık `%98` azalmadır ve 5 saniyelik sınırlı gözlemle uyumludur. MAX sayacı ilk fotoğraf çekilmeden önce çalışmaya başladığı için `CC_SAMPLES=1.823` değerini 17 dakikalık fotoğraf aralığına bölmek örnekleme hızına bir üst sınır verir: toplam en fazla yaklaşık `1,79 örnek/s`; ilk 50 hızlı örnek çıkarıldığında geç dönem en fazla yaklaşık `1,74 örnek/s` veya örnek başına en az `575 ms` görünür. Bu değerler kalıcı 50 Hz agresif polling'i ve 1 ms xHCI gözlemcisini 17 dakikanın baskın nedeni olmaktan çıkarır. Gecikme artık daha çok seri çalışan PnP/SetupAPI, registry dökümleri ve ETW dışa aktarma tanılarında aranmalıdır; ancak kesin aşamayı söylemek için `startnet.cmd` içinde aşama başlangıç/bitiş zaman damgaları gerekir.

Ana mimari fark şudur:

```text
Samsung Linux:
MAX IRQ -> CC=SOURCE -> VBUS/OTG + rol bildirimi -> MUIC USB_AP
        -> DWC3/PHY runtime resume -> xHCI -> USB enumerate -> HID

Windows `.27` cihaz sonucu:
ACPI _DEP yalnızca BYN0004 sürücüsünün başlamasını bekler
        +-> BYN0002 hemen PHY/DWC3 HOST yapar -> USBXHCI çalışır
        `-> BYN0004 zamanlayıcısı platform + HSI2C + MAX init'i tamamlar
            -> CONTROL3 read/masked-write/read-back başarılı;
               LockErrorEn zaten kapalıdır (`0x64 -> 0x64`)
            -> TOP + USBC IRQ blok okuma -> hemen CC_STATUS örneği/geçiş kaydı
            -> takılı Logitech alıcıya rağmen CC1 pini görülür,
               fakat CCStat=No Connection kalır
            -> güvenli yol: VBUS kapalı + MUIC OPEN -> xHCI attach göremez
```

## Adım adım yan yana karşılaştırma

| Aşama | Çalışan Samsung Linux akışı | Mevcut Windows kaynağının hedefi | Son Windows flaşında gözlenen | Sonuç |
|---|---|---|---|---|
| 1. Donanım sahipliği | `15d60000.hsi2c -> i2c-14 -> 14-0066 -> max77705-usbc`; TOP/USBC/charger alt adresleri tek MFD altında | `BYN0004`, HSI2C `0x15D60000`, USI `0x15C22000`, CMGP CMU ve ilgili pinctrl sayfalarını tek başına eşler; `0x66/0x25/0x69` adreslerini kendisi yönetir | `.27`de `BYN0004` Started; `0x15C00000`, `0x15D60000`, `0x15C22000` ve `0x15C30000` olmak üzere dört MMIO alanı atanmış | Eşleşen sahiplik modeli ve kaynak kapsamı cihazda doğrulandı |
| 2. HSI2C güç/saat hazırlığı | Linux pinctrl ile GPM12-0/GPM13-0'ı işlev 2 yapar; DT 200 MHz ister, clock framework mevcut hızı okur; QCH/gate hazırlanır ve 400 kHz timing hesaplanır | `.24`, aynı pinctrl ve CMGP3 alanlarını BYN0004'e verir; 200 MHz'i deterministik kurar, QCH'yi ve buna karşılık gelen FS timing alanlarını yazar, geri okur | `PLATFORM_OK=1`, `CMGP_MUX3=1`, `CMGP_DIV3=1`, `CMGP_QCH3=0x3f0001`, `GPM12_CON=GPM13_CON=2` | Eşleşme cihazda doğrulandı; `.23`teki ilk engel giderildi |
| 3. HSI2C mesajı | Samsung `exynos5_i2c_xfer` adres, yön, uzunluk ve RUN sırasını kurar; Linux ad alanında IRQ 114 ile tamamlanır | Windows aynı mesaj kurulumunu yapar; karşılık gelen GIC/ACPI interrupt kaynağı doğrulanmadığı için bounded sleeping polling ile tamamlanmayı gözler | Son işlem `I2C_REGADDR=I2C_TRYADDR=0x0701a400` (`0x69` charger), `I2C_RETRY=0`, `I2C_PHASE=0`, `I2C_ERR=0`, genel `STATUS=0` | Taşıma katmanı çalışıyor; Linux IRQ numarasını doğrudan kullanmamak bu flaşın blokajı değil |
| 4. MAX kimlik doğrulama | Parent MFD TOP `0x66`, USBC `0x25`, charger `0x69` istemcilerini oluşturur | TOP ID/rev, USBC HW/FW ve charger registerları aynı sırayla okunur | `PMIC_ID=0x15`, `PMIC_REV=0x2`, `USBC_HW=0x1a`, `USBC_FW=0x1a`, `USBC_MINOR=0x10` | MAX77705 fiziksel erişimi ve kimlik doğrulaması başarılı |
| 5. Olay mekanizması | GPIO parent IRQ 318; TOP `INTSRC` sonrası alt CC/MUIC/PD/APCMD registerları blok okunur ve nested IRQ işlenir | Doğrulanmış MAX GPIO ACPI kaynağı yok; TOP `INTSRC` + dört read-to-clear USBC baytı tek blokta, hemen ardından CC status okunur. 20 ms yalnız ilk 50 CC örneği, sonra 200 ms hedeflenir | `.27`: `TOP_SEEN=0x9`, `CC_SEEN=0x6d`, `PD_SEEN=0xc0`, `CC_IRQS=4`, `CC_SAMPLES=0x71f`; BYN0004 üzerinde IRQ kaynağı yok | Okuma sırası ve sınırlı hızlı pencere cihazda çalıştı; olay görünürlüğü korunuyor |
| 6. Probe/init opcodes ve kesme düzeni | USBC/PD probe; `UIC/CC/PD/VDM` maskeleri önce probe, sonra çalışma vektörüne geçer; CCCTRL ve firmware komut kuyruğu tamamlanır; en sonda manual CC-open off isteği CONTROL3 `LockErrorEn` bitini temizler | Charger unlock; `57 01`; `00/20/1c/ff`; `54 00`; `0c 1f` + `0b`; `00/20/00/f0`; sonra `1c` read, gerekirse `1d` masked write ve tekrar `1c` read-back | `.27`: `CCOPEN_OK=1`, `CTRL3_BEFORE=CTRL3_AFTER=0x64`; bit 1 zaten sıfır olduğundan write gerekmemiştir | 6. adım cihazda kapandı; CONTROL3 kodu doğru, LockErrorEn blokaj hipotezi yanlışlandı |
| 7. Fiziksel takılma ve CC | IRQ'da `CC1_ACTIVE`, `PLUG_ATTACHED`, `cc_SOURCE`; veri rolü DFP, güç rolü SRC olur | Probe-final normalizasyondan sonra CC örnek/geçiş/source sayaçlarını sürdürür ve yalnız `CC_SOURCE=2` ile HOST/OTG'yi açabilir | `.27`: aynı Logitech alıcı takılıyken `CC0/CC1=40/00`, `CC_STATE=CC_PREV=0`, `CC_TRANS=0`, `CC_SRC_EV=0`; 1.823 örnek, dört CC interrupt örneği ve birikimli olay baytları canlı | İlk işlevsel blokaj artık kesin olarak 7'dedir; sıradaki karşılaştırma CC event/FSM ve ForceError koordinasyonudur |
| 8. VBUS/charger OTG | `vbus_turn_on_ctrl(1)`, CHGIN-OTG; charger mode `0x04 -> 0x0e`; VBUS HIGH bildirilir | Charger kilidi açılır, interrupt maskesi ayarlanır, `CNFG_00` mevcut moda göre `0x0e` benzeri OTG moda geçirilir ve read-back doğrulanır | `CHG_UNLOCK=1`, `CHG_CFG06=0x0c`, `CHG_MODE=0x15`; `OTG_SW=OTG_HW=0` | Source görülmediği için VBUS güvenli biçimde kapalı tutuluyor; xHCI `PP=1` gerçek konektör VBUS'unu kanıtlamaz |
| 9. MUIC D+/D- yönlendirme | OTG algılanır, switch `0x09`; opcode `06 09` ile USB yolu AP'ye verilir | Source durumunda `CONTROL1_WRITE 06 09`, ardından `CONTROL1_READ 05` ile `0x09` doğrulanır; detach'te `OPEN=0x3f` seçilir | `MUIC_REQ=0x3f`, `MUIC_CTL1=0x3f`, `MUIC_OK=1` | İletişim ve read-back başarılı, fakat CC kararı nedeniyle D+/D- kasıtlı olarak OPEN bırakıldı |
| 10. Rol bildirimi ve DWC3 | `Turn On Host(DFP)` ve `USB_HOST_ATTACHED`; DWC3 role-switch work başlar | Windows'ta Linux notifier zinciri yok. `BYN0002` PrepareHardware içinde doğrudan sabit HOST seçer | BYN0002 ve BYN0003 Started | Windows bu aşamayı MAX sonucundan önce yaptı |
| 11. PHY/runtime resume | PHY connect, LDO on, runtime resume, UTMI init, tune, reset release | PMU izolasyonu kaldırılır; q-channel, PHY reset, UTMI, tune ve DWC3 core reset uygulanır; USB3 gizli tutulur | PHY registerları beklenen host değerlerinde | Host tarafının bu bölümü büyük ölçüde çalışıyor |
| 12. xHCI başlatma | DWC3 host olduktan sonra xHCI başlar, USB bus 1/2 kaydolur | `USBXHCI` inbox sürücüsü ACPI `BYN0003`'e bağlanır; parent önceden DWC3'ü HOST'a hazırlar | xHCI `RUN=1`, command ring/DCBAA/config kurulmuş | Controller çalışıyor; konektör yolu hazır değil |
| 13. Port bağlantısı | Yaklaşık 357 ms sonra full-speed aygıt görülür | USBXHCI'nin PORTSC değişimi görmesi beklenir | Logitech alıcı fiziksel olarak takılıyken controller `RUN=1`, `DCBAA=1`, `CFG=1`; portlar `CCS=0`, `PED=0`, `PLS=5/RxDetect`; sınırlı `.27` gözleminde 160 örnek ve sıfır transition | Elektriksel bağlantı xHCI'ye ulaşmıyor; gözlem maliyeti yaklaşık `%98` azaldı |
| 14. Enumeration | `new full-speed device`, `046d:c52e`, descriptor/config/interface işlemleri | Inbox hub/USB stack aynı standart enumeration'ı yapar | Aynı Logitech alıcı takılı olmasına rağmen root hub altında aygıt yok | Enumeration başlamadı; sorun HID sınıf sürücüsünde değil |
| 15. HID bind | Arayüz 1.0 `hid-generic` keyboard, 1.1 mouse | `USBHUB3 -> HIDUSB -> MOUHID/MOUCLASS` zinciri bağlanır | Hizmetler mevcut fakat “No devices were found” | Fare sorununun kökü HID değil, daha aşağıdaki Type-C/VBUS/MUIC yoludur |
| 16. Detach | VBUS kapatılır, roller geri alınır, MUIC açılır, DWC3 device/idle yoluna döner | Windows CC source dışına çıkınca önce OTG kapatır, sonra MUIC `OPEN=0x3f` doğrular | `.27`de takılı alıcıya rağmen attach oluşmadı; güvenli ayrılma sonucu `OPEN=0x3f` ve OTG off doğrulandı | Güvenli durum kanıtlandı; attach oluşmadığı için gerçek detach geçişi sınanamadı |

## Çalışan Linux zaman çizgisi

Fiziksel attach olayının başlangıcı yaklaşık `461.169117` kabul edildiğinde:

| Zaman | Linux olayı |
|---:|---|
| `+0.000 ms` | `PLUG_ATTACHED`, `cc_SOURCE` |
| `+0.746 ms` | `vbus_turn_on_ctrl enable=1` |
| `+2.802 ms` | charger mode `0x04 -> 0x0e` |
| `+8.336 ms` | DFP/SRC/ATTACHED kararı |
| `+14.737 ms` | `Turn On Host(DFP)` |
| `+14.799 ms` | `USB_HOST_ATTACHED` |
| `+16.776 ms` | MUIC opcode `06 09` |
| `+16.818 ms` | DWC3 host work ve PHY açılışı |
| `+24.795 ms` | `AUTOIBUS 57 01` kuyruğa alınır |
| `+35.519 ms` | İlgili opcode cevapları başarıyla tamamlanır |
| `+353.635 ms` | Logitech alıcı full-speed olarak görülür |
| `+543.232 ms` | HID keyboard bind olur |
| `+610.512 ms` | HID mouse bind olur |

Bu süreler Windows için bir performans hedefi olmaktan önce bir sıralama kanıtıdır: MAX/CC/VBUS/MUIC tarafı yaklaşık ilk 35 ms'de anlamlı biçimde tamamlanmış, USB enumeration ancak bundan sonra başlamıştır.

## Komut ve register eşleşmesi

| Amaç | Samsung Linux kanıtı | Windows uygulaması | Durum |
|---|---|---|---|
| USB D+/D- yolunu AP'ye ver | `CONTROL1_WRITE: 06 09` | Source görüldüğünde `06 09`, sonra `05` read-back | Uygulama var; `.27`de source görülmediği için bu dala girilmedi |
| Firmware AUTOIBUS | `57 01`, cevap `57` | `57 01`, APCMDRES ve cevap opcode kontrolü | `.27` READY sonucu, opcode taşıma yolunun tamamlandığını doğruluyor |
| Otomatik VBUS'u kapat | Samsung init davranışı | `54 00` | `.27` init zincirinde başarıyla tamamlandı |
| Kesme maskeleri | Çalışan log: `UIC/CC/PD/VDM=00/20/00/f0`; probe sırasında `PD/VDM=1c/ff` ara durumu | `00/20/1c/ff`, init sonrası `00/20/00/f0`; her register readback ile doğrulanır | `.27`de `00/20/00/f0` ve `MASKS_OK=1`; cihazda doğrulandı |
| CC kontrolü | `CCCTRL1_W 0c 1f`; sonrasında IRQ tabanlı rol durumu | `0c 1f`, `0b` readback=`1f`; TOP/USBC kesme blok okumasından hemen sonra `CC_STATUS0/1` | `.27`de `CCCTRL1=0x1f`; olay baytları yakalanıyor fakat takılı alıcıya rağmen `CC_STATE=0`, `CC_TRANS=0` |
| Probe-final CC-open bırakması | `cc_open_req=1`; `pdic_manual_ccopen_request(0)`; `CONTROL3_R=0x1c`, gerekirse `LockErrorEn` bit 1 temizlenerek `CONTROL3_W=0x1d` | Runtime maskelerinden sonra aynı read/masked-write/read-back; `CCOPEN_OK`, `CTRL3_BEFORE`, `CTRL3_AFTER` yayımlanır | `.27`: `CCOPEN_OK=1`, `0x64 -> 0x64`; LockErrorEn zaten kapalıydı, dolayısıyla işlem değişiklik yapmadan doğrulandı |
| Charger OTG | `CNFG_00 0x04 -> 0x0e` | Source görülürse mevcut moda göre `0x0e/0x0f/...`; read-back ve rollback | Source görülmediği için çalıştırılmadı; `CHG_MODE=0x15` OTG içermiyor |
| Detach izolasyonu | VBUS off, switch open | OTG off, ardından MUIC `0x3f` | `.27`de `MUIC_OK=1`, `MUIC_CTL1=0x3f`, `OTG_SW/HW=0` ile cihazda doğrulandı |

## Kritik farklar ve önem sırası

### 1. Host başlangıcı MAX77705 READY durumuna bağlı değil

ACPI `_DEP`, `BYN0002`yi `BYN0004`e bağlar; ancak `BYN0004` D0Entry, MAX probe'unu beklemeden timer kurup başarı döndürür. Windows bağımlılığı tamamlanmış sayar ve `BYN0002` PrepareHardware doğrudan HOST rolünü seçer. Bu nedenle MAX identity/CC/MUIC/VBUS başarısız olsa da USBXHCI başlar.

Bu, Linux sırasındaki en büyük yapısal farktır. Doğru eşdeğerlik için HOST handoff, en azından şu koşullar doğrulanmadan yapılmamalıdır:

```text
IdentityReady
ChargerUnlocked
AutoIbusConfigured
UsbcMasksConfigured
UsbcRuntimeMasksConfigured
AutoVbusDisabled
CcControlConfigured
CcOpenNormalized
CC_STATUS == SOURCE
MuicPathVerified == USB_AP
OtgHardwareActive
```

`.27` bu ayrımı kesinleştirmiştir: MAX tarafı Linux probe-final CC-open işlemi dahil READY'e ulaşsa ve CC interrupt baytlarını yakalasa bile `CC_STATUS == SOURCE`, `MuicPathVerified == USB_AP` ve `OtgHardwareActive` koşulları oluşmamıştır. BYN0002/BYN0003 yine HOST/RUN durumuna geçtiği için controller çalışır, fakat konektör zinciri hazır değildir.

### 2. `.27` 6. adımı kapattı; ilk işlevsel uyuşmazlık 7. adımda

Son Windows kanıtında platform, HSI2C, kimlik, opcode, kesme maskeleri, CCCTRL1 ve probe-final CC-open katmanları başarıyla tamamlanmış; olay tanısı da canlı veri üretmiştir:

```text
STAGE=0x70                 STATUS=0x0
PLATFORM_OK=0x1            I2C_RETRY=0x0
PMIC_ID=0x15               PMIC_REV=0x2
USBC_HW/FW/MINOR=1a/1a/10  CHG_UNLOCK=0x1
UIC/CC/PD/VDM=00/20/00/f0  MASKS_OK=0x1
CCCTRL1=0x1f               CC0/CC1=0x40/0x00
CCOPEN_OK=0x1              CTRL3_BEFORE/AFTER=0x64/0x64
TOP_SEEN=0x9               CC_SEEN=0x6d
PD_SEEN=0xc0               CC_IRQS=0x4
CC_SAMPLES=0x71f           CC_TRANS=0x0
CC_STATE/PREV=0x0/0x0      CC_SRC_EV=0x0
MUIC_OK=0x1                MUIC_CTL1=0x3f
OTG_SW/HW=0/0
```

`CTRL3_BEFORE=CTRL3_AFTER=0x64` sonucu belirleyicidir. `LockErrorEn` bit 1, `0x64` içinde zaten sıfırdır; sürücü Linux'taki işlemi uygulamış, read-back koşulunu geçmiş ve `CCOPEN_OK=1` yayımlamıştır, fakat donanım durumunu değiştirmemiştir. Bu nedenle 6→7 sınırında bulunan kod eksikliği giderilmiş olsa da attach arızasının sebebi değildir. 6. adım artık kapalı, ilk uyuşmayan adım 7'dir.

`CC0=0x40`, `bits[7:6]=1` ile stok Samsung tanımında `CC1_ACTIVE`; `bits[2:0]=0` ile `cc_No_Connection` değeridir. Aynı Logitech alıcı test boyunca takılıyken 1.823 örneğin hiçbirinde rol değişmemiştir. Dört sıfır olmayan CC interrupt örneği ve `CC_SEEN=0x6d` birikimi, TOP/USBC read-to-clear okumasının canlı olay verisi aldığını gösterir; buna rağmen `CC_TRANS=0` ve `CC_SRC_EV=0` kalmıştır. Sonuç, sorunu olayın hiç görülmemesinden çok CC olay/durum-makinesi koordinasyonuna, Linux'un attach sonrasında uyguladığı ForceError/status güncelleme akışına daraltır.

BYN0004'e atanmış gerçek interrupt kaynağı bulunmadığından bu sayaçlar parent GPIO interruptının Windows ISR'ına ulaştığı anlamına gelmez; olay timer tarafından okunmuştur. Ancak uzun test boyunca birden fazla interrupt-register örneği yakalanıp hiç SOURCE oluşmaması, “tek kısa olay polling aralığında kaçtı” açıklamasını daha da zayıflatır.

### 3. HSI2C platform hazırlığı `.24` ile cihazda doğrulandı

`BYN0004` artık HSI2C ve USI yanında CMGP CMU `0x15c00000..0x15c07fff` ile CMGP pinctrl `0x15c30000..0x15c30fff` kaynaklarını da tek sahip olarak alır. Başlatma sırası şöyledir:

1. GPM12-0 ve GPM13-0 için pull/drive sıfırlanır, function `2` seçilir.
2. `DIV_CLK_USI_CMGP3` (`+0x1824`) oran `1` ile divide-by-two durumuna alınır.
3. `MUX_CLK_USI_CMGP3` (`+0x1020`) 400 MHz CMGP bus parent'a alınır; sonuç 200 MHz'tir.
4. `USI_CMGP3_QCH` (`+0x304c`) normal enable durumuna (`bits[2:0]=1`) alınır.
5. Linux'un `200 MHz / 400 kHz` hesabıyla FS1=`0x00f00000`, FS2=`0x000000e0`, FS3=`0x00210000` alanları controller resetinden sonra kurulur.
6. Pin, clock ve timing alanları geri okunur; doğrulama başarısızsa HSI2C transferine geçilmez ve `HsI2cFailurePhase=0x08` yayımlanır.

Canlı `.24`–`.27` değerleri bu sıranın tamamlandığını gösterir: `PLATFORM_OK=1`, mux/div değerleri `1`, QCH alt üç biti `1`, GPM12/GPM13 function alanları `2` ve ilk kimlik okumasından sonraki charger erişimleri de başarılıdır. `.23`teki `ADDR1` timeout'u artık yeniden üretilmemiştir.

Linux logunda HSI2C transfer completion için görülen IRQ 114, Linux IRQ ad alanındaki sanal numaradır; Windows ACPI'ye doğrudan `114` olarak aktarılamaz. BYN0004 için karşılık gelen GIC SPI ve trigger türü doğrulanmış bir interrupt kaynağı bulunmadığından Windows tamamlanmayı PASSIVE bağlamda bounded polling ile gözler. `.24`–`.27` arasındaki hatasız kimlik/opcode/charger erişimleri bu seçimin önceki `ADDR1` arızasının veya mevcut `CCStat=No Connection` sonucunun sebebi olmadığını kanıtlar. ISR/DPC yolu ancak DT/GIC eşlemesi doğrulanıp doğru ACPI interrupt kaynağı yayımlandıktan sonra eklenmelidir. HSI2C completion kesmesi, Linux'ta IRQ 318 olarak görünen MAX77705 GPIO parent kesmesinden de ayrıdır.

Samsung DT `default-clk=200000000` ister. Vendor CAL tablosunda USI_CMGP3 için yalnız 400 MHz normal seviye bulunduğundan Linux'taki `clk_set_rate(200 MHz)` çağrısı platform durumuna göre başarısız olabilir; sürücü yine `clk_get_rate()` ile etkin hızı okuyup timing hesaplar. Windows clock framework kullanmadığı için bu belirsizliği taşımıyor: 400 MHz parent + oran `1` ile DT'nin amaçladığı 200 MHz'i açıkça kuruyor ve timing'i bu hıza göre yazıyor.

### 4. İki ayrı IRQ zinciri yerine sınırlı, doğrulanabilir polling kullanılıyor

Linux'ta HSI2C transfer completion Linux IRQ114 ile; MAX77705 CC/MUIC/PD/APCMD olayları ise Linux GPIO parent IRQ318 ve nested IRQ'larla yürür. Windows her iki alanda da polling kullanır: HSI2C transferi bounded sleeping polling; MAX olayları `.27`de ilk 50 başarılı CC örneğinde 20 ms, sonrasında ve yerleşmiş rolde 200 ms hedefli timer taramasıyla izlenir. TOP kaynak registerı önce, dört child kesme registerı tek blokta, CC status da hemen arkasından okunur. Read-to-clear örnekler ayrıca OR-birikimi ve sayaçlarla korunur. `.27` cihaz sonucu bu yolun `CC_SEEN=0x6d`, `CC_IRQS=4` ve 1.823 CC örneği ürettiğini doğrular; ancak geçiş/source üretmemiştir. BYN0003 üzerinde görülen IRQ273 yalnız USBXHCI kaynağıdır ve bu iki MAX/HSI2C kesme yolunun yerine geçmez; doğrulanmış GPA1_6 kaynağı gelene kadar gerçek bir ISR iddiasında bulunulmaz.

MAX sayacı ilk fotoğraftan önce başladığı için 17 dakikalık fotoğraf aralığıyla hesaplanan toplam hız en fazla yaklaşık `1,79 Hz`'dir. İlk 50 hızlı örnek çıkarıldığında geç dönem üst sınırı yaklaşık `1,74 Hz`, yani örnekler arası alt sınır yaklaşık `575 ms` olur. Bu, hedeflenen 200 ms periyodun WinPE zamanlama/tanı yükü altında gecikebildiğini gösterir; fakat eski sürekli 20 ms/50 Hz davranıştan çok uzaktır. xHCI tarafında örnek sayısının yaklaşık 7.887'den 160'a inmesi de 1 ms gözlem yükünün kaldırıldığını bağımsız olarak doğrular. Konsoldaki “xHCI startup observer 12 saniye için kuruldu” metni `b2lusbctl.exe` içinde güncellenmemiş sabit bir mesajdır; 160 sayacı gerçek sürücünün 5 saniyelik sınırlı gözlemini destekler. Dolayısıyla polling azaltımı işe yaramıştır ve 17 dakikanın baskın açıklaması değildir.

### 5. On yedi dakikalık gecikme tanı orkestrasyonuna daralıyor

İlk ve son fotoğraf arasındaki yaklaşık 17 dakika, polling azaltılmış olmasına rağmen oluşmuştur. Bu süreyi tek bir sürücü callback'ine yükleyecek aşama zaman damgası yoktur; dolayısıyla kesin neden henüz kanıtlanmış değildir. Bununla birlikte `windows/startnet.cmd` kalan başlıca maliyeti açıklar:

- `:B2LUsbCollectMax77705State` bir çağrıda 86 ayrı `reg.exe query` çalıştırır; 55 özet alanı da ayrı `reg.exe` süreçleriyle tekrar okur. Toplam 141 registry süreci üreten bu fonksiyon test yolunda iki kez çağrılır.
- Aynı yol çok sayıda eşzamanlı olmayan `pnputil /scan-devices` ve ayrıntılı `/enum-devices /services /stack /drivers /resources` sorgusu çalıştırır; ayrıca USB, HIDClass ve Mouse sınıflarını ayrı ayrı tarar.
- `HKLM\\...\\Enum\\ACPI` altında özyinelemeli `reg.exe ... /s /f B2LMon` araması, tam `setupapi.dev.log` üzerinde `findstr`, ETW için `logman` başlat/durdur ve senkron `tracerpt` CSV dönüşümü yapılır.
- Üretilen büyük raporların bir bölümü `type` ile dar ve yavaş WinPE konsoluna yazılır. Görüntülerde aynı uzun PnP ve registry listelerinin ekrana basılması bu yolun gerçekten çalıştığını doğrular.
- Sürücü tarafı agresif örnekleme yapmasa da `B2lMaxPublishDiagnostic()` her 10 başarılı geç dönem örneğinde geniş registry tanı kümesini yeniden yazar. Nominal 200 ms periyotta bu yaklaşık iki saniyede bir yazma grubu demektir; tek başına 17 dakikayı kanıtlamaz, fakat registry/tanı yükünü büyütür ve gerçek timer hızının hedefin altına düşmesine katkıda bulunabilir.

Bu nedenle eski “dakikaları sürekli MAX/xHCI polling oluşturuyor” hükmü geri çekilmiştir. Ölçümün desteklediği çalışma hipotezi, gecikmenin seri PnP/SetupAPI/registry/ETW toplama ve konsol çıktı yolunda birikmesidir. Bir sonraki performans doğrulaması, controller davranışını değiştirmek yerine her pahalı `startnet.cmd` aşamasının başına ve sonuna zaman damgası koymalı; ardından tekrar eden ikinci MAX dökümü, sınıf-geneli PnP sorguları, ETW dönüşümü ve tam registry özetleri ayrı ayrı süre ölçülerek daraltılmalıdır.

### 6. Windows attach sırası Linux ile tam aynı değil ve source kararı oluşmuyor

Canlı Linux kaydında aynı Logitech alıcıyla `CC1_ACTIVE` ardından `PLUG_ATTACHED` ve `cc_SOURCE` oluşur; charger/VBUS ve host notifier çok erken çalışır, MUIC `06 09` hemen arkasından gelir. `.27`de Windows tam probe/init ve olay taraması sonrasında yalnız `CC1_ACTIVE` kısmını görmüş, aynı status byte'ında `cc_No_Connection` kalmıştır. Dört CC interrupt örneği yakalanmasına rağmen 1.823 status örneğinde değişiklik olmamış; source dalına hiç girilmemiştir.

Source oluştuğunda Windows güvenlik amacıyla önce MUIC `06 09` + `05` doğrulamasını, sonra charger OTG/VBUS açılmasını şart koşar. Bu sıra Samsung ile birebir değildir. `.27` stok probe-final CONTROL3 işleminin blokaj olmadığını gösterdiği için sıradaki iş, 7. adımda Linux CC event/FSM ve ForceError koordinasyonunu karşılaştırmaktır. CC source doğrulanmadan VBUS'u zorla açmak bu belgedeki güvenlik koşulunu ihlal eder.

### 7. Linux koordinasyon katmanları Windows'ta sadeleştirilmiş

Linux akışı charger, battery, MUIC, Type-C manager, USB notifier, runtime-PM ve role-switch bileşenlerini birlikte yürütür. Windows sürücüsü gerekli register/opcode çekirdeğini uygular ama bu framework koordinasyonunun tamamını modellemez. Sabit HOST hedefi için kabul edilebilir olabilir; attach/detach, hata kurtarma ve güç politikası açısından tam eşdeğer değildir.

## `.27` devir teşhisi ve sonraki değişiklik sınırı

**Kaynak incelemesi güncellemesi:** Aşağıdaki cihaz sırası geçerlidir; bu
bölümde korunmuş ForceError odaklı eski önerinin yerini dosyanın başındaki
`.28` olay tanısı almıştır. SOURCE görülmemesi ForceError eksikliğini kanıtlamaz.

Son `.27` Windows flaşında, Logitech alıcı test boyunca takılıyken fareyi engelleyen sıra şöyledir:

1. BYN0004 dört MMIO kaynağıyla Started olur; CMGP/pinctrl/HSI2C hazırlığı doğrulanır.
2. TOP, USBC ve charger erişimleri ile init opcode zinciri tamamlanır; Linux çalışma maskeleri `00/20/00/f0`, `CCCTRL1=0x1f` ve probe-final CONTROL3 kontrolü geri okumayla doğrulanır. `CCOPEN_OK=1`, `0x64 -> 0x64` sonucu LockErrorEn'in zaten kapalı olduğunu ve 6. adımın tamamlandığını gösterir.
3. Timer tabanlı Linux okuma sırası olay baytlarını yakalar: `TOP_SEEN=0x9`, `CC_SEEN=0x6d`, `PD_SEEN=0xc0`, `CC_IRQS=4`.
4. Buna rağmen 1.823 örnekte `CC_STATE=CC_PREV=0`, `CC_TRANS=0`, `CC_SRC_EV=0` kalır. Fiziksel alıcı varken `CC_STATUS0=0x40`, CC1 pinini aktif gösterir fakat güç rolünü `cc_No_Connection` bırakır; Linux'taki `PLUG_ATTACHED -> cc_SOURCE` geçişi oluşmaz.
5. Windows güvenli olarak VBUS/OTG'yi kapalı ve MUIC yolunu `OPEN=0x3f` tutar.
6. Buna rağmen `_DEP` karşılanmış sayıldığı için BYN0002 DWC3/PHY'yi HOST yapar ve USBXHCI çalışır.
7. xHCI elektriksel attach göremez; sınırlı 160 örnekte portlar `RxDetect`, `CCS=0` kalır ve USBHUB3/HIDUSB/MOUHID aşamalarına gelinmez.
8. Polling azaltımı çalışmıştır, fakat ilk ve son fotoğraf arasında yaklaşık 17 dakika geçmiştir. Bu süre sürücü timerlarından çok seri PnP/SetupAPI/registry/ETW tanı yoluna işaret eder; aşama zaman damgaları olmadan tek bir komut kesin neden ilan edilemez.

Bu nedenle mevcut işlevsel öncelik HID, xHCI ring, HSI2C platformu, kesme maskesi, CONTROL3 veya Linux'taki sanal IRQ114 numarasını doğrudan kullanmak değildir. Bir sonraki sürücü değişikliği 7. adımda, Samsung Linux CC interrupt işleyicisinin `CC1_ACTIVE` sonrasında `PLUG_ATTACHED/cc_SOURCE` üretirken uyguladığı CC event/FSM ve ForceError koordinasyonunu eşlemelidir. Değişiklik şu ayrımı korumalıdır:

1. Önce Linux'taki ilgili CC interrupt/status ve ForceError işlem sırası kaynak düzeyinde birebir çıkarılmalı; Windows tanısına bu ara durumları ayıran read-back alanları eklenmelidir.
2. CC karar mekanizması gerçek `SOURCE` üretmeden MUIC `USB_AP` veya charger OTG/VBUS zorlanmamalıdır.
3. Başarı ölçütü `CC_STATE=2`, `CC_TRANS>0`, `CC_SRC_EV>0`; ardından MUIC `0x09`, OTG/VBUS ve xHCI portunda `CCS=1` zinciridir.
4. Performans işi işlevsel CC değişikliğinden ayrı tutulmalıdır. Önce `startnet.cmd` pahalı aşamalarına zaman damgaları eklenmeli; 141 registry süreci üreten MAX dökümünün iki çağrısı, PnP sınıf taramaları, SetupAPI araması ve ETW/`tracerpt` ayrı ayrı ölçülmelidir.

`cc_SOURCE` güvenilir biçimde doğrulandıktan sonraki ayrı iş, MAX READY ile BYN0002 HOST başlangıcı arasındaki çalışma zamanı kapısını kurmaktır.

## Kaynak konumları

- Linux canlı kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`, özellikle 309-375, 538-715, 747-789, 842-925 ve 978-1016.
- ACPI MAX ve USB bağımlılığı: `silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl`, 254-329.
- [Samsung GPL MAX77705 tanımları](https://github.com/ianmacd/d2s/blob/master/include/linux/ccic/max77705.h#L492-L520): `OPCODE_CTRLREG3_R=0x1c`, `OPCODE_CTRLREG3_W=0x1d`, `GET_CONTROL3_LOCK_ERROR_EN` bit 1.
- [Samsung GPL CONTROL3/CC-open uygulaması](https://github.com/ianmacd/d2s/blob/master/drivers/ccic/max77705_usbc.c#L899-L993): `max77705_set_lockerroren()` ve `max77705_manual_ccopen_request()`.
- [Samsung GPL probe sonu](https://github.com/ianmacd/d2s/blob/master/drivers/ccic/max77705_usbc.c#L3333-L3379): `cc_open_req=1` ve `pdic_manual_ccopen_request(0)`.
- Windows MAX durum makinesi: `windows/b2lusb/b2lmax77705.c`; özellikle `B2lMaxNormalizeCcOpen()` ve `B2lMaxServiceConnection()`.
- Windows MAX timer/PnP: `windows/b2lusb/b2lmax77705.c`; özellikle `B2lMaxEvtPollTimer()`.
- Windows HSI2C platform ve taşıma katmanı: `windows/b2lusb/b2lmax77705_hw.c`, 162-727.
- Windows sınırlı xHCI gözlemcisi ve sabit HOST başlangıcı: `windows/b2lusb/b2lusb.c`.
- Windows ilk-boot PnP, registry, ETW ve konsol tanı orkestrasyonu: `windows/startnet.cmd`, özellikle 200-233 ve 240-452.
- Windows PHY/DWC3 hazırlığı: `windows/b2lusb/b2lusb_hw.c`, 422-665.
