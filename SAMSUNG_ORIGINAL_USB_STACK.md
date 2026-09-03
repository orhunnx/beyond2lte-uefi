# Beyond2LTE orijinal Samsung USB yığını

Bu belge Samsung Galaxy S10+ `beyond2lte`/Exynos 9820 üzerinde USB bağlantısının
tek bir "USB sürücüsü" tarafından değil, birbirine bağlı birkaç donanım ve
sürücü katmanı tarafından nasıl kurulduğunu adım adım açıklar. Özellikle USB-C
porta takılan bir USB fare alıcısının algılanmasından fare olayının işletim
sistemine ulaşmasına kadar olan yolu izler.

Belgenin ana kaynağı Samsung Exynos 9820 Android çekirdek ağacının aşağıdaki
sabit revizyonudur:

- depo: `ianmacd/d2s`
- revizyon: `d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39`
- hedef: `exynos9820-d2s_defconfig` ve `exynos9820-d2` ortak aygıt ağacı

Bu revizyon sabitlendiği için aşağıdaki açıklamalar başka bir MAX77705 kullanan
Snapdragon cihazından tahmin edilmemiştir. Eski Exynos/Snapdragon kaynakları
yalnız karşılaştırma amacıyla kullanılabilir; beyond2lte için belirleyici olan
HSI2C, PMU, PHY ve DWC3 sırası bu Exynos 9820 ağacıdır.

> Önemli adlandırma notu: MAX7705 ve MAX77505 belgeleri MAX77705 ile aynı
> entegreyi tarif etmez. Bu belge, cihaz ağacında `max77705@66` olarak bulunan
> ve Samsung sürücülerinin yönettiği MAX77705 davranışını esas alır.

## 1. Yığının tamamı

Orijinal bağlantı yolu üstten alta şu şekildedir:

```text
USB-C konektörü
  |
  +-- CC1/CC2, VBUS ve aksesuar algılama
  |     |
  |     +-- MAX77705 USBC/CC/PD/MUIC blokları (I2C 0x25)
  |     +-- MAX77705 charger/boost bloğu       (I2C 0x69)
  |     +-- MAX77705 TOP/MFD kimlik bloğu      (I2C 0x66)
  |              |
  |              +-- Exynos HSI2C45 / USI_CMGP03
  |                    MMIO 0x15D60000, SCL GPM12, SDA GPM13
  |
  +-- USB2 D+/D- veri hattı
        |
        +-- Exynos USB2 PHY                    (0x10B00000)
        +-- Synopsys DWC_usb31 DRD/xHCI        (0x10C00000)
        +-- Linux xHCI HCD
        +-- USB core ve hub
        +-- USB HID
        +-- Linux input/mouse
```

Bu grafikte iki bağımsız ama birlikte tamamlanması gereken yol vardır:

1. **Güç/rol yolu:** HSI2C45 -> MAX77705 -> CC rolü -> charger boost -> VBUS.
2. **Veri yolu:** Exynos PHY -> DWC3 HOST -> xHCI -> root hub -> USB HID.

xHCI çalışsa bile MAX77705 porta VBUS vermiyorsa alıcı elektriksel olarak
görülmez. MAX77705 doğru rolü seçip VBUS üretse bile PHY/DWC3/xHCI çalışmıyorsa
USB paketleri taşınmaz. Fare için iki yolun da aynı anda hazır olması gerekir.

## 2. Donanım kaynakları ve sahipleri

| Kaynak | Orijinal tanım | Samsung/Linux sahibi | Görevi |
|---|---|---|---|
| HSI2C45 | `0x15D60000`, `0x1000` | `i2c-exynos5.c` | MAX77705 register erişimi |
| USI_CMGP03 | HSI2C içindeki `USI_CON=0xC4`; platform saat/pin alanları | Exynos I2C/clock/pinctrl | HSI2C fonksiyonunu resetten çıkarma ve saatleme |
| MAX77705 TOP | 7-bit I2C `0x66` | `drivers/mfd/max77705.c` | Entegre kimliği, revizyonu ve MFD ana aygıtı |
| MAX77705 USBC/MUIC | 7-bit I2C `0x25` | `max77705_usbc.c`, `max77705_cc.c`, `max77705_pd.c`, `max77705-muic.c` | CC/PD, rol, aksesuar ve opcode motoru |
| MAX77705 charger | 7-bit I2C `0x69` | `max77705_charger.c` | OTG boost ve konektör VBUS'u |
| MAX77705 fuel gauge | 7-bit I2C `0x36` | fuel-gauge sürücüsü | Pil ölçümü; fare host yolu için doğrudan veri taşımaz |
| MAX77705 kesmesi | `GPA1_6`, aktif düşük, wakeup | `max77705-irq.c` | Tek fiziksel IRQ'yu USBC/CC/PD/MUIC/charger alt IRQ'larına dağıtır |
| DWC3/xHCI | `0x10C00000`, `0x10000` | `dwc3-exynos.c`, DWC3 core, `xhci-plat.c` | DRD çekirdeği ve xHCI host denetleyicisi |
| USB2 PHY | `0x10B00000` | Exynos USB DRD PHY sürücüleri | D+/D- analog/dijital PHY hazırlığı |
| Combo PCS/PMA | `0x10AE0000`, `0x10AF0000` | Exynos USB/DP PHY | SuperSpeed yolu; mevcut Windows USB2-only profili için gerekli değil |
| USB PHY PMU | PMU offset `0x72C`, tam adres `0x1586072C` | Exynos PHY sürücüsü | PHY izolasyon/güç kapısı |

Aygıt ağacında `&hsi2c_45` etkinleştirilir ve altında
`max77705@66 { compatible = "maxim,max77705"; ... }` oluşturulur. Aynı aygıt
ağacı DWC3'ü `dr_mode = "otg"` ve `maximum-speed = "super-speed-plus"` olarak
tanımlar. Çekirdek yapılandırması MFD, CCIC, MUIC, charger, dual-role DWC3,
Exynos DWC3 wrapper ve Exynos USB DRD PHY bileşenlerini yerleşik olarak açar.

## 3. Açılış sırası: bağımlılık zinciri

Linux probe çağrılarının küresel zamanlaması ertelenmiş probe ve initcall
sırasına göre değişebilir. Buna rağmen donanımsal bağımlılık sırası nettir:

1. Clock ve pinctrl altyapısı HSI2C45'in gate/IP saatini ve GPM12/GPM13
   pinlerini hazırlar.
2. `i2c-exynos5` HSI2C45'i I2C master olarak kurar ve Linux I2C adapter'ını
   kaydeder.
3. I2C core, aygıt ağacındaki `max77705@66` düğümünü MFD sürücüsüne bağlar.
4. MAX77705 MFD sürücüsü TOP kimliğini doğrular, aynı fiziksel entegre için
   0x25/0x69/0x36 mantıksal I2C istemcilerini oluşturur ve fiziksel IRQ'yu
   iç IRQ'lara böler.
