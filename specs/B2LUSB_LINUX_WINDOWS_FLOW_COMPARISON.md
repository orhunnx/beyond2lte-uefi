# Beyond2LTE USB host akışı: Samsung Linux ve Windows karşılaştırması

## Kapsam ve kanıt sürümleri

Bu belge, Logitech `046d:c52e` alıcısının Samsung Linux/Lineage altında fiziksel olarak takılmasından HID klavye ve farenin oluşmasına kadar çalışan akışı, mevcut Windows sürücüleriyle yan yana karşılaştırır.

- Linux kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
- Son Windows cihaz kanıtı: ekrandaki `b2lmax77705` sürümü `0.1.0.24`
- Mevcut, henüz cihazda doğrulanmamış kaynak: `windows/b2lusb/b2lmax77705.inf.in` sürümü `0.1.0.25`
- `.24` cihaz kanıtı: kullanıcının son flaştan sağladığı ekran görüntüleri; paket `423fd45` ile eklenen CMGP/pinctrl/HSI2C platform hazırlığını içerir.

## En kısa sonuç

`.24` ile HSI2C platform hazırlığı ve MAX77705 iletişimi cihaz üzerinde doğrulanmıştır. Sürücü `STAGE=0x70`, `STATUS=0`, `PLATFORM_OK=1` durumuna ulaşır; PMIC/USBC kimlikleri okunur, firmware opcode yolu çalışır, charger kilidi açılır ve platform registerları geri okunarak doğrulanır. Ancak bu başarı 6. probe/init adımını Linux ile tam eşdeğer kılmıyordu: yalnız `PD_INT_M=0x1c` ve `VDM_INT_M=0xff` kuruluyor, `UIC_INT_M/CC_INT_M` eksik kalıyor, Linux'un çalışma maskesi `00/20/00/f0` durumuna geçilmiyor ve `CCCTRL1=0x1f` gerçek geri okumayla doğrulanmıyordu. Bu nedenle ilk eşleşmeyen basamak 6'dır.

`.25` kaynağı 6. adımı kod düzeyinde eşler: opcode öncesi `UIC/CC/PD/VDM=00/20/1c/ff`, opcode sonrası `00/20/00/f0` maskelerini yazıp geri okur; `CCCTRL1_W 0c 1f` ardından `CCCTRL1_R 0b` sonucunun `0x1f` olmasını şart koşar. Bu sürüm henüz cihazda doğrulanmadığı için 7. Type-C attach/rol aşamasına geçildiği varsayılamaz. `.25` maskeleri ve CCCTRL1 geri okumasını doğrular fakat `CCStat` yine `cc_No_Connection` kalırsa sonraki ilk uyuşmazlık 7 olacaktır.

Ana mimari fark şudur:

```text
Samsung Linux:
MAX IRQ -> CC=SOURCE -> VBUS/OTG + rol bildirimi -> MUIC USB_AP
        -> DWC3/PHY runtime resume -> xHCI -> USB enumerate -> HID

Mevcut Windows:
ACPI _DEP yalnızca BYN0004 sürücüsünün başlamasını bekler
        +-> BYN0002 hemen PHY/DWC3 HOST yapar -> USBXHCI çalışır
        `-> BYN0004 zamanlayıcısı platform + HSI2C + MAX init'i tamamlar
            -> CC1 pini görülür, fakat CCStat=No Connection kalır
            -> güvenli yol: VBUS kapalı + MUIC OPEN -> xHCI attach göremez
