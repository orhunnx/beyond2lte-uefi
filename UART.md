# UART jig — nasıl yapılır, ne kazandırır

Bu belge "kablo nasıl yapılır" sorusuna cevap veriyor. Karar senin; ben
donanım almanı ya da lehim yapmanı gerektiren hiçbir şeyi zorunlu tutmuyorum.

## Neden

Şu an her bilgi kırıntısı bir flaş döngüsüne mal oluyor ve tek ölçüm aletimiz
"ekranda ne göründü". UART, `ExitBootServices` sonrasında da yaşayan tek
kanal — Windows çekirdeğinin ve `winload`'ın söylediği her şeyi metin olarak
okuyabiliriz.

## Nasıl çalışıyor

Galaxy S10'da USB-C konnektöründeki **CC pinine bir direnç** takınca MUIC
(USB anahtarlama yongası) fabrika moduna geçiyor ve **AP'nin UART'ını
D+/D- pinlerine yönlendiriyor**. Bu bir yazılım ayarı değil, donanım
seviyesinde direnç algılama — yani sürücü yazmadan çalışıyor.

Stok DTB bunu doğruluyor:

```
muic,support-list = "TA" "USB" "CDP" "JIG UART OFF" "JIG UART OFF/VB"
                    "JIG UART ON" "JIG UART ON/VB" ...
```

## Parçalar

| Parça | Not |
|---|---|
| USB-C **erkek** breakout kartı | D+, D-, CC, GND pinlerini dışarı veren tip |
| Direnç | **620 kΩ** — 619 kΩ E96 serisi ve zor bulunur, 620 kΩ standart E24 değeri ve her yerde var. MUIC bir aralık algılıyor, tam değer şart değil. |
| USB-TTL dönüştürücü | FTDI FT232 / CP2102 / CH340. **3.3 V olmalı**, 5 V değil. |
| Jumper kablo | 3-4 adet |

## Bağlantı

```
USB-C breakout                USB-TTL adaptör
--------------                ---------------
CC1 ----[620 kΩ]---- GND      (jig direnci; adaptöre gitmiyor)
D+  ------------------------- RX
D-  ------------------------- TX
GND ------------------------- GND
```

`D+`/`D-` ile `RX`/`TX` ters olabilir; çalışmazsa ikisini değiştir, zarar
vermez.

Ayarlar: **115200 baud, 8N1, akış denetimi yok.**

Windows'ta PuTTY, Linux'ta `screen /dev/ttyUSB0 115200` ya da
`picocom -b 115200 /dev/ttyUSB0`.

## Dikkat

- Direnç **CC ile GND arasına**, adaptöre değil. Yanlış bağlanırsa jig modu
  açılmaz (zarar vermez, sadece çalışmaz).
- USB-TTL adaptörün 3.3 V olduğundan emin ol. 5 V, SoC pinlerine zarar verir.
- Telefonu bilgisayara aynı anda başka bir USB kablosuyla bağlama.

## Yazılım tarafı — bu kadarla bitmiyor

Kablo tek başına yetmiyor, üç iş daha var:

1. **sboot'un logları hemen okunur.** Kablo takılır takılmaz Samsung'un kendi
   bootloader çıktısını görmelisin. Bu, jig'in çalıştığının kanıtı olur.

2. **Bizim UEFI'nin yazması için `SerialPortLib`.** Mu-Silicium'da Exynos UART
   sürücüsü yok. Yazmak küçük iş: `uart@10440000`, sboot zaten baud/clock'u
   kurmuş durumda, yalnızca `UTRSTAT`'ı yoklayıp `UTXH`'a bayt basmak yeterli
   (~50 satır). Adresler `HARDWARE.md`'de.

3. **Windows'un yazması için DBG2 tablosu.** Windows seri portu ACPI'nin DBG2
   tablosundan öğreniyor — bizde yok (bilerek atlamıştık). Eklenmesi gerekir,
   ardından BCD'de:

   ```
   bcdedit /set {default} bootdebug on
   bcdedit /dbgsettings serial debugport:1 baudrate:115200
   ```

Yani sıra: jig → sboot logu (doğrulama) → SerialPortLib (UEFI logu) → DBG2
(Windows logu).

## Bilinen risk

DTB'de `s5100_evt1_dual_2cp_uart_sel` diye bir GPIO var, yani AP/CP (modem)
UART seçicisi mevcut. Jig takınca modem UART'ına düşersek bir GPIO çevirmek
gerekecek. İlk denemede anlaşılır: sboot logu yerine modem çıktısı gelirse
sebep budur.