5. MFD child aygıtları oluşturulur: `max77705-usbc`, `max77705-charger` ve
   yapılandırmaya göre safeout/fuel-gauge/diğer fonksiyonlar.
6. USBC child; sürüm, opcode kuyruğu, MUIC, CC ve PD durum makinelerini kurar.
7. Charger child; korumalı charger registerlarını açar, varsayılan güç
   politikasını kurar ve `otg` adlı power-supply arayüzünü yayımlar.
8. Exynos DWC3 wrapper saatleri/regülatörleri/PHY'leri kurar ve Synopsys DWC3
   child aygıtını oluşturur.
9. Başlangıçta Exynos role-switch durumu B-device'dır: `id=1`,
   `b_sess_vld=0`. Fiziksel Type-C durumu belli olmadan host varsayılmaz.
10. MAX77705 CC/PD olayları veri ve güç rolünü bildirince OTG durumu HOST'a
    geçer, VBUS açılır ve xHCI platform aygıtı başlatılır.

## 4. HSI2C45'in orijinal kurulumu

Kaynak: `drivers/i2c/busses/i2c-exynos5.c`.

### 4.1 Probe aşaması

HSI2C45 düğümü şu platform özelliklerine sahiptir:

- controller MMIO: `0x15D60000..0x15D60FFF`;
- `default-clk = 200000000`;
- `DOUT_CLK_USI_CMGP3` ve `GATE_USI_CMGP3` saatleri;
- `samsung,check-transdone-int`;
- `samsung,scl-clk-stretching`;
- `samsung,usi-i2c-v2`;
- SCL `GPM12`, SDA `GPM13`;
- `INTREQ__USI_CMGP3` kesmesi.

`exynos5_i2c_probe()` şu sırayı uygular:

1. MMIO, IRQ, clock, pinctrl ve aygıt-ağacı özelliklerini alır.
2. Runtime PM ve gerekli clock gate'leri açılır.
3. `exynos_usi_init()` çağrılır. `USI_CON` offset `0xC4` içine `0` yazılır.
   USI reset aktif-yüksek olduğu için bu yazma dahili reseti bırakır.
4. Bootloader veya önceki kullanımdan kalan pending HSI2C kesmeleri
   `INT_STATUS` registerına geri yazılarak temizlenir.
5. `exynos5_i2c_reset()` `HSI2C_CTL.SW_RST` bitini set edip temizler.
6. `exynos5_hsi2c_clock_setup()` fast-speed zamanlamalarını hesaplayıp
   `TIMING_FS1/FS2/FS3` alanlarına yazar. Seçili moda göre HS/fast-plus/
   standard timing de hazırlanır.
7. `exynos5_i2c_init()`:
   - `HSI2C_CTL = HSI2C_MASTER` yazar;
   - trailing count'u `0xFFFFFF` yapar;
   - high-speed ise master ID'yi yazar;
   - `HSI2C_CONF.AUTO_MODE` bitini açar.
8. Linux I2C adapter'ı kaydedilir. Bundan sonra MFD sürücüsünün 0x66
   transferleri bu adapter üzerinden gider.
9. İş yokken runtime PM clock'u kapatabilir; her transfer öncesi tekrar açılır.

Buradaki kritik koşul yalnız `CLK_CTL != 0` değildir. Samsung sürücüsünün
gerçek hazır göstergeleri geçerli FS timing, I2C master kontrolü ve
`CONF.AUTO_MODE` durumudur.

### 4.2 Tek bir register okuması

MAX77705 register okuması iki I2C mesajından oluşur:

1. hedef adrese bir byte register numarası yazılır, fakat ilk mesajda STOP
   üretilmez;
2. aynı hedef adresten bir byte okunur ve son mesajda STOP üretilir.

Bu, `START + address(W) + register + REPEATED START + address(R) + data + STOP`
işlemidir. Araya STOP sokmak bazı register erişimlerinde yanlış davranıştır.

`exynos5_i2c_xfer_msg()` her mesaj için şu ayrıntılı sırayı kullanır:

1. `CTL`, `AUTO_CONF` ve `TIMEOUT` mevcut değerleri okunur.
2. Donanım timeout enable biti temizlenir; yazılım 1 saniyelik bounded timeout
   uygular.
3. Mesaj uzunluğuna göre RX/TX FIFO threshold belirlenir. Kısa mesajda
   threshold doğrudan mesaj uzunluğudur; aksi halde 8'dir.
4. Okuma için `RXCHON`, yazma için `TXCHON` seçilir.
5. Okumada `AUTO_CONF.READ_WRITE` set, yazmada clear edilir.
6. Yalnız son mesajda `AUTO_CONF.STOP_AFTER_TRANS` set edilir.
7. `HSI2C_ADDR` içindeki eski hedef/master alanları temizlenir.
8. Normal fast-speed aktarımında master kimliği `7` bit `[31:24]` alanına,
   7-bit slave adresi bit `[16:10]` alanına yazılır.
9. HSI2C45 DTS'i polling-mode istemediği için Samsung normalde interrupt
   modundadır. FIFO/trailing kesmelerine ek olarak transfer-state hata grubu
   ve `TRANSFER_DONE` açılır; IRQ aktif CPU'ya yönlendirilir.
10. Mesaj uzunluğu `AUTO_CONF[15:0]` alanına yazılır.
11. `AUTO_CONF.MASTER_RUN` set edilerek transfer başlatılır.
12. RX kesmesinde FIFO boş olmayana kadar byte'lar okunur. TX kesmesinde FIFO
    dolu olmayana kadar byte'lar yazılır.
13. Yazma ancak hem `TRANSFER_DONE` hem TX FIFO empty görüldüğünde tamamdır.
14. `NO_ACK`, `NO_DEVICE`, abort/transfer-state hatası veya timeout durumunda
    transfer başarısız sayılır ve controller resetlenir.
15. `samsung,scl-clk-stretching` nedeniyle işlem sonunda master state'in
    başlangıç/idle durumuna dönmesi ayrıca bounded olarak beklenir.
16. `-EAGAIN` sınıfı hata alınırsa bütün mesaj dizisi 100 mikrosaniye arayla
    adapter retry sayısı kadar baştan denenir.

Windows sürücüsü HSI2C IRQ kaynağını sahiplenmiyorsa Samsung kodunun interrupt
beklemesini aynen kopyalayamaz; aynı tamamlanma ve hata koşullarını bounded
polling ile gözlemlemelidir. Polling, donanım protokolünü değiştirmez; yalnız
tamamlanma bildiriminin işletim sistemine taşınma biçimini değiştirir.

### 4.3 Suspend, resume ve hata kurtarma

Samsung sürücüsü suspend sırasında adapter'ı kilitler, gerekiyorsa clock'u
açar, controller'ı reset durumuna alır ve runtime clock'u kapatır. Resume'da:

