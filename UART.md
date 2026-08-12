# UART jig — nasıl yapılır, ne kazandırır

Bu belge "kablo nasıl yapılır" sorusuna cevap veriyor. Karar senin; ben
donanım almanı ya da lehim yapmanı gerektiren hiçbir şeyi zorunlu tutmuyorum.

## Neden

Şu an her bilgi kırıntısı bir flaş döngüsüne mal oluyor ve tek ölçüm aletimiz
"ekranda ne göründü". UART, `ExitBootServices` sonrasında da yaşayan tek
kanal — Windows çekirdeğinin ve `winload`'ın söylediği her şeyi metin olarak
okuyabiliriz.

## Nasıl çalışıyor

Galaxy S10 ailesindeki MAX77705 MUIC, uygun bir fabrikasyon-jig algıladığında
**AP'nin UART'ını USB-C tarafındaki hatlara yönlendirebilir**. Bu bir yazılım
ayarı değil, donanım seviyesinde direnç algılamasıdır.

> **S10+ için bağlantı noktası henüz doğrulanmadı.** Stock DTB'deki `JIG UART`
> listesi özelliğin yazılımda desteklendiğini kanıtlar; direnç kesin olarak
> `CC1`'e bağlanır demek değildir. Başka bir Samsung Type-C incelemesinde
> 619 kΩ'un CC'ye bağlanması algılanmamış, farklı bir konnektör hattı
> kullanılmıştır. Bu nedenle doğrulanmış bir SM-G975F jig şeması olmadan
> CC/SBU/D+/D- pinlerine direnç ya da ESP bağlama.

Stok DTB bunu doğruluyor:

```
muic,support-list = "TA" "USB" "CDP" "JIG UART OFF" "JIG UART OFF/VB"
                    "JIG UART ON" "JIG UART ON/VB" ...
```

## Parçalar

| Parça | Not |
|---|---|
| USB-C **erkek** breakout kartı | D+, D-, CC, GND pinlerini dışarı veren tip |
| Direnç | **620 kΩ** — 619 kΩ E96 serisi ve zor bulunur, 620 kΩ standart E24 değeri ve her yerde var. MUIC ailesinin 619 kΩ `JIG UART ON` eşiğini kullandığı doğrulanmış olsa da, S10+ konnektöründeki bağlanacağı hat ayrıca doğrulanmalıdır. |
| Seviye çevirici | Telefon hattı 1.8 V da olabilir; iki yön için 1.8 V↔3.3 V çevirici gerekir. 5 V USB-TTL kullanılamaz. |
| Jumper kablo | 3-4 adet |

## ESP32-C6 ve yalnız dirençle çözüm

ESP32-C6 geliştirme kartı 3.3 V'ta çalışırken dijital giriş için en az
`0.75 × VDD = 2.475 V` ister. Telefonun UART çıkışı 1.8 V ise C6 bunu güvenilir
bir lojik 1 olarak okuyamaz. Direnç bölücü yalnızca 3.3 V'u 1.8 V'a **düşürür**;
1.8 V'u C6'nın eşiğine **yükseltemez**. Bu yüzden iki dirençle güvenli ve
çift yönlü UART kurulamaz.

- ESP32 TX → telefon RX için 10 kΩ / 12 kΩ bölücü yaklaşık 1.8 V üretir,
  fakat yalnızca telefon girişinin gerçekten 1.8 V olduğu ölçülürse anlamlıdır.
- Telefon TX → ESP32 RX yönü için direnç yeterli değildir; aktif bir yükseltici
  gerekir.
- Sadece log okumak için bile telefon TX seviyesini ve Type-C jig yönlendirmesini
  ölçmeden C6 RX'e bağlamak önerilmez.

Bu nedenle en küçük güvenli ek donanım iki kanallı 1.8 V↔3.3 V seviye çevirici
modülüdür (BSS138 türü veya uygun bir lojik çevirici). UART için önce yalnız
telefon TX → ESP RX yönünü kurmak yeterlidir; ESP TX bağlı olmadan S-Boot/UEFI
çıktısı yakalanabilir.

## Bağlantı (yalnız S10+ jig hattı doğrulandıktan sonra)

```
USB-C breakout                USB-TTL adaptör
--------------                ---------------
doğrulanmış jig hattı --[620 kΩ]-- GND  (adaptöre gitmez)
telefon TX --[seviye çevirici]-- ESP RX
telefon RX --[seviye çevirici]-- ESP TX  (gerekirse; ilk aşamada boş bırak)
GND ------------------------- GND
```

Hatların D+/D−, SBU veya başka bir MUIC yolu olup olmadığı cihaz-özel olarak
doğrulanmalıdır; rastgele yer değiştirmek güvenli bir test değildir.

Ayarlar: **115200 baud, 8N1, akış denetimi yok.**

Windows'ta PuTTY, Linux'ta `screen /dev/ttyUSB0 115200` ya da
`picocom -b 115200 /dev/ttyUSB0`.

## Dikkat

- S10+ için doğrulanmış konektör hattı olmadan 620 kΩ'u CC'ye bağlama.
- Telefon hattına 3.3 V veya 5 V doğrudan sürme. Seviye bilinmiyorsa önce
  ölçüm ya da aktif seviye çevirici gerekir.
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