```

## Adım adım yan yana karşılaştırma

| Aşama | Çalışan Samsung Linux akışı | Mevcut Windows kaynağının hedefi | Son Windows flaşında gözlenen | Sonuç |
|---|---|---|---|---|
| 1. Donanım sahipliği | `15d60000.hsi2c -> i2c-14 -> 14-0066 -> max77705-usbc`; TOP/USBC/charger alt adresleri tek MFD altında | `BYN0004`, HSI2C `0x15D60000`, USI `0x15C22000`, CMGP CMU ve ilgili pinctrl sayfalarını tek başına eşler; `0x66/0x25/0x69` adreslerini kendisi yönetir | `.24`te `BYN0004` Started; `0x15C00000`, `0x15D60000`, `0x15C22000` ve `0x15C30000` olmak üzere dört MMIO alanı atanmış | Eşleşen sahiplik modeli ve kaynak kapsamı cihazda doğrulandı |
| 2. HSI2C güç/saat hazırlığı | Linux pinctrl ile GPM12-0/GPM13-0'ı işlev 2 yapar; DT 200 MHz ister, clock framework mevcut hızı okur; QCH/gate hazırlanır ve 400 kHz timing hesaplanır | `.24`, aynı pinctrl ve CMGP3 alanlarını BYN0004'e verir; 200 MHz'i deterministik kurar, QCH'yi ve buna karşılık gelen FS timing alanlarını yazar, geri okur | `PLATFORM_OK=1`, `CMGP_MUX3=1`, `CMGP_DIV3=1`, `CMGP_QCH3=0x3f0001`, `GPM12_CON=GPM13_CON=2` | Eşleşme cihazda doğrulandı; `.23`teki ilk engel giderildi |
| 3. HSI2C mesajı | Samsung `exynos5_i2c_xfer` adres, yön, uzunluk ve RUN sırasını kurar; IRQ 114 ile tamamlanır | Windows aynı mesaj kurulumunu yapar, fakat IRQ 114 yerine bounded sleeping polling ile tamamlanmayı gözler | Son işlem `I2C_REGADDR=I2C_TRYADDR=0x0701a400` (`0x69` charger), `I2C_RETRY=0`, `I2C_PHASE=0`, `I2C_ERR=0`, genel `STATUS=0` | Taşıma katmanı çalışıyor; IRQ 114'ün yokluğu bu flaşın blokajı değil |
| 4. MAX kimlik doğrulama | Parent MFD TOP `0x66`, USBC `0x25`, charger `0x69` istemcilerini oluşturur | TOP ID/rev, USBC HW/FW ve charger registerları aynı sırayla okunur | `PMIC_ID=0x15`, `PMIC_REV=0x2`, `USBC_HW=0x1a`, `USBC_FW=0x1a`, `USBC_MINOR=0x10` | MAX77705 fiziksel erişimi ve kimlik doğrulaması başarılı |
| 5. Olay mekanizması | GPIO parent IRQ 318; alt CC/MUIC/PD/APCMD IRQ'ları nested IRQ olarak işlenir | MAX GPIO IRQ ACPI'de yok; read-to-clear status registerları PASSIVE one-shot timer ile 200 ms'de bir taranır, hatada 2 s sonra yeniden denenir | Timer MAX durumunu okuyup READY'e ulaşıyor; ancak attach olayı Linux'taki IRQ zinciriyle yürütülmüyor | Polling işlevsel, fakat Linux IRQ semantiği ve tepki süresiyle eşdeğer değil |
| 6. Probe/init opcodes ve kesme düzeni | USBC/PD probe; `UIC/CC/PD/VDM` maskeleri önce probe, sonra çalışma vektörüne geçer; CCCTRL ve firmware komut kuyruğu tamamlanır | `.25`: charger unlock; `AUTOIBUS 57 01`; `00/20/1c/ff` probe maskeleri; `AUTO_VBUS 54 00`; `CCCTRL1_W 0c 1f` + `CCCTRL1_R 0b`; `00/20/00/f0` çalışma maskeleri | `.24`: `STAGE=0x70`, `STATUS=0`, fakat yalnız `PD=1c/VDM=ff` kurulmuştu; UIC/CC, çalışma maskesi ve CCCTRL1 readback kanıtı yoktu | İlk cihazda kanıtlanmış uyumsuzluk; `.25` kodu eşliyor, yeni flaş doğrulaması gerekli |
| 7. Fiziksel takılma ve CC | IRQ'da `CC1_ACTIVE`, `PLUG_ATTACHED`, `cc_SOURCE`; veri rolü DFP, güç rolü SRC olur | Poll ile `CC_STATUS0 & 7`; yalnız `CC_SOURCE=2` HOST/OTG'yi açabilir | `.24`: `CC0=0x40`, yani `CCPinStat=CC1_ACTIVE` fakat `CCStat=cc_No_Connection`; source görülmedi | `.25`te 6. adım doğrulanırsa incelenecek sonraki sınır |
| 8. VBUS/charger OTG | `vbus_turn_on_ctrl(1)`, CHGIN-OTG; charger mode `0x04 -> 0x0e`; VBUS HIGH bildirilir | Charger kilidi açılır, interrupt maskesi ayarlanır, `CNFG_00` mevcut moda göre `0x0e` benzeri OTG moda geçirilir ve read-back doğrulanır | `CHG_UNLOCK=1`, `CHG_CFG06=0x0c`, `CHG_MODE=0x15`; `OTG_SW=OTG_HW=0` | Source görülmediği için VBUS güvenli biçimde kapalı tutuluyor; xHCI `PP=1` gerçek konektör VBUS'unu kanıtlamaz |
| 9. MUIC D+/D- yönlendirme | OTG algılanır, switch `0x09`; opcode `06 09` ile USB yolu AP'ye verilir | Source durumunda `CONTROL1_WRITE 06 09`, ardından `CONTROL1_READ 05` ile `0x09` doğrulanır; detach'te `OPEN=0x3f` seçilir | `MUIC_REQ=0x3f`, `MUIC_CTL1=0x3f`, `MUIC_OK=1` | İletişim ve read-back başarılı, fakat CC kararı nedeniyle D+/D- kasıtlı olarak OPEN bırakıldı |
| 10. Rol bildirimi ve DWC3 | `Turn On Host(DFP)` ve `USB_HOST_ATTACHED`; DWC3 role-switch work başlar | Windows'ta Linux notifier zinciri yok. `BYN0002` PrepareHardware içinde doğrudan sabit HOST seçer | BYN0002 ve BYN0003 Started | Windows bu aşamayı MAX sonucundan önce yaptı |
| 11. PHY/runtime resume | PHY connect, LDO on, runtime resume, UTMI init, tune, reset release | PMU izolasyonu kaldırılır; q-channel, PHY reset, UTMI, tune ve DWC3 core reset uygulanır; USB3 gizli tutulur | PHY registerları beklenen host değerlerinde | Host tarafının bu bölümü büyük ölçüde çalışıyor |
| 12. xHCI başlatma | DWC3 host olduktan sonra xHCI başlar, USB bus 1/2 kaydolur | `USBXHCI` inbox sürücüsü ACPI `BYN0003`'e bağlanır; parent önceden DWC3'ü HOST'a hazırlar | xHCI `RUN=1`, command ring/DCBAA/config kurulmuş | Controller çalışıyor; konektör yolu hazır değil |
| 13. Port bağlantısı | Yaklaşık 357 ms sonra full-speed aygıt görülür | USBXHCI'nin PORTSC değişimi görmesi beklenir | Controller `RUN=1`, `DCBAA=1`, `CFG=1`; portlar `CCS=0`, `PED=0`, `PLS=5/RxDetect`; yaklaşık 7.884 örnekte transition yok | Elektriksel bağlantı xHCI'ye ulaşmıyor |
| 14. Enumeration | `new full-speed device`, `046d:c52e`, descriptor/config/interface işlemleri | Inbox hub/USB stack aynı standart enumeration'ı yapar | Root hub altında alıcı yok | Enumeration başlamadı; sorun HID sınıf sürücüsünde değil |
| 15. HID bind | Arayüz 1.0 `hid-generic` keyboard, 1.1 mouse | `USBHUB3 -> HIDUSB -> MOUHID/MOUCLASS` zinciri bağlanır | Hizmetler mevcut fakat “No devices were found” | Fare sorununun kökü HID değil, daha aşağıdaki Type-C/VBUS/MUIC yoludur |
| 16. Detach | VBUS kapatılır, roller geri alınır, MUIC açılır, DWC3 device/idle yoluna döner | Windows CC source dışına çıkınca önce OTG kapatır, sonra MUIC `OPEN=0x3f` doğrular | Gerçek attach/detach test edilmedi; güvenli ayrılma sonucu `OPEN=0x3f` ve OTG off doğrulandı | Güvenli başlangıç durumu kanıtlandı, olay geçişleri henüz kanıtlanmadı |

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
| USB D+/D- yolunu AP'ye ver | `CONTROL1_WRITE: 06 09` | Source görüldüğünde `06 09`, sonra `05` read-back | Uygulama var; `.24`te source görülmediği için bu dala girilmedi |
| Firmware AUTOIBUS | `57 01`, cevap `57` | `57 01`, APCMDRES ve cevap opcode kontrolü | `.24` READY sonucu, opcode taşıma yolunun tamamlandığını doğruluyor |
| Otomatik VBUS'u kapat | Samsung init davranışı | `54 00` | `.24` init zincirinde başarıyla tamamlandı |
| Kesme maskeleri | Çalışan log: `UIC/CC/PD/VDM=00/20/00/f0`; probe sırasında `PD/VDM=1c/ff` ara durumu | `.25`: `00/20/1c/ff`, init sonrası `00/20/00/f0`; her register readback ile doğrulanır | Kod eşlendi; cihaz kanıtı bekleniyor |
| CC kontrolü | `CCCTRL1_W 0c 1f`; sonrasında IRQ tabanlı rol durumu | `.25`: `0c 1f`, `0b` readback=`1f`, ardından polling ile `CC_STATUS0` | `.24`te yalnız yazma cevabı vardı; `.25` cihaz kanıtı bekleniyor |
| Charger OTG | `CNFG_00 0x04 -> 0x0e` | Source görülürse mevcut moda göre `0x0e/0x0f/...`; read-back ve rollback | Source görülmediği için çalıştırılmadı; `CHG_MODE=0x15` OTG içermiyor |
| Detach izolasyonu | VBUS off, switch open | OTG off, ardından MUIC `0x3f` | `.24`te `MUIC_OK=1`, `MUIC_CTL1=0x3f`, `OTG_SW/HW=0` ile cihazda doğrulandı |

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
CC_STATUS == SOURCE
MuicPathVerified == USB_AP
OtgHardwareActive
```