1. adapter kilitlenir;
2. clock açılır;
3. `exynos_usi_init()` ile dahili USI reset bırakılır;
4. `exynos5_i2c_reset()` ile timing ve master/auto-mode yeniden kurulur;
5. clock tekrar runtime PM yönetimine bırakılır;
6. adapter kilidi bırakılır.

Dolayısıyla reset sonrası yalnız `MASTER` bitini yazmak yeterli değildir;
timing, `FUNC_MODE_I2C` gereken platform durumu, FIFO/interrupt kalıntıları ve
`AUTO_MODE` birlikte tutarlı hale getirilmelidir.

## 5. MAX77705 MFD: tek entegreden çoklu işlev üretme

Kaynak: `drivers/mfd/max77705.c` ve `drivers/mfd/max77705-irq.c`.

`max77705_i2c_probe()` fiziksel TOP istemcisi 0x66 üzerinde çalışır:

1. DT platform verisi okunur: IRQ GPIO, wakeup, firmware product ID, rol-takas
   ve ses desteği.
2. MAX77705 alt kesmeleri için ardışık Linux IRQ numaraları ayrılır.
3. Tüm MAX77705 I2C erişimlerini serileştiren `i2c_lock` oluşturulur.
4. TOP `PMICID1` ve `PMICREV` registerları okunur.
5. Kimlik/revizyon destek tablosunda yoksa probe durur; rastgele 0x66 cevabı
   geçerli MAX77705 kabul edilmez.
6. Gerekirse Samsung paketindeki USBC firmware revizyona göre denetlenir veya
   güncellenir. Bu işlem genel host açma komutu değildir ve Windows portunda
   firmware blobu/revizyon politikası olmadan kopyalanmamalıdır.
7. Aynı I2C adapter üzerinde dummy/mantıksal istemciler oluşturulur:
   - MUIC/USBC `0x25`;
   - charger `0x69`;
   - fuel gauge `0x36`;
   - yapılandırılmışsa debug istemcisi.
8. Fiziksel MAX77705 IRQ geçici olarak kapatılır ve `max77705_irq_init()` ile
   parent interrupt controller kurulur.
9. `mfd_add_devices()` USBC, charger, safeout, fuel-gauge ve açık diğer MFD
   child'larını yayımlar.
10. Aygıt wakeup özelliği etkinleştirilir.

Bu nedenle 0x25 ve 0x69 ayrı yongalar değildir. Aynı MAX77705 içindeki farklı
register alanlarıdır ve aynı I2C bus kilidi/parent güç durumu altında
yönetilmelidir.

## 6. MAX77705 kesme dağıtımı

MAX77705'in GPA1_6 hattı tek fiziksel aktif-düşük kesmedir. Parent IRQ thread'i:

1. TOP interrupt-source registerını okur.
2. Kaynak USBC ise 0x25 adresinden UIC, CC, PD ve gerekiyorsa VDM interrupt
   registerlarını toplu okur.
3. Kaynak charger ise 0x69 charger interrupt registerlarını okur.
4. Bazı revizyonlarda register yerleşimi farklı olduğu için PASS1 ile
   PASS2/3/4/5 okuma yolu ayrılır.
5. Yazılım interrupt maskeleri uygulanır.
6. Set kalan her bit, ayrılmış nested IRQ numarasına çevrilerek ilgili child
   handler'a gönderilir.

Örnekler:

- `APCmdResI` -> opcode cevabı hazır;
- `CCStatI` -> kablo/rol durumu değişti;
- `VBUSDetI`, `CHGTypI`, `UIDADCI` -> MUIC sınıflandırması değişti;
- PD message/PS_RDY/hard-reset bitleri -> PD durum makinesi;
- charger BYP/CHGIN/CHG bitleri -> charger koruma ve güç olayı.

Samsung kodundaki olay temelli model budur. Windows'ta GPIO IRQ kaynağı henüz
ACPI ile verilmediyse geçici polling uygulanabilir; fakat polling aynı status
registerlarını okuyup aynı state transition'ları üretmeli, sonsuz bekleme veya
koşulsuz VBUS açma yapmamalıdır.

## 7. USBC/CC/PD/MUIC başlatma sırası

Kaynaklar:

- `drivers/ccic/max77705_usbc.c`
- `drivers/ccic/max77705_cc.c`
- `drivers/ccic/max77705_pd.c`
- `drivers/muic/max77705-muic.c`

`max77705_usbc_probe()` veri yapılarını, kilitleri, completion nesnelerini ve
workqueue'ları hazırladıktan sonra işlevsel başlangıcı şu sırada yapar:

1. `max77705_get_version_info()`:
   - USBC HW rev `0x00`;
   - USBC FW rev `0x01`;
   - FW minor `0x09` okunur.
2. `max77705_init_irq_handler()`:
   - APCmdRes ve SYSMsg;
   - VDM;
   - diğer UIC olayları için nested IRQ handlerları bağlanır.
3. `max77705_muic_probe()` çağrılır.
4. `max77705_cc_init()` çağrılır; CC interrupt handlerları ve CC durum verisi
   hazırlanır.
5. `max77705_pd_init()` çağrılır; PD interrupt handlerları, PDO/PS_RDY ve
   retry işleri hazırlanır.
6. `REG_PD_INT_M = 0x1C` yazılır.
7. `REG_VDM_INT_M = 0xFF` yazılır.
8. `max77705_init_opcode()` çağrılır.
9. Başlangıç opcode'u olarak firmware Auto-VBUS davranışı kapatılır.
10. Audio desteği açıksa CC control içindeki debug/audio detect bitleri ayrıca
    etkinleştirilir.

Başlangıç state'i önemlidir:

- `auto_vbus_en = false`;
- `op_ctrl1_w = CCSrcSnk | CCSnkSrc | CCDetEn`;
- güç rolü ve veri rolü olay gelmeden geçerli kabul edilmez;
- VBUS yalnız CC/PD kararından sonra charger yoluyla açılır.

### 7.1 Opcode protokolü

MAX77705 USBC firmware komutları normal register write değildir. Samsung kodu
seri hale getirilmiş bir opcode kuyruğu kullanır:

1. Opcode ve payload bir buffer'a konur.
2. Buffer 0x25 adresindeki `0x21` registerından başlayarak toplu yazılır.
3. Payload maksimumdan kısaysa `0x41` registerına `0x00` yazılarak veri sonu
   işaretlenir.
4. Komut kuyruğu bir cevap düğümünü beklemeye alır.
5. Firmware işi bitirince UIC interrupt registerında `APCmdResI` set eder.
6. APCmdRes IRQ handler'ı `0x51` başlangıcından opcode+cevap byte'larını okur.
7. Dönen ilk byte beklenen opcode ile eşleştirilir.
8. Kuyruk bir sonraki komuta geçirilir; timeout/yanlış cevap kurtarma yoluna
   girer.

Sabit host için önemli iki Samsung komutu şunlardır:

