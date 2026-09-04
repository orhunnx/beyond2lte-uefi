# Beyond2LTE USB host akışı: Samsung Linux ve Windows karşılaştırması

## Kapsam ve kanıt sürümleri

Bu belge, Logitech `046d:c52e` alıcısının Samsung Linux/Lineage altında fiziksel olarak takılmasından HID klavye ve farenin oluşmasına kadar çalışan akışı, mevcut Windows sürücüleriyle yan yana karşılaştırır.

- Linux kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
- Son Windows cihaz kanıtı: ekrandaki `b2lmax77705` sürümü `0.1.0.26`
- Son cihazda doğrulanmış kaynak: `0.1.0.26`
- Mevcut kaynak adayı: `windows/b2lusb/b2lmax77705.inf.in` sürümü `0.1.0.27`; henüz cihazda doğrulanmadı
- `.26` cihaz kanıtı: kullanıcının son flaştan sağladığı ekran görüntüleri. Logitech `046d:c52e` alıcısı, çalışan recovery/Linux analizindekiyle aynı biçimde bütün Windows testi boyunca fiziksel olarak takılıydı.

## En kısa sonuç

`.26`, 6. probe/init adımının büyük kısmını cihaz üzerinde doğrulamıştır. Sürücü `STAGE=0x70`, `STATUS=0`, `PLATFORM_OK=1` durumuna ulaşır; PMIC/USBC kimliklerini okur, firmware opcode yolunu tamamlar ve charger kilidini açar. Linux'un çalışma kesme maskesi `UIC/CC/PD/VDM=00/20/00/f0` donanımdan aynı değerlerle geri okunmuş (`MASKS_OK=1`), `CCCTRL1_W 0c 1f` sonrasındaki `CCCTRL1_R 0b` sonucu da `0x1f` olmuştur. Ancak Linux sürücüsü probe'un sonunda ayrıca `cc_open_req=1` ayarlayıp `pdic_manual_ccopen_request(0)` çağırır; bu yol `CONTROL3_R=0x1c` ile durumu okuyup `LockErrorEn` bit 1 açıksa diğer bitleri koruyarak `CONTROL3_W=0x1d` ile temizler. `.26` bu son normalizasyonu yapmıyordu. Bu nedenle önceki “6. adım tamamen kapalı” hükmü fazla güçlüdür: ilk eksik işlem 6→7 sınırındaki probe-final CC-open bırakmasıdır.

İlk cihazda gözlenen işlevsel uyuşmazlık yine 7. Type-C attach/rol adımıdır: fiziksel Logitech alıcı takılıyken `CC0=0x40`, yani `CCPinStat=CC1_ACTIVE` görülür; buna karşın `CC_STATE=CC_PREV=0`, `CC_TRANS=0` ve `CC_SRC_EV=0` kalır. Olay yolu 2.518 CC örneği almış, sıfır olmayan bir CC interrupt-register örneği (`CC_IRQS=1`) ve birikimli `CC_SEEN=0x7d` yakalamış, fakat hiçbir örneği `cc_SOURCE` rolüne çevirememiştir. `.27`, bunun hemen öncesindeki eksik Linux probe-final işlemini tamamlar; sonucu `CCOPEN_OK`, `CTRL3_BEFORE` ve `CTRL3_AFTER` ile ayırır. Bu, SOURCE rolünü doğrudan üretmez: yalnız CC durum makinesinin Linux ile aynı kilit durumundan başlamasını sağlar ve hipotezi ölçülebilir kılar.

Kesme registerlarının okunabilmesi, MAX77705 GPIO kaynaklı kesmenin Windows'a donanımsal IRQ olarak teslim edildiğini kanıtlamaz. Ekrandaki IRQ `273`, BYN0003/USBXHCI kaynağıdır; Linux'ta görünen HSI2C IRQ114 veya MAX77705 parent GPIO IRQ318 değildir. `.26` doğrulanmış MAX ACPI IRQ kaynağı olmadığı için bu registerları 20 ms timer ile tarar. Buna rağmen `TOP_SEEN=0x9`, `CC_SEEN=0x7d`, `PD_SEEN=0xc0` ve `CC_IRQS=1`, read-to-clear olay baytlarının en az bir kez yakalandığını kanıtlar. Logitech alıcı test boyunca takılıyken 2.518 örneğin hiçbirinde rol değişmemesi, önceki “kısa olay 200 ms polling tarafından kaçırılmış olabilir” açıklamasını büyük ölçüde dışlar; sorun artık olay gözleminden sonra `CC_STATUS0` alt rol bitlerinin `No Connection` kalmasıdır.