`.24` bu ayrımı görünür hale getirmiştir: MAX tarafı READY'e ulaşsa bile `CC_STATUS == SOURCE`, `MuicPathVerified == USB_AP` ve `OtgHardwareActive` koşulları oluşmamıştır. BYN0002/BYN0003 yine HOST/RUN durumuna geçtiği için controller çalışır, fakat konektör zinciri hazır değildir.

### 2. `.24` transportu tamamladı, fakat probe/init eşdeğerliği eksik kaldı

Son Windows kanıtında platform, HSI2C, kimlik ve opcode katmanları başarıyla tamamlanmıştır:

```text
STAGE=0x70                 STATUS=0x0
PLATFORM_OK=0x1            I2C_RETRY=0x0
PMIC_ID=0x15               PMIC_REV=0x2
USBC_HW/FW/MINOR=1a/1a/10  CHG_UNLOCK=0x1
MUIC_OK=0x1                CC0=0x40
OTG_SW/HW=0/0
```

Bu çıktı I2C/opcode taşımasının çalıştığını kanıtlar; ancak `.24` UIC/CC maskelerini kurmadığı, çalışma maskesine geçmediği ve CCCTRL1'i geri okumadığı için `CC0=0x40` sonucunu artık tam Linux init'inden sonraki attach kararı olarak kabul etmiyoruz. `bits[7:6]=1` stok Samsung tanımında `CC1_ACTIVE`, `bits[2:0]=0` ise `cc_No_Connection` değeridir; fakat 7. adımın gerçek blokaj olup olmadığı `.25`te 6. adım doğrulandıktan sonra yeniden değerlendirilecektir.