| Amaç | Opcode | Payload | Samsung anlamı |
|---|---:|---:|---|
| Firmware Auto-VBUS'u kapat | `SAMSUNG_FACTORY_TEST = 0x54` | `0x00` | VBUS kararını işletim sistemi/charger state makinesine bırakır |
| Auto-IBUS davranışını seç | `FW_AUTOIBUS = 0x57` | `AUTOIBUS_AT_OFF = 0x01` | Samsung PD başlangıcındaki giriş-bus politikası |

`0x57,0x03` başka platform günlüğünde görülmüş olsa bile beyond2lte Samsung
Exynos kaynak yolunun yerine geçirilemez. İşlemci, PMIC bağlantısı ve firmware
politikası farklı bir cihazın sabit değeri bu cihaz için kanıt değildir.

### 7.2 MUIC'in görevi

MUIC xHCI değildir ve USB paketlerini taşımaz. Görevi konektörde ne olduğunu
sınıflandırmak ve policy/notifier katmanlarını beslemektir.

`max77705_muic_probe()`:

1. MUIC state ve mutex/wakelock'larını kurar.
2. 0x25 USBC istemcisini kullanır.
3. DT'deki destek listesini okur; USB, CDP, TA, JIG, OTG ve diğer aksesuar
   sınıflarını açar.
4. Eski pending UIC interruptını okuyarak temizler.
5. UIDADC, charger-type, fake-VBUS, DCD-timeout, VBADC ve VBUS-detect nested
   IRQ'larını bağlar.
6. `max77705_muic_init_detect()` ilk kablo algılamasını IRQ beklemeden yapar.
7. USB external notifier ve CCIC notifier bağlantılarını kurar.

`max77705_muic_detect_dev()` art arda USBC_STATUS1, USBC_STATUS2, BC_STATUS,
FW_MINOR ve CC_STATUS0 alanlarını okuyup şu verileri çözer:

- UID ADC/aksesuar direnç sınıfı;
- VBUS var/yok;
- charger type ve proprietary charger type;
- DCD timeout;
- CC state;
- fake/unsafe VBUS durumu.

Fare alıcısı senaryosunda MUIC'in asıl önemi OTG/USB aksesuar durumunu
doğrulaması ve yanlış/tehlikeli VBUS koşullarını ayırmasıdır.

## 8. Kablo takıldığında CC ve rol kararı

MAX77705 CC status registerı 0x25 adres alanındadır. `CC_STATUS0` alt üç biti
Samsung enum'unda temel bağlantı durumunu verir:

| Alt 3 bit | Durum | Güç davranışı |
|---:|---|---|
| `0` | No connection | VBUS kapatılır, PD/rol state temizlenir |
| `1` | Sink | Cihaz karşı taraftan güç alır; kaynak VBUS kapatılır |
| `2` | Source | Telefon kaynak/host adayıdır; OTG VBUS açılır |
| diğer | Audio/debug/error/disabled/RFU | İlgili özel politika uygulanır; normal host varsayılmaz |

`max77705_ccstat_irq_handler()` adım adım:

1. Güncel CC state'i okur.
2. Ayrılma görülürse opcode kuyruğu ve PD attach durumu temizlenir.
3. `cc_No_Connection`:
   - power role NONE/SINK varsayılanına döner;
   - `max77705_detach_pd()` çalışır;
   - `max77705_vbus_turn_on_ctrl(..., OFF, false)` çağrılır.
4. `cc_SINK`:
   - power role SINK olur;
   - önceki rol SOURCE ise VBUS kapatılır;
   - charger giriş seçimi/akım bildirimi güncellenir.
5. `cc_SOURCE`:
   - power role SOURCE olur;
   - ilk geçiş veya SINK->SOURCE swap durumuna göre
     `max77705_vbus_turn_on_ctrl(..., ON, swapped)` çağrılır.
6. Audio/debug accessory normal USB host olarak numaralandırılmaz; kendi
   accessory yollarına gider.

PD mesajları da bu kararı düzeltebilir. Hard reset, sender timeout veya
source->sink geçişinde VBUS hemen kapatılır; gerektiğinde yaklaşık 760--800 ms
sonra kontrollü yeniden açma işi planlanır. Sink->source PR_SWAP tamamlanınca
VBUS açılır.

## 9. VBUS'un gerçekten açılması: charger state makinesi

Kaynak: `drivers/battery_v2/max77705_charger.c`.

`max77705_vbus_turn_on_ctrl()` doğrudan rastgele PMIC registerı yazmaz. `otg`
adlı power-supply nesnesini bulur ve property üzerinden charger child'a isteği
iletir. Bu, CC policy ile charger donanım sahipliğini ayırır.

### 9.1 Charger başlangıcı

`max77705_charger_probe()`:

1. MFD parent'tan 0x69 charger I2C istemcisini alır.
2. `max77705_charger_initialize()` ile korumalı register alanını ve temel
   charger politikasını kurar.
3. INT_OK üzerinden ilk kablo/güç durumunu okur.
4. `max77705-charger` ve `otg` power-supply arayüzlerini kaydeder.
5. Charger/WCIN/CHGIN/AICL/BYP gibi nested IRQ'ları bağlar.

Tam Android charger başlangıcı charge timer, float voltage, watchdog, AICL,
wireless charging, UNO limiti ve batarya politikasını da programlar. Sabit
Windows USB host portu bunların hepsini körlemesine kopyalamamalıdır. USB host
için zorunlu olan bölüm, charger korumasını doğru açmak ve mevcut CNFG_00
durumundan güvenli OTG durumuna geçmektir.

### 9.2 Korumalı registerların kilidini açma

Samsung charger probe sırasında `max77705_charger_initialize()` ile
`CHG_CNFG_06.CHGPROT` alanını açar. Genel charger property yazma yolu da işlem
başında `max77705_charger_unlock()` çağırır:

1. `CHG_CNFG_06` (`0xBD`) okunur.
2. `CHGPROT = bits[3:2]` çıkarılır.
3. Değer `3` değilse masked update ile bits[3:2] = `3` yapılır.
4. 20 ms beklenir.
5. Readback yapılır; en fazla 10 deneme uygulanır.
6. Kilit açılırken register init kaybı gözlenmişse charger init yeniden
   uygulanabilir.

Önemli kaynak ayrıntısı: ayrı `otg` power-supply setter'ı doğrudan
`max77705_set_otg()` çağırır; bu fonksiyon kendi içinde yeniden unlock yapmaz.
Samsung burada charger probe'unun daha önce korumayı açmış olmasına güvenir.
Windows'ta probe sırası ve önceki firmware durumu farklı olabileceği için
OTG yazmasından hemen önce CHGPROT readback/unlock yapmak güvenli eşdeğerdir;
bu, Samsung state makinesini değiştirmek değil önkoşulunu açıkça doğrulamaktır.

