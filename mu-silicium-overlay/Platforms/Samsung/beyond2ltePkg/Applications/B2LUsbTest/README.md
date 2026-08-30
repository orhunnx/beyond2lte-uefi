# B2LUsbTest EFI Shell USB tanısı

`B2LUSB.EFI`, beyond2lte USB yolunun ilk ve yalnız salt-okuma tanısıdır.
Denetleyiciyi başlatmaz; register yazmaz, reset atmaz, MAX77705'e I2C komutu
göndermez ve USB transferi başlatmaz.

Uygulama yeni MMIO pencerelerini tek tek ve yalnız kullanıcı tuşuyla açar:

1. FSYS0A power-domain içindeki doğrulanmış `CONFIG/STATUS/OPTION` değerleri,
2. PMU içindeki USB PHY kontrol değerleri,
3. DWC3 kimliği/globalleri ile xHCI capability, operational ve port durumu,
4. DWC3 device/OTG rol registerleri,
5. USB DRD PHY ana penceresinin doğrulanmış ilk 64 baytı,
6. combo PHY PCS için önceki abort sonucu; MMIO okuması yok,
7. combo PHY PMA için önceki abort sonucu; MMIO okuması yok.

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

Bu turun başarı ölçütü bir USB aygıtının enumerate olması değildir. Beklenen
çıktı; tutarlı DWC3 `GSNPSID`, geçerli xHCI `CAPLENGTH/HCIVERSION`, makul port
sayısı, controller/PHY reset-suspend bitleri ve kablo takılı/takılı değilken
`PORTSC` farkıdır. Yazmalı host init ancak bu ham değerler cihazda görüldükten
sonra eklenecektir.