`.26` olay ayrımını cihaz üzerinde ölçülebilir hale getirmiştir: TOP `INTSRC` önce okunur, dört USBC kesme baytı tek read-to-clear blokta alınır, hemen arkasından `CC_STATUS0/1` okunur. Kesme OR-birikimleri ve CC sayaçları son ekran özetine ulaşmıştır. GPA1_6 için doğrulanmış ACPI/GIC kaynağı bulunmadığından `.27` de sahte ISR kurmaz. Agresif polling kalıcı olmaktan çıkarılmıştır: MAX tarafında 20 ms tarama yalnız ilk 50 başarılı CC örneğinde, yani yaklaşık ilk bir saniyelik attach tanı penceresinde kullanılır; ardından rol hâlâ ayrık olsa bile 200 ms sağlık temposuna dönülür. xHCI salt-okuma gözlemcisi de yüksek çözünürlüklü 1 ms/12 s yerine normal 20 ms/5 s pencere kullanır; üst sınır yaklaşık 12.000 yerine 250 zamanlanmış örnektir.

Ana mimari fark şudur:

```text
Samsung Linux:
MAX IRQ -> CC=SOURCE -> VBUS/OTG + rol bildirimi -> MUIC USB_AP
        -> DWC3/PHY runtime resume -> xHCI -> USB enumerate -> HID

Windows `.26` cihaz sonucu ve `.27` adayı:
ACPI _DEP yalnızca BYN0004 sürücüsünün başlamasını bekler
        +-> BYN0002 hemen PHY/DWC3 HOST yapar -> USBXHCI çalışır
        `-> BYN0004 zamanlayıcısı platform + HSI2C + MAX init'i tamamlar
            -> `.27`: CONTROL3 LockErrorEn temizle + read-back
            -> TOP + USBC IRQ blok okuma -> hemen CC_STATUS örneği/geçiş kaydı
            -> takılı Logitech alıcıya rağmen CC1 pini görülür,
               fakat CCStat=No Connection kalır
            -> güvenli yol: VBUS kapalı + MUIC OPEN -> xHCI attach göremez