Kilit açılmadan `CNFG_00` yazmak I2C seviyesinde başarılı görünüp işlevsel
olarak etkisiz kalabilir. Bu nedenle yazma başarısı ile mod readback'i ayrı
doğrulanmalıdır.

### 9.3 OTG ON geçişi

`max77705_set_otg(enable=1)`:

1. Zaten açık mı ve low-power charge modu engelliyor mu kontrol edilir.
2. Wireless charger çatışması kontrol edilir.
3. OTG wakelock ve charger mutex alınır.
4. Mevcut `CHG_INT_MASK` (`0xB1`) saklanır.
5. CHG ve CHGIN interruptları maskelenir, BYP interruptı açılır. Masked update
   maskesi `0x51`, yazılan değer `0x50`'dir.
6. `max77705_chg_set_mode_state(OTG_ON)` çağrılır.
7. Yeni INT_MASK ve `CHG_CNFG_00` (`0xB7`) okunarak kayıt altına alınır.
8. Mutex/wakelock bırakılır ve power-supply değişikliği bildirilir.

`CNFG_00[3:0]` geçişleri mevcut moda bağlıdır:

| Önceki mod | Anlam | OTG ON hedefi |
|---:|---|---:|
| `0x0` | all off | `0xA` boost + OTG |
| `0x4` | buck | `0xE` buck + boost + OTG |
| `0x5` | buck + charger | `0xF` buck + charger + boost + OTG |
| `0x8` | boost + UNO | önce `0x4`, 1--2 ms bekle, sonra `0xA` |
| `0xC` | buck + boost + UNO | önce `0x4`, 1--2 ms bekle, sonra `0xE` |
| `0xD` | buck + charger + boost + UNO | önce `0x4`, 1--2 ms bekle, sonra `0xE` |
| `0xA/0xE/0xF` | OTG zaten açık | uygun OTG modu korunur |

Bu ara `0x4` geçişi atlanmamalıdır. UNO->OTG doğrudan bit değiştirme değildir;
Samsung kaynakta açıkça buck ara durumu ve gecikme kullanır.

### 9.4 OTG OFF geçişi

`max77705_set_otg(enable=0)`:

1. State machine OTG bitini mevcut güç durumunu koruyarak kapatır:
   - `0xA -> 0x0`;
   - `0xE -> 0x4`;
   - `0xF -> 0x5`.
2. 50 ms beklenir.
3. OTG açılmadan önce saklanan `CHG_INT_MASK` geri yazılır.
4. INT_MASK ve CNFG_00 readback edilir.
5. Wireless/power-supply bildirimleri güncellenir.

Kablo ayrılmasında ve D0/suspend kapanışında bu simetrik kapatma yapılmalıdır;
aksi halde telefon konektörde gereksiz/tehlikeli VBUS bırakabilir.

## 10. Exynos PHY, DWC3 ve xHCI host başlangıcı

Kaynaklar:

- `drivers/usb/dwc3/dwc3-exynos.c`
- `drivers/usb/dwc3/core.c`
- `drivers/usb/dwc3/otg.c`
- `drivers/phy/samsung/phy-exynos-usbdrd.c`
- `drivers/phy/samsung/phy-exynos-usb3p1.c`
- `drivers/usb/host/xhci-plat.c`
- `drivers/usb/host/xhci.c`

### 10.1 Exynos wrapper probe

`dwc3_exynos_probe()`:

1. 36-bit DMA maskesi ister.
2. USB wrapper clocklarını alır, hazırlar ve açar.
3. Runtime PM'yi etkinleştirir.
4. VDD33 ve VDD10 regülatörlerini alıp açar.
5. Generic USB2/USB3 PHY platform aygıtlarını kaydeder.
6. DT child'larını populate ederek Synopsys DWC3 core probe'unu başlatır.

Exynos USB PHY sürücüsü PMU izolasyonunu kaldırır, USB2 PHY registerlarını
programlar ve gerektiğinde combo PHY sahipliğini koordine eder. D2S stock
profilinde USB3/DP combo PHY mevcuttur; bizim Windows USB2-only hedefimizde
alıcı için temel zorunlu yol ana USB2 PHY'dir.

### 10.2 Role switch ve OTG state machine

Exynos role-switch başlangıçta:

```text
fsm.id = 1          # B-device / host değil
fsm.b_sess_vld = 0
```

CC/USB notifier yolu `dwc3_exynos_id_event()` ve
`dwc3_exynos_vbus_event()` üzerinden yeni durumu FSM'e aktarır. ID host
durumuna (`id=0`) gelince `A_IDLE -> A_HOST` geçişi yapılır:

1. `dwc3_otg_start_host(..., 1)` çağrılır.
2. `dwc3_otg_phy_enable()`:
   - USB2 PHY connection/güç isteğini açar;
   - runtime PM ve QoS isteğini alır;
   - `dwc3_core_init()` ile core'u yeniden başlatır.
3. Platformda gerçek VBUS pad algısı yoksa yazılım DP/DM pulldown ayarlarını
   uygular.
4. `dwc3_otg_set_host_mode()` DWC3 `GCTL.PRTCAP` alanını HOST yapar.
5. DWC3 tarafından önceden ayrılmış xHCI platform aygıtı
   `platform_device_add(dwc->xhci)` ile yayınlanır.
6. xHCI platform sürücüsü MMIO ve IRQ'yu alır, primary ve shared HCD'leri
   `usb_add_hcd()` ile USB core'a ekler.
7. `xhci_run()` command ring, DCBAA, interrupter ve root-hub çalışma durumunu
   başlatır.
8. `dwc3_otg_start_host()` içinden MAX77705'e host-turn-on olayı bildirilir.
9. Bu fonksiyon başarıyla döndükten sonra OTG FSM VBUS regulator yolunu açar.
10. FSM `A_HOST` durumuna geçer.

Bu kaynakta host core ve VBUS çağrıları farklı alt sistemlerden gelebilir;
sonuçta istenen invariant şudur: CC=SOURCE, charger OTG açık, PHY aktif,
DWC3 HOST ve xHCI RUN aynı fiziksel bağlantı sırasında birlikte doğru olmalı.

### 10.3 Host kapanışı

ID host durumundan çıkınca sıra tersine döner:

1. VBUS regulator kapatılır.
2. MAX77705 host-turn-on olayı kapatılır.
3. xHCI platform aygıtı kaldırılır; root hub ve child USB aygıtları sökülür.
4. DWC3 core/PHY runtime PM ile kapatılır.
5. FSM tekrar `A_IDLE`/`B_IDLE` durumuna döner.

## 11. USB fare alıcısının numaralandırılması

CC, VBUS, PHY ve xHCI hazır olduktan sonra standart Linux USB/HID zinciri
devralır. MAX77705 bundan sonra HID raporu taşımaz.