Tek aktif CPU açısından sonuç olumludur: WinPE test sonuna ve komut istemine ulaşmış, xHCI gözlemcisi yaklaşık 7.884 örnek toplamış ve controller `RUN=1` kalmıştır. PASSIVE timer ve bounded sleeping polling boot CPU'sunu kilitlememiştir. `.25` bu çalışan taşıma modelini koruyarak yalnız eksik MAX77705 init/kesme sözleşmesini tamamlar.

### 3. HSI2C platform hazırlığı `.24` ile cihazda doğrulandı

`BYN0004` artık HSI2C ve USI yanında CMGP CMU `0x15c00000..0x15c07fff` ile CMGP pinctrl `0x15c30000..0x15c30fff` kaynaklarını da tek sahip olarak alır. Başlatma sırası şöyledir:

1. GPM12-0 ve GPM13-0 için pull/drive sıfırlanır, function `2` seçilir.
2. `DIV_CLK_USI_CMGP3` (`+0x1824`) oran `1` ile divide-by-two durumuna alınır.
3. `MUX_CLK_USI_CMGP3` (`+0x1020`) 400 MHz CMGP bus parent'a alınır; sonuç 200 MHz'tir.
4. `USI_CMGP3_QCH` (`+0x304c`) normal enable durumuna (`bits[2:0]=1`) alınır.
5. Linux'un `200 MHz / 400 kHz` hesabıyla FS1=`0x00f00000`, FS2=`0x000000e0`, FS3=`0x00210000` alanları controller resetinden sonra kurulur.
6. Pin, clock ve timing alanları geri okunur; doğrulama başarısızsa HSI2C transferine geçilmez ve `HsI2cFailurePhase=0x08` yayımlanır.

