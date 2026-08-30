# B2LUsbTest EFI Shell USB tanısı

`B2LUSB.EFI`, beyond2lte USB yolunun ilk ve yalnız salt-okuma tanısıdır.
Denetleyiciyi başlatmaz; register yazmaz, reset atmaz, MAX77705'e I2C komutu
göndermez ve USB transferi başlatmaz.

Uygulama yeni MMIO pencerelerini tek tek ve yalnız kullanıcı tuşuyla açar:

1. PMU içindeki FSYS0A power-domain ve USB PHY kontrol değerleri,
2. DWC3 kimliği/globalleri ile xHCI capability, operational ve port durumu,
3. DWC3 device/OTG rol registerleri,
4. USB DRD PHY ana penceresinin ilk 64 baytı,
5. combo PHY PCS penceresinin ilk 64 baytı,
6. combo PHY PMA penceresinin ilk 64 baytı.

Her sayfa sonraki erişimden **önce** fotoğraflanmalıdır. Bir pencere saat veya
güç kapalı olduğu için sistemi kilitlerse son görülen sayfa erişim sınırını
belirler. `Esc` veya `Q` sonraki pencereyi okumadan uygulamadan çıkar.

Derleme ve paketleme:

```bash
bash yap.sh --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Odin'de `out/system-esp.tar` yalnız AP alanından, **Re-Partition kapalı**
olarak flashlanır. Uygulama SYSTEM ESP kökünde `B2LUSB.EFI` adıyla bulunur ve
`startup.nsh` dosya mevcutsa onu Windows'tan önce çalıştırır.

Bu turun başarı ölçütü bir USB aygıtının enumerate olması değildir. Beklenen
çıktı; tutarlı DWC3 `GSNPSID`, geçerli xHCI `CAPLENGTH/HCIVERSION`, makul port
sayısı, controller/PHY reset-suspend bitleri ve kablo takılı/takılı değilken
`PORTSC` farkıdır. Yazmalı host init ancak bu ham değerler cihazda görüldükten
sonra eklenecektir.
