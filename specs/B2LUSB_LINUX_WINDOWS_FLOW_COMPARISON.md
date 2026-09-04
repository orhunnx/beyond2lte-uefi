# Beyond2LTE USB host akışı: Samsung Linux ve Windows karşılaştırması

## Kapsam ve kanıt sürümleri

Bu belge, Logitech `046d:c52e` alıcısının Samsung Linux/Lineage altında fiziksel olarak takılmasından HID klavye ve farenin oluşmasına kadar çalışan akışı, mevcut Windows sürücüleriyle yan yana karşılaştırır.

- Linux kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
- Son Windows cihaz kanıtı: ekrandaki `b2lmax77705` sürümü `0.1.0.23`
- Mevcut kaynak: `windows/b2lusb/b2lmax77705.inf.in` sürümü `0.1.0.24`
- Önemli ayrım: `0.1.0.23` flaşı, HSI2C'deki korunmuş/stale durum üzerinde transfer başlatan `44deb23` ile MAX işini tek boot CPU'sunun dışına alan `5608362` commitlerini içerir; bu turda eklenen platform hazırlığını içermez.

## En kısa sonuç

Windows kaynağında Linux'taki temel MAX77705 komutlarının çoğu artık vardır; fakat son cihaz çalıştırmasında bu komutlara ulaşılamamıştır. `.23` doğru `0x66` hedef adresiyle transferi başlatmış, fakat HSI2C `ADDR1` durumunda kalıp `STATUS_IO_TIMEOUT` ile bitmiştir. `0.1.0.24`, bu karşılaştırmada ilk eşleşmeyen basamak olan HSI2C platform hazırlığını kaynakta tamamlar; cihaz sonucu henüz yoktur. MAX77705 akışı ilk kimlik okumasının HSI2C taşıma katmanında dururken, Windows DWC3/xHCI yolunu bağımsız olarak HOST modunda başlatmıştır. Sonuç olarak xHCI çalışır görünür, fakat iki port da `CCS=0`, `PED=0`, `PLS=RxDetect` durumunda kalır ve USB aygıtı/HID oluşmaz.

Ana mimari fark şudur:

```text
Samsung Linux:
MAX IRQ -> CC=SOURCE -> VBUS/OTG + rol bildirimi -> MUIC USB_AP
        -> DWC3/PHY runtime resume -> xHCI -> USB enumerate -> HID

Mevcut Windows:
ACPI _DEP yalnızca BYN0004 sürücüsünün başlamasını bekler
        +-> BYN0002 hemen PHY/DWC3 HOST yapar -> USBXHCI çalışır
        `-> BYN0004 zamanlayıcısı HSI2C/MAX işlemlerini sonradan dener
            -> son flaşta ilk MAX kimlik okumasından önce/okuma sırasında durdu