1. Alıcı VBUS ile enerjilenir ve D+/D- pull-up'ı görünür olur.
2. xHCI `PORTSC.CCS` değişimini ve port-status-change event'ini üretir.
3. xHCI interrupt handler olayı root hub'a bildirir.
4. `drivers/usb/core/hub.c:hub_event()` port resetini, hız algısını ve adresleme
   akışını başlatır.
5. `usb_new_device()` device descriptor, configuration descriptor ve string
   descriptorları okur.
6. `usb_set_configuration()` uygun configuration ve interface'leri etkinler.
7. HID interface `drivers/hid/usbhid/hid-core.c:usbhid_probe()` ile eşleşir.
8. HID descriptor ve report descriptor okunur.
9. `hid_add_device()` HID core'a aygıtı ekler; HID input katmanı relative X/Y,
   wheel ve button usages alanlarını Linux input eventlerine çevirir.
10. `input_register_device()` ile mouse input aygıtı yayınlanır.
11. Interrupt-IN endpoint periyodik raporları xHCI transfer ring üzerinden
    taşır; her rapor imleç hareketi/tıklama olayına dönüşür.

Alıcı birleşik klavye+fare aygıtıysa tek USB device altında birden fazla HID
interface oluşabilir. Hub'da aygıt görünmeden HID sürücüsünü değiştirmek hiçbir
sonuç vermez; hata önce PORTSC/numaralandırma sınırında çözülmelidir.

## 12. Windows'taki eşdeğer sorumluluklar

Windows'un Linux sürücülerini satır satır kopyalaması gerekmez. Aynı donanım
invariantlarını Windows PnP/power/driver modeline göre sağlaması gerekir.

| Samsung/Linux sorumluluğu | Bu buildde Windows sahibi | Eşdeğer sonuç |
|---|---|---|
| `i2c-exynos5` HSI2C45/USI | `b2lmax77705.sys`, `ACPI\BYN0004` | USI reset bırakma, timing/master/auto-mode, bounded transfer ve recovery |
| MAX77705 MFD 0x66/0x25/0x69 | `b2lmax77705.sys` içindeki mantıksal katmanlar | Tek bus kilidi altında identity, USBC opcode/CC ve charger OTG |
| MAX77705 nested IRQ | Şimdilik pasif KMDF timer/polling | Aynı status/state transition; gelecekte GPIO IRQ kaynağıyla olay temelli model |
| CC/PD/MUIC policy | `b2lmax77705.sys` sabit-HOST alt kümesi | Auto-VBUS off, Auto-IBUS at-off, CC=SOURCE şartı, detach'te OFF |
| MAX77705 charger OTG | `b2lmax77705.sys` | CHGPROT unlock, INT_MASK, CNFG_00 state machine ve readback |
| Exynos PHY/DWC3 wrapper | `b2lusb.sys`, `ACPI\BYN0002` | PMU/PHY hazırlığı, DWC3 reset ve HOST handoff |
| Linux xHCI HCD | Microsoft `USBXHCI.SYS`, `ACPI\BYN0003` | xHCI ring/interrupt/root-hub yönetimi |
| Linux USB core/hub | Microsoft `USBHUB3.SYS` | USB alıcı numaralandırması |
| Linux usbhid/HID input | `HIDUSB.SYS` + `HIDCLASS.SYS` + `MOUHID.SYS` + `MOUCLASS.SYS` | HID raporundan Windows mouse input'una |
| Kernel tanısı | `b2lusbmon.sys`, `b2lusbctl.exe`, `startnet.cmd` | Salt-okuma kaynak/state ölçümü; donanım sahipliği yok |

ACPI bağımlılığı şu nedenle önemlidir:

- `BYN0004` MAX77705/HSI2C sahibidir.
- `BYN0002` USB PHY/DWC3 platform hazırlığı sahibidir ve MAX77705'e `_DEP`
  ile bağlıdır.
- `BYN0003` gerçek xHCI MMIO+IRQ child'ıdır ve Microsoft USBXHCI'ye gider.

Windows PnP'nin global probe sırası Linux initcall sırasıyla aynı olmak zorunda
değildir; `_DEP`, ayrı ACPI aygıtları ve D0 callback'leri gerekli donanım
hazırlığının child xHCI başlamadan önce tamamlanmasını sağlamalıdır.

### 12.1 Windows'ta bire bir korunması gereken davranışlar

1. HSI2C bus hazır olmadan MAX77705 kimlik okuması yapılmaması.
2. Register read için write-pointer + repeated START + read işlemi.
3. Her transferde adres/master alanlarının doğru yeniden kurulması.
4. Timeout, NACK, abort ve FIFO durumlarının ayrıştırılması.
5. Hata sonrası timing/auto-mode dahil tam HSI2C recovery.
6. TOP identity doğrulanmadan USBC/charger yazması yapılmaması.
7. Opcode 0x21/0x41/APCmdRes/0x51 protokolünün ve cevap opcode'unun
   doğrulanması.
8. Auto-VBUS kapandıktan sonra VBUS'un yalnız CC=SOURCE durumunda açılması.
9. Charger CHGPROT unlock ve readback.
10. CNFG_00 UNO->OTG ara `0x4` geçişinin korunması.
11. OTG kapanışında önceki interrupt maskesinin geri yüklenmesi.
12. D0Exit/ReleaseHardware/kablo ayrılmasında VBUS'un güvenli kapatılması.
13. DWC3 HOST/xHCI başladıktan sonra USBHUB3/HID işini inbox sürücülere
    bırakmak; özel fare protokolü yazmamak.

### 12.2 Körlemesine kopyalanmaması gereken Android bölümleri

1. Batarya charge voltage/current, wireless charging, AFC/QC ve watchdog
   politikalarının tamamı.
2. USBC firmware update blobu ve revizyon tablosu; ürün firmware paketi
   doğrulanmadan uygulanamaz.
3. DisplayPort alternate mode, VDM, SBU audio, VCONN ve role-swap özellikleri;
   sabit USB2 HOST hedefinin parçası değildir.
4. Linux wakelock, workqueue, notifier ve power-supply API çağrılarının şekli;
   Windows'ta karşılığı KMDF power state, timer/work item, ACPI dependency ve
   class-driver sözleşmeleridir.
5. Başka SoC'deki HSI2C/GPIO/clock/MMIO adresleri veya sabit opcode payloadları.

### 12.3 Windows 0.1.0.19 kodunun Samsung kaynağına göre fark listesi

Bu bölüm `windows/b2lusb` sürüm 0.1.0.19 kaynaklarını tarif eder.
"Eksik" ifadesi bir Linux API'sinin Windows'a taşınmadığını değil, karşılık
gelen donanım invariantının henüz kanıtlanmadığını belirtir.

