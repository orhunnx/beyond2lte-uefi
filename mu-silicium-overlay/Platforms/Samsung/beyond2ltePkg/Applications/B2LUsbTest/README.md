# B2LUsbTest EFI Shell USB tanısı

`B2LUSB.EFI`, beyond2lte USB yolunun aşamalı EFI tanısıdır. R9, eski pasif
harita sayfalarının ardından tek ekranda iki saniyede bir PMU, DWC3 device/OTG,
xHCI portları ve doğrulanmış ana DRD PHY kayıtlarını yeniler. Önce PC veri
kablosu takılıyken, sonra aynı boot içinde kablo çıkarıldıktan sonra bu ekranın
fotoğrafı alınır. Uygulama DWC3/xHCI başlatmaz; PHY, rol veya PMU yazmaz ve USB
transferi yapmaz. R8 MAX77705 yardımcı kaynağı ağaçta korunur fakat R9
uygulamasına bağlanmaz ve çalıştırılmaz.

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

Bu turun başarı ölçütü USB enumeration değildir. İki fotoğrafta `ORNEK`
sayısının farklı olması ve kablo çıkarıldığında hangi blokların `DELTA ANLIK`
ve `DELTA BIRIKIM` bayrağı ürettiğinin görülmesi yeterlidir. Hiçbir blok
değişmezse sorun DWC3/xHCI'den önceki attach/role handoff sınırındadır; OTG
`VBUS/BSES` değişip device `RUN=0/HLT=1` kalırsa sonraki kontrollü adım DWC3
device başlatmasıdır.