Canlı `.24` değerleri bu sıranın tamamlandığını gösterir: `PLATFORM_OK=1`, mux/div değerleri `1`, QCH alt üç biti `1`, GPM12/GPM13 function alanları `2` ve ilk kimlik okumasından sonraki charger erişimleri de başarılıdır. `.23`teki `ADDR1` timeout'u artık yeniden üretilmemiştir.

Linux'un gerçek IRQ 114 ile HSI2C transfer tamamlaması hâlâ Windows'ta yoktur; Windows tamamlanmayı PASSIVE bağlamda bounded polling ile gözler. `.24`ün hatasız kimlik/opcode/charger erişimleri IRQ114'ün önceki `ADDR1` arızasının sebebi olmadığını kanıtlar. IRQ114 nihai sürücüde ISR/DPC veya uygun passive completion zinciri için eklenmelidir, ancak mevcut `CCStat=No Connection` ve VBUS-off sonucunu tek başına değiştirmez. Ayrıca IRQ114, Linux'taki MAX77705 GPIO parent IRQ 318'den farklıdır.

Samsung DT `default-clk=200000000` ister. Vendor CAL tablosunda USI_CMGP3 için yalnız 400 MHz normal seviye bulunduğundan Linux'taki `clk_set_rate(200 MHz)` çağrısı platform durumuna göre başarısız olabilir; sürücü yine `clk_get_rate()` ile etkin hızı okuyup timing hesaplar. Windows clock framework kullanmadığı için bu belirsizliği taşımıyor: 400 MHz parent + oran `1` ile DT'nin amaçladığı 200 MHz'i açıkça kuruyor ve timing'i bu hıza göre yazıyor.

### 4. İki ayrı IRQ zinciri yerine polling kullanılıyor

Linux'ta HSI2C transfer completion IRQ114 ile; MAX77705 CC/MUIC/PD/APCMD olayları ise GPIO parent IRQ318 ve nested IRQ'larla yürür. Windows her iki alanda da polling kullanır: HSI2C transferi bounded sleeping polling, MAX olayları 200 ms timer taramasıyla izlenir. `.24` birincisinin çalıştığını kanıtladı; ancak ikinci fark kısa CC geçişlerini, olay sırasını veya rol oluşumunu etkileyebilir. BYN0003 üzerinde görülen IRQ273 yalnız USBXHCI kaynağıdır ve bu iki MAX/HSI2C IRQ'sının yerine geçmez.

### 5. Windows attach sırası Linux ile tam aynı değil ve source kararı oluşmuyor