```

## Adım adım yan yana karşılaştırma

| Aşama | Çalışan Samsung Linux akışı | Mevcut Windows kaynağının hedefi | Son Windows flaşında gözlenen | Sonuç |
|---|---|---|---|---|
| 1. Donanım sahipliği | `15d60000.hsi2c -> i2c-14 -> 14-0066 -> max77705-usbc`; TOP/USBC/charger alt adresleri tek MFD altında | `BYN0004`, HSI2C `0x15D60000`, USI `0x15C22000`, CMGP CMU ve ilgili pinctrl sayfalarını tek başına eşler; `0x66/0x25/0x69` adreslerini kendisi yönetir | `.26`da `BYN0004` Started; `0x15C00000`, `0x15D60000`, `0x15C22000` ve `0x15C30000` olmak üzere dört MMIO alanı atanmış | Eşleşen sahiplik modeli ve kaynak kapsamı cihazda doğrulandı |
| 2. HSI2C güç/saat hazırlığı | Linux pinctrl ile GPM12-0/GPM13-0'ı işlev 2 yapar; DT 200 MHz ister, clock framework mevcut hızı okur; QCH/gate hazırlanır ve 400 kHz timing hesaplanır | `.24`, aynı pinctrl ve CMGP3 alanlarını BYN0004'e verir; 200 MHz'i deterministik kurar, QCH'yi ve buna karşılık gelen FS timing alanlarını yazar, geri okur | `PLATFORM_OK=1`, `CMGP_MUX3=1`, `CMGP_DIV3=1`, `CMGP_QCH3=0x3f0001`, `GPM12_CON=GPM13_CON=2` | Eşleşme cihazda doğrulandı; `.23`teki ilk engel giderildi |
| 3. HSI2C mesajı | Samsung `exynos5_i2c_xfer` adres, yön, uzunluk ve RUN sırasını kurar; Linux ad alanında IRQ 114 ile tamamlanır | Windows aynı mesaj kurulumunu yapar; karşılık gelen GIC/ACPI interrupt kaynağı doğrulanmadığı için bounded sleeping polling ile tamamlanmayı gözler | Son işlem `I2C_REGADDR=I2C_TRYADDR=0x0701a400` (`0x69` charger), `I2C_RETRY=0`, `I2C_PHASE=0`, `I2C_ERR=0`, genel `STATUS=0` | Taşıma katmanı çalışıyor; Linux IRQ numarasını doğrudan kullanmamak bu flaşın blokajı değil |
| 4. MAX kimlik doğrulama | Parent MFD TOP `0x66`, USBC `0x25`, charger `0x69` istemcilerini oluşturur | TOP ID/rev, USBC HW/FW ve charger registerları aynı sırayla okunur | `PMIC_ID=0x15`, `PMIC_REV=0x2`, `USBC_HW=0x1a`, `USBC_FW=0x1a`, `USBC_MINOR=0x10` | MAX77705 fiziksel erişimi ve kimlik doğrulaması başarılı |
| 5. Olay mekanizması | GPIO parent IRQ 318; TOP `INTSRC` sonrası alt CC/MUIC/PD/APCMD registerları blok okunur ve nested IRQ işlenir | Doğrulanmış MAX GPIO ACPI kaynağı yok; TOP `INTSRC` + dört read-to-clear USBC baytı tek blokta, hemen ardından CC status okunur. `.27`: 20 ms yalnız ilk 50 CC örneği, sonra 200 ms | `.26`: `TOP_SEEN=0x9`, `CC_SEEN=0x7d`, `PD_SEEN=0xc0`, `CC_IRQS=1`, `CC_SAMPLES=0x9d6`; BYN0004 üzerinde IRQ kaynağı yok | Okuma sırası cihazda çalıştı; `.27` aynı görünürlüğü sınırlı hızlı pencereyle koruyor, polling değişikliği cihaz doğrulaması bekliyor |
| 6. Probe/init opcodes ve kesme düzeni | USBC/PD probe; `UIC/CC/PD/VDM` maskeleri önce probe, sonra çalışma vektörüne geçer; CCCTRL ve firmware komut kuyruğu tamamlanır; en sonda manual CC-open off isteği CONTROL3 `LockErrorEn` bitini temizler | `.27`: charger unlock; `57 01`; `00/20/1c/ff`; `54 00`; `0c 1f` + `0b`; `00/20/00/f0`; sonra `1c` read, gerekirse `1d` masked write ve tekrar `1c` read-back | `.26`, CONTROL3 öncesine kadar bütün değerleri doğruladı; yeni `CCOPEN_OK/CTRL3_BEFORE/CTRL3_AFTER` alanları henüz cihazdan alınmadı | Önceki sürüm 6. adımın son işlemini atlıyordu. `.27` kaynakta eşleştirdi; cihaz doğrulaması gerekli |
| 7. Fiziksel takılma ve CC | IRQ'da `CC1_ACTIVE`, `PLUG_ATTACHED`, `cc_SOURCE`; veri rolü DFP, güç rolü SRC olur | `.27`, probe-final normalizasyondan sonra CC örnek/geçiş/source sayaçlarını sürdürür ve yalnız `CC_SOURCE=2` ile HOST/OTG'yi açabilir | `.26`: aynı Logitech alıcı takılıyken `CC0/CC1=40/00`, `CC_STATE=CC_PREV=0`, `CC_TRANS=0`, `CC_SRC_EV=0`; 2.518 örnek ve olay baytları canlı | İşlevsel blokaj 7'de görünür; `.27` bunun hemen öncesindeki ilk eksik işlemi sınar. SOURCE oluşup oluşmadığı yeni flaşla görülecek |
| 8. VBUS/charger OTG | `vbus_turn_on_ctrl(1)`, CHGIN-OTG; charger mode `0x04 -> 0x0e`; VBUS HIGH bildirilir | Charger kilidi açılır, interrupt maskesi ayarlanır, `CNFG_00` mevcut moda göre `0x0e` benzeri OTG moda geçirilir ve read-back doğrulanır | `CHG_UNLOCK=1`, `CHG_CFG06=0x0c`, `CHG_MODE=0x15`; `OTG_SW=OTG_HW=0` | Source görülmediği için VBUS güvenli biçimde kapalı tutuluyor; xHCI `PP=1` gerçek konektör VBUS'unu kanıtlamaz |
| 9. MUIC D+/D- yönlendirme | OTG algılanır, switch `0x09`; opcode `06 09` ile USB yolu AP'ye verilir | Source durumunda `CONTROL1_WRITE 06 09`, ardından `CONTROL1_READ 05` ile `0x09` doğrulanır; detach'te `OPEN=0x3f` seçilir | `MUIC_REQ=0x3f`, `MUIC_CTL1=0x3f`, `MUIC_OK=1` | İletişim ve read-back başarılı, fakat CC kararı nedeniyle D+/D- kasıtlı olarak OPEN bırakıldı |
| 10. Rol bildirimi ve DWC3 | `Turn On Host(DFP)` ve `USB_HOST_ATTACHED`; DWC3 role-switch work başlar | Windows'ta Linux notifier zinciri yok. `BYN0002` PrepareHardware içinde doğrudan sabit HOST seçer | BYN0002 ve BYN0003 Started | Windows bu aşamayı MAX sonucundan önce yaptı |
| 11. PHY/runtime resume | PHY connect, LDO on, runtime resume, UTMI init, tune, reset release | PMU izolasyonu kaldırılır; q-channel, PHY reset, UTMI, tune ve DWC3 core reset uygulanır; USB3 gizli tutulur | PHY registerları beklenen host değerlerinde | Host tarafının bu bölümü büyük ölçüde çalışıyor |
| 12. xHCI başlatma | DWC3 host olduktan sonra xHCI başlar, USB bus 1/2 kaydolur | `USBXHCI` inbox sürücüsü ACPI `BYN0003`'e bağlanır; parent önceden DWC3'ü HOST'a hazırlar | xHCI `RUN=1`, command ring/DCBAA/config kurulmuş | Controller çalışıyor; konektör yolu hazır değil |
| 13. Port bağlantısı | Yaklaşık 357 ms sonra full-speed aygıt görülür | USBXHCI'nin PORTSC değişimi görmesi beklenir | Logitech alıcı fiziksel olarak takılıyken controller `RUN=1`, `DCBAA=1`, `CFG=1`; portlar `CCS=0`, `PED=0`, `PLS=5/RxDetect`; yaklaşık 7.887 örnekte transition yok | Elektriksel bağlantı xHCI'ye ulaşmıyor |
| 14. Enumeration | `new full-speed device`, `046d:c52e`, descriptor/config/interface işlemleri | Inbox hub/USB stack aynı standart enumeration'ı yapar | Aynı Logitech alıcı takılı olmasına rağmen root hub altında aygıt yok | Enumeration başlamadı; sorun HID sınıf sürücüsünde değil |
| 15. HID bind | Arayüz 1.0 `hid-generic` keyboard, 1.1 mouse | `USBHUB3 -> HIDUSB -> MOUHID/MOUCLASS` zinciri bağlanır | Hizmetler mevcut fakat “No devices were found” | Fare sorununun kökü HID değil, daha aşağıdaki Type-C/VBUS/MUIC yoludur |
| 16. Detach | VBUS kapatılır, roller geri alınır, MUIC açılır, DWC3 device/idle yoluna döner | Windows CC source dışına çıkınca önce OTG kapatır, sonra MUIC `OPEN=0x3f` doğrular | `.26`da takılı alıcıya rağmen attach oluşmadı; güvenli ayrılma sonucu `OPEN=0x3f` ve OTG off doğrulandı | Güvenli durum kanıtlandı; attach oluşmadığı için gerçek detach geçişi sınanamadı |

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
| USB D+/D- yolunu AP'ye ver | `CONTROL1_WRITE: 06 09` | Source görüldüğünde `06 09`, sonra `05` read-back | Uygulama var; `.26`da source görülmediği için bu dala girilmedi |
| Firmware AUTOIBUS | `57 01`, cevap `57` | `57 01`, APCMDRES ve cevap opcode kontrolü | `.26` READY sonucu, opcode taşıma yolunun tamamlandığını doğruluyor |
| Otomatik VBUS'u kapat | Samsung init davranışı | `54 00` | `.26` init zincirinde başarıyla tamamlandı |
| Kesme maskeleri | Çalışan log: `UIC/CC/PD/VDM=00/20/00/f0`; probe sırasında `PD/VDM=1c/ff` ara durumu | `.26`: `00/20/1c/ff`, init sonrası `00/20/00/f0`; her register readback ile doğrulanır | `.26`da `00/20/00/f0` ve `MASKS_OK=1`; cihazda doğrulandı |
| CC kontrolü | `CCCTRL1_W 0c 1f`; sonrasında IRQ tabanlı rol durumu | `.26`: `0c 1f`, `0b` readback=`1f`; TOP/USBC kesme blok okumasından hemen sonra `CC_STATUS0/1` | `.26`da `CCCTRL1=0x1f`; olay baytları yakalanıyor fakat takılı alıcıya rağmen `CC_STATE=0`, `CC_TRANS=0` |
| Probe-final CC-open bırakması | `cc_open_req=1`; `pdic_manual_ccopen_request(0)`; `CONTROL3_R=0x1c`, gerekirse `LockErrorEn` bit 1 temizlenerek `CONTROL3_W=0x1d` | `.27`: runtime maskelerinden sonra aynı read/masked-write/read-back; `CCOPEN_OK`, `CTRL3_BEFORE`, `CTRL3_AFTER` yayımlanır | Kaynakta uygulandı; `.26` cihaz kanıtında bu işlem yoktu, `.27` cihaz doğrulaması bekleniyor |
| Charger OTG | `CNFG_00 0x04 -> 0x0e` | Source görülürse mevcut moda göre `0x0e/0x0f/...`; read-back ve rollback | Source görülmediği için çalıştırılmadı; `CHG_MODE=0x15` OTG içermiyor |
| Detach izolasyonu | VBUS off, switch open | OTG off, ardından MUIC `0x3f` | `.26`da `MUIC_OK=1`, `MUIC_CTL1=0x3f`, `OTG_SW/HW=0` ile cihazda doğrulandı |

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

`.26` bu ayrımı kesinleştirmiştir: MAX tarafı kendi uyguladığı init ile READY'e ulaşsa ve CC interrupt baytlarını yakalasa bile `CC_STATUS == SOURCE`, `MuicPathVerified == USB_AP` ve `OtgHardwareActive` koşulları oluşmamıştır. Sonradan bulunan probe-final CC-open eksikliği nedeniyle `.26` için “tam Linux uyumlu probe” denemez. BYN0002/BYN0003 yine HOST/RUN durumuna geçtiği için controller çalışır, fakat konektör zinciri hazır değildir.

### 2. `.26` olay gözlemini doğruladı; eksik ilk işlem 6→7 sınırında

Son Windows kanıtında platform, HSI2C, kimlik, opcode, kesme maskeleri ve CCCTRL1 katmanları başarıyla tamamlanmış; yeni olay tanısı da canlı veri üretmiştir:

```text
STAGE=0x70                 STATUS=0x0
PLATFORM_OK=0x1            I2C_RETRY=0x0
PMIC_ID=0x15               PMIC_REV=0x2
USBC_HW/FW/MINOR=1a/1a/10  CHG_UNLOCK=0x1
UIC/CC/PD/VDM=00/20/00/f0  MASKS_OK=0x1
CCCTRL1=0x1f               CC0/CC1=0x40/0x00
TOP_SEEN=0x9               CC_SEEN=0x7d
PD_SEEN=0xc0               CC_IRQS=0x1
CC_SAMPLES=0x9d6           CC_TRANS=0x0
CC_STATE/PREV=0x0/0x0      CC_SRC_EV=0x0
MUIC_OK=0x1                MUIC_CTL1=0x3f
OTG_SW/HW=0/0
```

Bu çıktı maske ve CCCTRL1 dahil `.26`nın uyguladığı init alt adımlarını kapatır; fakat Linux kaynak kodunun tamamı baştan sona karşılaştırıldığında probe sonunda ek bir işlem bulunmuştur. `max77705_usbc_probe()` `cc_open_req=1` sonrasında `pdic_manual_ccopen_request(0)` çağırır. Kapama dalı `CONTROL3_R (0x1c)` sonucundaki `LockErrorEn` bit 1'i denetler; bit açıksa diğer CONTROL3 bitlerini koruyarak `CONTROL3_W (0x1d)` ile temizler. Bu, `CCCTRL1=0x1f` doğrulamasından farklıdır ve `.26`da yoktur.

Dolayısıyla doğru sınır şudur: 7. adımda görülen `CCStat=No Connection` ilk işlevsel semptomdur; ilk eksik Windows eylemi ise 6. adımın sonundaki CC-open bırakmasıdır. `.27` bu işlemi runtime kesme maskelerinden sonra ve ilk CC değerlendirmesinden önce bir kez yapar. `CONTROL3_AFTER & 0x02` sıfır değilse init başarısız sayılır; başarı halinde `CcOpenNormalized=1` olur. Bu işlem `CCCTRL2 ForceError`, VBUS veya SOURCE rolü yazmaz, dolayısıyla başarısız hipotez durumunda güvenlik politikasını gevşetmez.

`CC0=0x40` artık eksik maske/CCCTRL kurulumu öncesindeki veya tek bir geç örnekteki belirsiz sonuç değildir; `.26` init'inden sonra 2.518 örnek boyunca rol değişmemiştir. `bits[7:6]=1` stok Samsung tanımında `CC1_ACTIVE`, `bits[2:0]=0` ise `cc_No_Connection` değeridir. Logitech alıcının aynı fiziksel düzenle test boyunca takılı olduğu bilgisi, fiziksel test girdisinin Linux ve Windows arasında eş olduğunu doğrular.

`CC_IRQS=1` ve birikimli kesme baytları, Linux sırasına göre yapılan TOP/USBC read-to-clear okumasının canlı olay verisi aldığını gösterir. Ancak BYN0004'e atanmış gerçek interrupt kaynağı bulunmadığından bu, parent GPIO interruptının Windows ISR'ına ulaştığı anlamına gelmez; olay timer tarafından okunmuştur. `CC_TRANS=0` ve `CC_SRC_EV=0`, kısa bir SOURCE durumunun 20 ms tarama aralığında görülüp sonra kaybolduğu açıklamasını desteklemez.

WinPE sonunda test sonuna ulaşmış ve xHCI `RUN=1` kalmıştır; fakat kullanıcı PnP/ilk-boot bölümlerinin dakikalar sürdüğünü gözlemlemiştir. `CC_SAMPLES=0x9d6`, ayrık durumda 20 ms MAX taramasının sürekli çalıştığını doğrular. Bunun 1 ms xHCI gözlemcisi, ETW ve PnP taramalarıyla birleşmesi tek aktif CPU üzerinde ağır bir tanı yükü oluşturur. `.27` MAX hızlı pencereyi 50 örnekle sınırlar; xHCI timerını 20 ms/5 s ve normal çözünürlüğe indirir. Böylece MAX yaklaşık 50 Hz'den 5 Hz'e döner, xHCI tanı üst sınırı yaklaşık 12.000'den 250 zamanlanmış örneğe iner. ETW/PnP komutları korunur; dolayısıyla yeni flaş süre farkı doğrudan polling azaltımını da sınayabilir.

### 3. HSI2C platform hazırlığı `.24` ile cihazda doğrulandı

`BYN0004` artık HSI2C ve USI yanında CMGP CMU `0x15c00000..0x15c07fff` ile CMGP pinctrl `0x15c30000..0x15c30fff` kaynaklarını da tek sahip olarak alır. Başlatma sırası şöyledir:

1. GPM12-0 ve GPM13-0 için pull/drive sıfırlanır, function `2` seçilir.
2. `DIV_CLK_USI_CMGP3` (`+0x1824`) oran `1` ile divide-by-two durumuna alınır.
3. `MUX_CLK_USI_CMGP3` (`+0x1020`) 400 MHz CMGP bus parent'a alınır; sonuç 200 MHz'tir.
4. `USI_CMGP3_QCH` (`+0x304c`) normal enable durumuna (`bits[2:0]=1`) alınır.
5. Linux'un `200 MHz / 400 kHz` hesabıyla FS1=`0x00f00000`, FS2=`0x000000e0`, FS3=`0x00210000` alanları controller resetinden sonra kurulur.
6. Pin, clock ve timing alanları geri okunur; doğrulama başarısızsa HSI2C transferine geçilmez ve `HsI2cFailurePhase=0x08` yayımlanır.

Canlı `.24`, `.25` ve `.26` değerleri bu sıranın tamamlandığını gösterir: `PLATFORM_OK=1`, mux/div değerleri `1`, QCH alt üç biti `1`, GPM12/GPM13 function alanları `2` ve ilk kimlik okumasından sonraki charger erişimleri de başarılıdır. `.23`teki `ADDR1` timeout'u artık yeniden üretilmemiştir.

Linux logunda HSI2C transfer completion için görülen IRQ 114, Linux IRQ ad alanındaki sanal numaradır; Windows ACPI'ye doğrudan `114` olarak aktarılamaz. BYN0004 için karşılık gelen GIC SPI ve trigger türü doğrulanmış bir interrupt kaynağı bulunmadığından Windows tamamlanmayı PASSIVE bağlamda bounded polling ile gözler. `.24`–`.26` arasındaki hatasız kimlik/opcode/charger erişimleri bu seçimin önceki `ADDR1` arızasının veya mevcut `CCStat=No Connection` sonucunun sebebi olmadığını kanıtlar. ISR/DPC yolu ancak DT/GIC eşlemesi doğrulanıp doğru ACPI interrupt kaynağı yayımlandıktan sonra eklenmelidir. HSI2C completion kesmesi, Linux'ta IRQ 318 olarak görünen MAX77705 GPIO parent kesmesinden de ayrıdır.

Samsung DT `default-clk=200000000` ister. Vendor CAL tablosunda USI_CMGP3 için yalnız 400 MHz normal seviye bulunduğundan Linux'taki `clk_set_rate(200 MHz)` çağrısı platform durumuna göre başarısız olabilir; sürücü yine `clk_get_rate()` ile etkin hızı okuyup timing hesaplar. Windows clock framework kullanmadığı için bu belirsizliği taşımıyor: 400 MHz parent + oran `1` ile DT'nin amaçladığı 200 MHz'i açıkça kuruyor ve timing'i bu hıza göre yazıyor.

### 4. İki ayrı IRQ zinciri yerine sınırlı, doğrulanabilir polling kullanılıyor

Linux'ta HSI2C transfer completion Linux IRQ114 ile; MAX77705 CC/MUIC/PD/APCMD olayları ise Linux GPIO parent IRQ318 ve nested IRQ'larla yürür. Windows her iki alanda da polling kullanır: HSI2C transferi bounded sleeping polling; MAX olayları `.27`de ilk 50 başarılı CC örneğinde 20 ms, sonrasında ve yerleşmiş rolde 200 ms timer taramasıyla izlenir. TOP kaynak registerı önce, dört child kesme registerı tek blokta, CC status da hemen arkasından okunur. Read-to-clear örnekler ayrıca OR-birikimi ve sayaçlarla korunur. `.26` cihaz sonucu bu yolun `CC_SEEN=0x7d` ve `CC_IRQS=1` ürettiğini doğrular; ancak 2.518 örnekte geçiş/source üretmemiştir. BYN0003 üzerinde görülen IRQ273 yalnız USBXHCI kaynağıdır ve bu iki MAX/HSI2C kesme yolunun yerine geçmez; doğrulanmış GPA1_6 kaynağı gelene kadar `.27` gerçek bir ISR iddiasında bulunmaz. 200 ms geç dönem tarama kalıcı 50 Hz yükü kaldırırken, fiziksel olarak sonradan takılan ve kalıcı olması beklenen SOURCE durumunu hâlâ gözleyebilir.

### 5. Windows attach sırası Linux ile tam aynı değil ve source kararı oluşmuyor

Canlı Linux kaydında aynı Logitech alıcıyla `CC1_ACTIVE` ardından `PLUG_ATTACHED` ve `cc_SOURCE` oluşur; charger/VBUS ve host notifier çok erken çalışır, MUIC `06 09` hemen arkasından gelir. `.26`da Windows tam probe/init ve olay taraması sonrasında yalnız `CC1_ACTIVE` kısmını görmüş, aynı status byte'ında `cc_No_Connection` kalmıştır. Bir CC interrupt örneği yakalanmasına rağmen 2.518 status örneğinde değişiklik olmamış; source dalına hiç girilmemiştir.

Source oluştuğunda Windows güvenlik amacıyla önce MUIC `06 09` + `05` doğrulamasını, sonra charger OTG/VBUS açılmasını şart koşar. Bu sıra Samsung ile birebir değildir; ancak son kanıt daha erken bir sınırda durduğu için `.27` yalnız stok probe-final CONTROL3 işlemini ekler. CC source doğrulanmadan VBUS'u zorla açmak bu belgedeki güvenlik koşulunu ihlal eder.

### 6. Linux koordinasyon katmanları Windows'ta sadeleştirilmiş

Linux akışı charger, battery, MUIC, Type-C manager, USB notifier, runtime-PM ve role-switch bileşenlerini birlikte yürütür. Windows sürücüsü gerekli register/opcode çekirdeğini uygular ama bu framework koordinasyonunun tamamını modellemez. Sabit HOST hedefi için kabul edilebilir olabilir; attach/detach, hata kurtarma ve güç politikası açısından tam eşdeğer değildir.

## Son teşhis ve `.27` doğrulama ölçütü

Son `.26` Windows flaşında, Logitech alıcı test boyunca takılıyken fareyi engelleyen sıra şöyledir:

1. BYN0004 dört MMIO kaynağıyla Started olur; CMGP/pinctrl/HSI2C hazırlığı doğrulanır.
2. TOP, USBC ve charger erişimleri ile `.26`nın init opcode zinciri tamamlanır; Linux çalışma maskeleri `00/20/00/f0` ve `CCCTRL1=0x1f` geri okumayla doğrulanır; sürücü `STAGE=READY`, `STATUS=SUCCESS` durumuna ulaşır. Ancak Linux probe'un son CONTROL3/CC-open bırakma işlemi `.26`da yoktur.
3. Timer tabanlı Linux okuma sırası olay baytlarını yakalar: `TOP_SEEN=0x9`, `CC_SEEN=0x7d`, `PD_SEEN=0xc0`, `CC_IRQS=1`.
4. Buna rağmen 2.518 örnekte `CC_STATE=CC_PREV=0`, `CC_TRANS=0`, `CC_SRC_EV=0` kalır. Fiziksel alıcı varken `CC_STATUS0=0x40`, CC1 pinini aktif gösterir fakat güç rolünü `cc_No_Connection` bırakır; Linux'taki `PLUG_ATTACHED -> cc_SOURCE` geçişi oluşmaz.
5. Windows güvenli olarak VBUS/OTG'yi kapalı ve MUIC yolunu `OPEN=0x3f` tutar.
6. Buna rağmen `_DEP` karşılanmış sayıldığı için BYN0002 DWC3/PHY'yi HOST yapar ve USBXHCI çalışır.
7. xHCI elektriksel attach göremez; yaklaşık 7.887 örnekte portlar `RxDetect`, `CCS=0` kalır ve USBHUB3/HIDUSB/MOUHID aşamalarına gelinmez.
8. Sürekli 20 ms MAX taraması ile 1 ms xHCI gözlemcisi/PnP-ETW tanılarının birleşimi WinPE akışını dakikalar mertebesinde yavaşlatır.

Bu nedenle mevcut öncelik HID, xHCI ring, HSI2C platformu, kesme maskesi veya Linux'taki sanal IRQ114 numarasını doğrudan kullanmak değildir. `.27`, bulunan ilk eksik eylemi CONTROL3 masked read/modify/write/read-back olarak 6→7 sınırına ekler ve agresif polling'i sınırlar. Sonraki cihaz testinde şu sonuçlar ayrıştırıcıdır:

1. `CCOPEN_OK=1` ve `(CTRL3_AFTER & 0x02)=0` olmalı. Bu yoksa sürücü probe-final aşamasına ulaşmamış veya opcode/read-back başarısız olmuştur.
2. `CTRL3_BEFORE` bit 1 açıksa ve `CTRL3_AFTER` bit 1 kapanmışsa `.27` gerçek bir donanım durumunu değiştirmiştir. Ardından `CC_STATE=2`, `CC_SRC_EV>0`, MUIC `0x09` ve OTG açılırsa eksik halka CONTROL3 kilit bırakmasıydı.
3. `CTRL3_BEFORE` bit 1 zaten kapalıysa veya başarıyla kapandığı halde `CC_STATE=0` kalırsa bu hipotez yanlışlanır; sıradaki karşılaştırma ForceError/CC state-machine koordinasyonu ve event-driven status güncellemesidir. Bu durumda da VBUS zorlanmamalıdır.
4. `CC_SAMPLES` test süresine göre artık binlerce hızla büyümemeli; ilk yaklaşık bir saniyeden sonra saniyede yaklaşık beş artmalıdır. xHCI `samples` sayısı beş saniyelik tek gözlemde yaklaşık 250 civarında kalmalıdır.

`cc_SOURCE` güvenilir biçimde doğrulandıktan sonraki ayrı iş, MAX READY ile BYN0002 HOST başlangıcı arasındaki çalışma zamanı kapısını kurmaktır. Bu yapısal kapı, `.27`deki kontrollü hipotez testine özellikle karıştırılmamıştır.

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
- Windows PHY/DWC3 hazırlığı: `windows/b2lusb/b2lusb_hw.c`, 422-665.