| Samsung adımı | Mevcut Windows durumu | Sonuç/eylem |
|---|---|---|
| HSI2C45 ve CMGP03 kaynak sahipliği | `BYN0004` iki MMIO sayfasını tek `b2lmax77705.sys` sahibine veriyor | Doğru sahiplik modeli |
| CMGP03'ü I2C moduna seçme | `0x15C22000 + 0x30` alanına I2C modu yazılıyor | UEFI handoff'a bağımlılığı azaltıyor |
| HSI2C iç USI resetini bırakma | HSI2C `+0xC4` alanına `0` yazılıyor | Samsung `exynos_usi_init()` ile eşleşiyor |
| Clock framework ile gate/IP clock yönetimi | Windows sürücüsü clock framework kullanmıyor; UEFI'nin açık bıraktığı saat/timing durumunu devralıyor | Timing readback var, fakat clock ownership orijinal Linux kadar güçlü değil |
| Interrupt tabanlı HSI2C45 transferi | Windows ACPI kaynağında HSI2C IRQ yok; bounded polling kullanılıyor | Kabul edilebilir geçici uyarlama; kaynak yorumunda HSI2C45'in "stock polling" olduğu yazılmamalı, stock DT yolu interrupt modudur |
| Repeated START register read | Write mesajında STOP yok, ardından read+STOP uygulanıyor | Samsung transfer semantiğiyle eşleşiyor |
| FIFO threshold, master ID 7, message length ve MASTER_RUN | Uygulanmış | Readback ve gerçek PMIC cevabıyla kanıtlanmalı |
| NACK/timeout sonrası recovery | İki tam deneme, 100 us aralık ve yalnız hata yolunda timing korumalı reset var | Samsung adapter retry sınırı ve `reset-before-trans` bulunmayan HSI2C45 yolu ile eşleşiyor |
| TOP identity/revision tablosu | 0x66 PMICID1/PMICREV ve PASS3/4/5 eşleşmesi var | Doğru MFD giriş kapısı |
| Mantıksal 0x25/0x69 istemcileri | Ayrı WDF child yerine aynı driver içinde mantıksal adres katmanları var | Windows için geçerli eşdeğer; ortak bus lock korunmalı |
| MAX77705 firmware update | Uygulanmıyor | Bilinçli güvenlik sınırı; doğrulanmış Samsung firmware paketi olmadan eklenmemeli |
| Fiziksel GPA1_6 parent IRQ ve nested IRQ'lar | ACPI IRQ sözleşmesi yok; pasif timer CC durumunu okuyor | Detach/PD hata tepkisi gecikmeli; gelecekte GPIO interrupt controller tanımı gerekir |
| USBC HW/FW/minor okuma | Uygulanmış | HSI2C çalıştığında ilk kimlik kanıtı |
| UIC/CC/PD/VDM interrupt maskelerinin Samsung başlangıç değerleri | Stock probe'un son `PD_INT_M=0x1C` ve `VDM_INT_M=0xFF` değerleri readback ile uygulanıyor; dört status registerı polling döngüsünde read-to-clear ediliyor | UIC/CC nested-IRQ maskelerini GPIO IRQ'suz Windows profili sahiplenmiyor; status birikmesi önleniyor |
| MUIC ilk status taraması ve aksesuar sınıflandırması | CC_STATUS0/1, BC_STATUS ve charger detail okunuyor; tam UIDADC/VBADC/DCD/charger-type state makinesi yok | Basit fare-host için alt küme olabilir, fakat hatalı kablo/unsafe VBUS ayrımı eksik |
| `CCCTRL1` ile CC detect/source-sink/audio policy | `CCCTRL1_W(0x0C,0x1F)` opcode'u ve cevap doğrulaması uygulanıyor | beyond2lte `support-audio` ile stock `op_ctrl1_w` bitlerinin tamamı açık kuruluyor |
| `0x54,0x00` Auto-VBUS off | 0x21/0x41 yazma, APCmdRes polling ve 0x51 cevap-opcode doğrulaması var | Samsung protokolüyle eşleşiyor |
| `0x57,0x01` Auto-IBUS at-off | Aynı opcode protokolüyle uygulanmış | Exynos kaynağına uygun değer |
| Charger başlangıç/unlock | `CHG_CNFG_06 (0xBD)` bits[3:2] en fazla on yazma ve 20 ms beklemeyle açılıyor; son readback zorunlu | Kilit kanıtlanmadan korumalı charger registerlarına geçilmiyor |
| Charger INT_MASK OTG politikası | `0x51/0x50` masked update ve baseline restore var | Samsung `set_otg` davranışıyla eşleşiyor |
| CNFG_00 OTG state machine | 0/4/5/8/C/D -> A/E/F yolları, CHGPROT önkoşulu ve 1.5 ms ara geçiş uygulanmış | D modunun Samsung hedefi E olmasına dikkat edilmiş |
| OTG OFF | A/E/F -> 0/4/5, 50 ms ve mask restore uygulanmış | Samsung kapanışına yakın |
| CC SOURCE olmadan VBUS açmama | `B2lMaxEvaluateConnection()` yalnız state 2'de açıyor; diğer bütün CC durumlarında kendi açtığı veya bootloader'dan devraldığı A/E/F OTG modunu kapatıyor | Doğru temel güvenlik kapısı |
| PD hard-reset/PR_SWAP yeniden zamanlaması | Tam PD message state machine yok | Sabit fare host profilinde rol takası dışarıda; fakat PD hatasında VBUS recovery sınırlı |
| Exynos PHY/DWC3 HOST | `b2lusb.sys` ile uygulanmış ve cihazda xHCI RUN kanıtlanmış | Bu katman mevcut ana engel değil |
| Root hub/HID/mouse | Microsoft inbox yığınına bırakılmış | Doğru; `PORTSC.CCS` oluşmadan HID tarafında yapılacak iş yok |

Bu karşılaştırmadan çıkan en kısa teknik sonuç şudur:

1. Windows xHCI/PHY yolu cihazda çalışıyor.
2. HSI2C transport hâlâ gerçek PMIC kimlik cevabıyla kanıtlanmalı.
3. `CHG_CNFG_06.CHGPROT`, stock PD/VDM maskeleri ve beyond2lte `CCCTRL1`
   başlangıç adımları 0.1.0.19'da uygulanmıştır; cihaz readback'i bekleniyor.
4. Sonraki açık donanım sınırı, gerçek CC=SOURCE sonucundan sonra charger
   OTG readback'i ile xHCI `PORTSC.CCS` oluşumunun birlikte görülmesidir.
5. Gerçek VBUS ve `PORTSC.CCS` birlikte ölçülmeli;
   yalnız registry flag'i başarı sayılmamalıdır.

## 13. Mevcut cihaz çıktısının bu zincirdeki yeri

Son cihaz ölçümünde xHCI tarafı şunları gösterdi:

- istenen rol HOST ve mevcut rol HOST;
- DWC3 reset uygulanmış;
- xHCI `USBCMD=0x2005`, `USBSTS=0`;
- command ring/DCBAA/config alanları programlanmış;
- iki portta `PP=1`;
- fakat iki portta da `CCS=0`, `PED=0`, `PLS=RxDetect`;
- port transition sayısı sıfır.