Canlı Linux kaydında `CC1_ACTIVE` ardından `PLUG_ATTACHED` ve `cc_SOURCE` oluşur; charger/VBUS ve host notifier çok erken çalışır, MUIC `06 09` hemen arkasından gelir. `.24`te Windows yalnız `CC1_ACTIVE` kısmını görmüş, aynı status byte'ında `cc_No_Connection` kalmıştır. Bu nedenle source dalına hiç girmez.

Source oluştuğunda Windows güvenlik amacıyla önce MUIC `06 09` + `05` doğrulamasını, sonra charger OTG/VBUS açılmasını şart koşar. Bu sıra Samsung ile birebir değildir; ancak son kanıt daha erken bir sınırda durduğu için öncelik, stok Linux'taki CCCTRL/DRP rol kurulumu, yerleşme süresi ve event-driven status güncellemesini karşılaştırmaktır. CC source doğrulanmadan VBUS'u zorla açmak bu belgedeki güvenlik koşulunu ihlal eder.

### 6. Linux koordinasyon katmanları Windows'ta sadeleştirilmiş

Linux akışı charger, battery, MUIC, Type-C manager, USB notifier, runtime-PM ve role-switch bileşenlerini birlikte yürütür. Windows sürücüsü gerekli register/opcode çekirdeğini uygular ama bu framework koordinasyonunun tamamını modellemez. Sabit HOST hedefi için kabul edilebilir olabilir; attach/detach, hata kurtarma ve güç politikası açısından tam eşdeğer değildir.

## Son teşhis

Son Windows flaşında fareyi engelleyen sıra şöyledir:

1. BYN0004 dört MMIO kaynağıyla Started olur; CMGP/pinctrl/HSI2C hazırlığı doğrulanır.
2. TOP, USBC ve charger erişimleri ile init opcode zinciri tamamlanır; sürücü `STAGE=READY`, `STATUS=SUCCESS` durumuna ulaşır.
3. `CC_STATUS0=0x40`, CC1 pinini aktif gösterirken güç rolünü `cc_No_Connection` bırakır; Linux'taki `PLUG_ATTACHED -> cc_SOURCE` geçişi oluşmaz.
4. Windows güvenli olarak VBUS/OTG'yi kapalı ve MUIC yolunu `OPEN=0x3f` tutar.
5. Buna rağmen `_DEP` karşılanmış sayıldığı için BYN0002 DWC3/PHY'yi HOST yapar ve USBXHCI çalışır.
6. xHCI elektriksel attach göremez; portlar `RxDetect`, `CCS=0` kalır ve USBHUB3/HIDUSB/MOUHID aşamalarına gelinmez.

Bu nedenle mevcut öncelik HID, xHCI ring, HSI2C platformu veya IRQ114 değildir. Bir sonraki karşılaştırma sınırı stok Linux'taki CCCTRL/DRP başlangıcı, CC status yerleşme zamanı ve MAX GPIO IRQ318 üzerinden ilerleyen attach/rol olay zinciridir. Windows'ta `cc_SOURCE` güvenilir biçimde doğrulandıktan sonra MUIC `USB_AP`, charger OTG/VBUS ve en son MAX READY ile BYN0002 HOST başlangıcı arasındaki çalışma zamanı kapısı tamamlanmalıdır.

## Kaynak konumları

- Linux canlı kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`, özellikle 309-375, 538-715, 747-789, 842-925 ve 978-1016.
- ACPI MAX ve USB bağımlılığı: `silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl`, 254-329.
- Windows MAX durum makinesi: `windows/b2lusb/b2lmax77705.c`, 1145-1222.
- Windows MAX timer/PnP: `windows/b2lusb/b2lmax77705.c`, 1337-1540.
- Windows HSI2C platform ve taşıma katmanı: `windows/b2lusb/b2lmax77705_hw.c`, 162-727.
- Windows sabit HOST başlangıcı: `windows/b2lusb/b2lusb.c`, 230-293 ve 398-420.
- Windows PHY/DWC3 hazırlığı: `windows/b2lusb/b2lusb_hw.c`, 422-665.