```

## Adım adım yan yana karşılaştırma

| Aşama | Çalışan Samsung Linux akışı | Mevcut Windows kaynağının hedefi | Son Windows flaşında gözlenen | Sonuç |
|---|---|---|---|---|
| 1. Donanım sahipliği | `15d60000.hsi2c -> i2c-14 -> 14-0066 -> max77705-usbc`; TOP/USBC/charger alt adresleri tek MFD altında | `BYN0004`, HSI2C `0x15D60000`, USI `0x15C22000`, CMGP CMU ve ilgili pinctrl sayfalarını tek başına eşler; `0x66/0x25/0x69` adreslerini kendisi yönetir | `.23`te `BYN0004` yüklü ve Started; yalnız eski iki MMIO kaynağı atanmış | `.24` kaynak modeli Samsung platform bağımlılıklarını da kapsıyor |
| 2. HSI2C güç/saat hazırlığı | Linux pinctrl ile GPM12-0/GPM13-0'ı işlev 2 yapar; DT 200 MHz ister, clock framework mevcut hızı okur; QCH/gate hazırlanır ve 400 kHz timing hesaplanır | `.24`, aynı pinctrl ve CMGP3 alanlarını BYN0004'e verir; 200 MHz'i deterministik kurar, QCH'yi ve buna karşılık gelen FS timing alanlarını yazar, geri okur | `.23` ACPI'de yalnız HSI2C/USI sayfaları vardı; `PlatformPrepared`, CMGP ve GPM tanıları yoktu | İlk eşleşmeyen adım; `.24` kaynakta tamamladı, cihaz doğrulaması bekleniyor |
| 3. HSI2C mesajı | Samsung `exynos5_i2c_xfer` adres, yön, uzunluk ve RUN sırasını kurar; interrupt ile tamamlanır | Windows FIFO'yu hazırlar, adres/AUTO_CONF yazar, küçük write'ı FIFO'ya yükler, `MASTER_RUN` verir ve bounded polling yapar | `.23`: `I2C_CTL=0x8`, `AUTO=0x80000001`, adres `0x07019800` (master-id 7 + `0x66`), `XFER=0xA0006` (`ADDR1`), `INT=ERR=0`, `STATUS=0xC00000B5`, phase `0x20` | Yazılım transferi doğru adresle başlatıyor; fiziksel pin/saat/timing hazırlığı olmadığı için adres fazı tamamlanmıyor |
| 4. MAX kimlik doğrulama | Parent MFD TOP `0x66`, USBC `0x25`, charger `0x69` istemcilerini oluşturur | TOP ID/rev, USBC HW/FW ve charger registerları aynı sırayla okunur | PMIC ID/rev ve USBC rev alanları `0`; identity tamamlanmadı | Sonraki bütün MAX aşamaları fiilen çalışmadı |
| 5. Olay mekanizması | GPIO parent IRQ 318; alt CC/MUIC/PD/APCMD IRQ'ları nested IRQ olarak işlenir | MAX GPIO IRQ ACPI'de yok; read-to-clear status registerları PASSIVE one-shot timer ile 200 ms'de bir taranır, hatada 2 s sonra yeniden denenir | Timer devrede, ancak HSI2C başarısızlığı yüzünden CC olayı okunamadı | İşlevsel olarak daha geç ve daha zayıf; Linux IRQ semantiğiyle eşdeğer değil |
| 6. Probe/init opcodes | USBC/PD probe; interrupt maskeleri; CCCTRL; firmware komut kuyruğu | Charger unlock; `AUTOIBUS 57 01`; UIC/CC/PD/VDM maskeleri; `AUTO_VBUS 54 00`; `CCCTRL1 0c 1f` | `AutoIbusConfigured=0`, `UsbcMasksConfigured=0`, `CcControlConfigured=0` | Kod mevcut, son flaşta ulaşılmadı |
| 7. Fiziksel takılma ve CC | IRQ'da `CC1_ACTIVE`, `PLUG_ATTACHED`, `cc_SOURCE`; veri rolü DFP, güç rolü SRC olur | Poll ile `CC_STATUS0 & 7`; yalnız `CC_SOURCE=2` HOST/OTG'yi açabilir | `CcStatus0=0xff` veya geçersiz başlangıç değeri | Windows MAX'tan kablo takılmasını öğrenemedi |
| 8. VBUS/charger OTG | `vbus_turn_on_ctrl(1)`, CHGIN-OTG; charger mode `0x04 -> 0x0e`; VBUS HIGH bildirilir | Charger kilidi açılır, interrupt maskesi ayarlanır, `CNFG_00` mevcut moda göre `0x0e` benzeri OTG moda geçirilir ve read-back doğrulanır | `OtgHardwareActive=0`; charger alanları okunmamış | Port gücü `PP=1` olması konektörde gerçek VBUS üretildiğini kanıtlamaz |
| 9. MUIC D+/D- yönlendirme | OTG algılanır, switch `0x09`; opcode `06 09` ile USB yolu AP'ye verilir | `CONTROL1_WRITE 06 09`, ardından ek güvenlik olarak `CONTROL1_READ 05` ile `0x09` doğrulanır | `MuicPathRequested=0xff`, `MuicControl1=0xff`, `MuicPathVerified=0` | D+/D- fiziksel olarak AP/xHCI yoluna yönlendirilmedi |
| 10. Rol bildirimi ve DWC3 | `Turn On Host(DFP)` ve `USB_HOST_ATTACHED`; DWC3 role-switch work başlar | Windows'ta Linux notifier zinciri yok. `BYN0002` PrepareHardware içinde doğrudan sabit HOST seçer | BYN0002 ve BYN0003 Started | Windows bu aşamayı MAX sonucundan önce yaptı |
| 11. PHY/runtime resume | PHY connect, LDO on, runtime resume, UTMI init, tune, reset release | PMU izolasyonu kaldırılır; q-channel, PHY reset, UTMI, tune ve DWC3 core reset uygulanır; USB3 gizli tutulur | PHY registerları beklenen host değerlerinde | Host tarafının bu bölümü büyük ölçüde çalışıyor |
| 12. xHCI başlatma | DWC3 host olduktan sonra xHCI başlar, USB bus 1/2 kaydolur | `USBXHCI` inbox sürücüsü ACPI `BYN0003`'e bağlanır; parent önceden DWC3'ü HOST'a hazırlar | xHCI `RUN=1`, command ring/DCBAA/config kurulmuş | Controller çalışıyor; konektör yolu hazır değil |
| 13. Port bağlantısı | Yaklaşık 357 ms sonra full-speed aygıt görülür | USBXHCI'nin PORTSC değişimi görmesi beklenir | Portlar `CCS=0`, `PED=0`, `PLS=5/RxDetect`, transition yok | Elektriksel bağlantı xHCI'ye ulaşmıyor |
| 14. Enumeration | `new full-speed device`, `046d:c52e`, descriptor/config/interface işlemleri | Inbox hub/USB stack aynı standart enumeration'ı yapar | Root hub altında alıcı yok | Enumeration başlamadı; sorun HID sınıf sürücüsünde değil |
| 15. HID bind | Arayüz 1.0 `hid-generic` keyboard, 1.1 mouse | `USBHUB3 -> HIDUSB -> MOUHID/MOUCLASS` zinciri bağlanır | Hizmetler mevcut fakat “No devices were found” | Fare sorununun kökü HID değil, daha aşağıdaki Type-C/VBUS/MUIC yoludur |
| 16. Detach | VBUS kapatılır, roller geri alınır, MUIC açılır, DWC3 device/idle yoluna döner | Windows CC source dışına çıkınca önce OTG kapatır, sonra MUIC `OPEN=0x3f` doğrular | Attach hiç kurulamadığından test edilmedi | Kod var, cihazda kanıt yok |

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
| USB D+/D- yolunu AP'ye ver | `CONTROL1_WRITE: 06 09` | `06 09`, sonra `05` read-back | Eşleşiyor; Windows daha sıkı doğruluyor |
| Firmware AUTOIBUS | `57 01`, cevap `57` | `57 01`, APCMDRES ve cevap opcode kontrolü | Eşleşiyor |
| Otomatik VBUS'u kapat | Samsung init davranışı | `54 00` | Kaynakta var, canlı kanıtta ulaşılmadı |
| CC kontrolü | startup CC control | `0c 1f` | Kaynakta var, canlı kanıtta ulaşılmadı |
| Charger OTG | `CNFG_00 0x04 -> 0x0e` | Mevcut moda göre `0x0e/0x0f/...`; read-back ve rollback | Temel davranış eşleşiyor |
| Detach izolasyonu | VBUS off, switch open | OTG off, ardından MUIC `0x3f` | Tasarım olarak eşleşiyor |

## Kritik farklar ve önem sırası

### 1. Host başlangıcı MAX77705 READY durumuna bağlı değil

ACPI `_DEP`, `BYN0002`yi `BYN0004`e bağlar; ancak `BYN0004` D0Entry, MAX probe'unu beklemeden timer kurup başarı döndürür. Windows bağımlılığı tamamlanmış sayar ve `BYN0002` PrepareHardware doğrudan HOST rolünü seçer. Bu nedenle MAX identity/CC/MUIC/VBUS başarısız olsa da USBXHCI başlar.

Bu, Linux sırasındaki en büyük yapısal farktır. Doğru eşdeğerlik için HOST handoff, en azından şu koşullar doğrulanmadan yapılmamalıdır:

```text
IdentityReady
ChargerUnlocked
AutoIbusConfigured
UsbcMasksConfigured
AutoVbusDisabled
CcControlConfigured
CC_STATUS == SOURCE
MuicPathVerified == USB_AP
OtgHardwareActive
```

### 2. Son flaş MAX komut katmanına ulaşmadı

Son Windows kanıtında PMIC/USBC kimliği okunmamış, bütün opcode/mask/CC/MUIC/OTG bayrakları başlangıç değerlerinde kalmıştır. Dolayısıyla “hangi MAX77705 komutu eksik?” sorusunun o flaş için cevabı: önce komut değil, HSI2C transportu çalışmalıdır.

`0.1.0.23`, eski `BUS_IDLE` önkoşulunu kaldırıp retained/stale HSI2C state üzerinde adres/AUTO_CONF/RUN yazmıştır. Cihaz kanıtında `I2C_CTL=0x8`, `I2C_AUTO=0x80000001`, `I2C_REGADDR=I2C_TRYADDR=0x07019800` ve `I2C_XFER=0xA0006` görülür. `TRANS_STATUS` alt durum `6`, Samsung sürücüsündeki `HSI2C_MASTER_ST_ADDR1` durumudur. İki deneme de yazma fazında (`I2C_PHASE=0x20`) zaman aşımına uğramıştır; BUS_IDLE sorunu geride kalmış, kırılma pin/saat/timing sınırına taşınmıştır.

Tek aktif CPU açısından `.23` olumlu kanıt vermiştir: WinPE test sonuna ulaşmış, xHCI gözlemcisi yaklaşık 7.882 örnek toplamış ve controller `RUN=1` kalmıştır. Dolayısıyla artık görülen arıza CPU'nun busy-poll ile kilitlenmesi değil, HSI2C adres fazının fiziksel olarak tamamlanamamasıdır. `.24` platform hazırlığı yine PASSIVE timer işinde çalışır; CMU BUSY beklemeleri CPU'yu döndürmek yerine `KeDelayExecutionThread` ile bırakır ve her saat alanı için 10 ms ile sınırlıdır.

### 3. HSI2C platform hazırlığı `.24` ile kaynakta tamamlandı

`BYN0004` artık HSI2C ve USI yanında CMGP CMU `0x15c00000..0x15c07fff` ile CMGP pinctrl `0x15c30000..0x15c30fff` kaynaklarını da tek sahip olarak alır. Başlatma sırası şöyledir:

1. GPM12-0 ve GPM13-0 için pull/drive sıfırlanır, function `2` seçilir.
2. `DIV_CLK_USI_CMGP3` (`+0x1824`) oran `1` ile divide-by-two durumuna alınır.
3. `MUX_CLK_USI_CMGP3` (`+0x1020`) 400 MHz CMGP bus parent'a alınır; sonuç 200 MHz'tir.
4. `USI_CMGP3_QCH` (`+0x304c`) normal enable durumuna (`bits[2:0]=1`) alınır.
5. Linux'un `200 MHz / 400 kHz` hesabıyla FS1=`0x00f00000`, FS2=`0x000000e0`, FS3=`0x00210000` alanları controller resetinden sonra kurulur.
6. Pin, clock ve timing alanları geri okunur; doğrulama başarısızsa HSI2C transferine geçilmez ve `HsI2cFailurePhase=0x08` yayımlanır.

Linux'un gerçek IRQ 114 ile transfer tamamlaması hâlâ Windows'ta yoktur; bu, aşağıdaki olay/transfer mekanizması farkıdır ve ikinci basamağın platform hazırlığından ayrı tutulmuştur.

Samsung DT `default-clk=200000000` ister. Vendor CAL tablosunda USI_CMGP3 için yalnız 400 MHz normal seviye bulunduğundan Linux'taki `clk_set_rate(200 MHz)` çağrısı platform durumuna göre başarısız olabilir; sürücü yine `clk_get_rate()` ile etkin hızı okuyup timing hesaplar. Windows clock framework kullanmadığı için bu belirsizliği taşımıyor: 400 MHz parent + oran `1` ile DT'nin amaçladığı 200 MHz'i açıkça kuruyor ve timing'i bu hıza göre yazıyor.

### 4. IRQ yerine polling kullanılıyor

Linux'ta MAX parent IRQ 318 ve nested CC/MUIC/PD/APCMD interruptları olay sırasını taşır. Windows 200 ms polling yapar. Bu, hazır bir bus üzerinde attach'i sonunda yakalayabilir; fakat read-to-clear interruptlar, opcode yanıt sırası ve kısa geçişler Linux ile birebir değildir.

### 5. Windows attach sırası Linux ile tam aynı değil

Canlı Linux kaydında charger/VBUS ve host notifier çok erken çalışır; MUIC `06 09` hemen arkasından gelir. Windows güvenlik amacıyla önce MUIC `06 09` + `05` doğrulamasını, sonra charger OTG/VBUS açılmasını şart koşar. Bu sıra güvenli olabilir, fakat “Samsung ile birebir aynı” değildir ve donanımın VBUS olmadan MUIC read-back davranışı ayrıca doğrulanmalıdır.

### 6. Linux koordinasyon katmanları Windows'ta sadeleştirilmiş

Linux akışı charger, battery, MUIC, Type-C manager, USB notifier, runtime-PM ve role-switch bileşenlerini birlikte yürütür. Windows sürücüsü gerekli register/opcode çekirdeğini uygular ama bu framework koordinasyonunun tamamını modellemez. Sabit HOST hedefi için kabul edilebilir olabilir; attach/detach, hata kurtarma ve güç politikası açısından tam eşdeğer değildir.

## Son teşhis

Son Windows flaşında fareyi engelleyen sıra şöyledir:

1. BYN0004 sürücüsü PnP açısından Started olur.
2. Arka plan MAX işi HSI2C/identity sınırında başarısız olur.
3. Bu nedenle CC source görülmez, charger OTG açılmaz ve MUIC `USB_AP` yapılmaz.
4. Buna rağmen `_DEP` karşılanmış sayıldığı için BYN0002 DWC3/PHY'yi HOST yapar ve USBXHCI başlar.
5. xHCI elektriksel attach göremez; port `RxDetect`, `CCS=0` kalır.
6. USB descriptor okunmadığı için USBHUB3/HIDUSB/MOUHID aşamalarına hiç gelinmez.

Bu nedenle mevcut durumda öncelik HID, xHCI ring veya ek bir MAX opcode değildir. Önce `.24` ile `PLATFORM_OK=1`, `GPM12_CON/GPM13_CON` alt nibble `2`, `CMGP_DIV3` alt nibble `1`, `CMGP_MUX3` bit 0 `1`, `CMGP_QCH3` alt üç bit `1` ve TOP `0x66` kimlik okuması cihazda doğrulanmalı; ardından MAX READY ile BYN0002 HOST başlangıcı arasında gerçek bir çalışma zamanı kapısı kurulmalıdır.

## Kaynak konumları

- Linux canlı kanıtı: `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`, özellikle 309-375, 538-715, 747-789, 842-925 ve 978-1016.
- ACPI MAX ve USB bağımlılığı: `silicium-acpi-overlay/SoCs/Samsung/S5E9820/DSDT_minimal.dsl`, 254-329.
- Windows MAX durum makinesi: `windows/b2lusb/b2lmax77705.c`, 1145-1222.
- Windows MAX timer/PnP: `windows/b2lusb/b2lmax77705.c`, 1337-1540.
- Windows HSI2C platform ve taşıma katmanı: `windows/b2lusb/b2lmax77705_hw.c`, 162-727.
- Windows sabit HOST başlangıcı: `windows/b2lusb/b2lusb.c`, 230-293 ve 398-420.
- Windows PHY/DWC3 hazırlığı: `windows/b2lusb/b2lusb_hw.c`, 422-665.