Bu, `b2lusb.sys -> USBXHCI.SYS` yolunun artık çalıştığını; fakat alıcının
konektörde elektriksel olarak görünmediğini gösterir. Arıza sınırı HID veya
USBHUB3 değildir. Öncelikli sınır:

```text
HSI2C45 erişimi
  -> MAX77705 identity/opcode
  -> CC state SOURCE
  -> charger CHGPROT + CNFG_00 OTG
  -> konektörde gerçek 5 V VBUS
  -> xHCI PORTSC.CCS geçişi
```

Özellikle önceki tanıda HSI2C address readback'in `0xFF0FFFFF` kalması ve
MAX77705 identity/status alanlarının sıfır/başarısız olması, MAX77705 komut
zincirinin doğrulanmadığını gösterir. xHCI port power biti tek başına Type-C
konektörde charger boost/VBUS üretildiğini kanıtlamaz.

## 14. Başarı ölçütü: adım adım doğrulama

Bir sonraki tek build/flash turunda aşağıdaki zincir aynı boot içinde
doğrulanmalıdır:

1. HSI2C `USI_CON=0`; FS timing değerleri sıfır değil; `CONF.AUTO_MODE=1`.
2. 0x66 TOP `PMICID1/PMICREV` okuması geçerli ve desteklenen revizyon.
3. 0x25 USBC HW/FW/minor registerları geçerli.
4. `0x54,0x00` opcode cevabının ilk byte'ı `0x54`.
5. `0x57,0x01` opcode işlemi tamamlanmış.
6. `CC_STATUS0 & 0x7 == SOURCE(2)`; adaptör ters takılırsa CC pin sonucu yine
   doğru okunmalı.
7. `CHG_CNFG_06.CHGPROT == 3`.
8. `CHG_INT_MASK` OTG politikasına geçmiş.
9. `CHG_CNFG_00 & 0xF`, mevcut moda göre `0xA`, `0xE` veya `0xF`.
10. Konektörde gerçek VBUS ölçümü mevcut.
11. xHCI HOST/RUN, HCHalted clear ve port power açık.
12. Alıcı takılınca `PORTSC.CCS=1`; ardından port reset/enable.
13. `USBHUB3` altında receiver VID/PID görünür.
14. HID interface `HIDUSB/HIDCLASS` altında Started.
15. Fare PDO'su `MOUHID/MOUCLASS` altında Started.
16. Hareket, buton ve wheel raporları masaüstüne ulaşır.

İlk başarısız madde gerçek hata sınırıdır. Örneğin madde 10 başarılı ama 12
başarısızsa MAX77705 registerlarını genişletmek yerine Type-C data mux/USB2
PHY fiziksel bağlantısı incelenmelidir. Madde 12 başarılı ama 13 başarısızsa
hub enumeration/descriptor/transfer hatasına geçilmelidir.

## 15. Kaynak dosya indeksi

Aşağıdaki yollar `ianmacd/d2s` revizyonu
`d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39` içindedir:

| Konu | Kaynak dosya/fonksiyon |
|---|---|
| Exynos HSI2C45 tanımı | `arch/arm64/boot/dts/exynos/exynos9820.dts` |
| beyond2lte MAX77705 düğümü | `arch/arm64/boot/dts/samsung/exynos9820-d2_if-pmic-max77705.dtsi` |
| DWC3 `dr_mode` | `arch/arm64/boot/dts/samsung/exynos9820-d2_common.dtsi` |
| HSI2C probe/xfer/recovery | `drivers/i2c/busses/i2c-exynos5.c`: `exynos5_i2c_probe`, `exynos5_i2c_xfer`, `exynos5_i2c_xfer_msg` |
| MAX77705 MFD | `drivers/mfd/max77705.c`: `max77705_i2c_probe` |
| MAX77705 interrupt controller | `drivers/mfd/max77705-irq.c`: parent IRQ thread, `max77705_irq_init` |
| USBC/opcode | `drivers/ccic/max77705_usbc.c`: `max77705_usbc_probe`, `max77705_i2c_opcode_write/read` |
| CC state machine | `drivers/ccic/max77705_cc.c`: `max77705_ccstat_irq_handler`, `max77705_cc_init` |
| PD/VBUS policy | `drivers/ccic/max77705_pd.c`: `max77705_pd_init`, `max77705_vbus_turn_on_ctrl` |
| MUIC sınıflandırma | `drivers/muic/max77705-muic.c`: `max77705_muic_probe`, `max77705_muic_detect_dev` |
| Charger/OTG | `drivers/battery_v2/max77705_charger.c`: `max77705_charger_unlock`, `max77705_set_otg`, `max77705_chg_set_mode_state` |
| Exynos DWC3 wrapper | `drivers/usb/dwc3/dwc3-exynos.c`: `dwc3_exynos_probe`, role-switch eventleri |
| DWC3 OTG host geçişi | `drivers/usb/dwc3/otg.c`: `dwc3_otg_start_host`, `dwc3_otg_phy_enable` |
| Exynos USB PHY | `drivers/phy/samsung/phy-exynos-usbdrd.c`, `phy-exynos-usb3p1.c` |
| xHCI platform/HCD | `drivers/usb/host/xhci-plat.c`, `drivers/usb/host/xhci.c` |
| USB hub enumeration | `drivers/usb/core/hub.c`: `hub_event`, `usb_new_device` |
| USB HID | `drivers/hid/usbhid/hid-core.c`: `usbhid_probe` |
| HID/input core | `drivers/hid/hid-core.c`, `drivers/input/input.c` |

Sabit GitHub kaynak kökü:

`https://github.com/ianmacd/d2s/tree/d1d4fb4aa21d6f4c9b9f36caf15bc1a3c21d4e39`

## 16. Kısa sonuç

Orijinal Samsung tasarımında USB host açmak yalnız DWC3 `PRTCAP=HOST` yazmak
değildir. Doğru sıra şudur:

```text
HSI2C45'i tam kur
-> MAX77705 TOP kimliğini doğrula
-> MFD USBC/CC/PD/MUIC/charger işlevlerini başlat
-> CC bağlantısını SOURCE olarak doğrula
-> firmware Auto-VBUS'u kapat ve opcode cevabını doğrula
-> charger korumasını aç
-> güvenli CNFG_00 geçişiyle OTG boost/VBUS üret
-> Exynos PHY ve DWC3'ü HOST yap
-> xHCI/root hub'ı başlat
-> USB alıcıyı numaralandır
-> standart HID/mouse yığınına bırak
```

Mevcut buildde çalışan xHCI'nin `RxDetect` durumunda kalması, zincirin son
HID kısmından önce kesildiğini gösterir. Bir sonraki geliştirme kararı MAX77705
HSI2C kimlik/opcode/charger readback'i ve konektörde gerçek VBUS kanıtına göre
verilmelidir.
