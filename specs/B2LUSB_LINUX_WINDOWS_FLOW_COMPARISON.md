# Beyond2LTE USB host akışı: Samsung Linux ve Windows karşılaştırması

## Kapsam ve kanıt sürümleri

Bu belge, Logitech `046d:c52e` alıcısının Samsung Linux/Lineage altında fiziksel olarak takılmasından HID klavye ve farenin oluşmasına kadar çalışan akışı, mevcut Windows sürücüleriyle yan yana karşılaştırır.

- Linux kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
- Son Windows cihaz kanıtı ve cihazda doğrulanmış kaynak: ekrandaki `b2lmax77705` sürümü `0.1.0.27`
- `.27` cihaz kanıtı: kullanıcının son flaştan sağladığı sekiz ekran görüntüsü. Logitech `046d:c52e` alıcısı, çalışan recovery/Linux analizindekiyle aynı biçimde bütün Windows testi boyunca fiziksel olarak takılıydı.
- Gözlenen duvar saati: ilk fotoğraf ile test sonunu gösteren son fotoğraf arasında yaklaşık 17 dakika. Scriptte aşama zaman damgaları bulunmadığından bu süre tek bir komuta kesin olarak yüklenemez.

## En kısa sonuç

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

## Son teşhis ve bir sonraki değişiklik sınırı

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
