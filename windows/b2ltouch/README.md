# B2LTOUCH Windows ARM64 touchscreen driver

`b2ltouch.sys`, Galaxy S10+ üzerindeki Samsung `sec_ts` denetleyicisini
Windows dokunmatik yığınına bağlayan ARM64 KMDF function driver'dır.

## Neden HIDI2C.sys değil?

Windows'un yerleşik `HIDI2C.sys` sürücüsü, aygıtın HID-over-I²C descriptor ve
register protokolünü doğrudan sunmasını ister. beyond2lte paneli bunun yerine
Samsung `sec_ts` komutlarını (`0x60/0x61` olay kuyruğu) kullanır. Bu sürücü
Microsoft'un Virtual HID Framework (VHF) modelini kullanır: fiziksel taşıma ve
`sec_ts` çözümleme burada yapılır, Windows'a standart Touch Screen HID top-level
collection verilir.

İlk sürüm tek temaslıdır ve UEFI'de cihaz üzerinde doğrulanmış parser ile aynı
12-bit X/Y formatını kullanır. HID raporu Windows'un zorunlu Tip Switch, Contact
Identifier, X, Y ve Contact Count alanlarını; feature raporu da maksimum temas
sayısını içerir.

## Donanım ve güvenlik sınırı

ACPI `TSP0` yalnız şu iki 4 KiB device-MMIO kaynağını verir:

- HSI2C10: `0x104B0000..0x104B0FFF`
- USI mode: `0x10421000..0x10421FFF`

Sürücü bu iki adresi ve tam `0x1000` uzunluğu translated `_CRS` kaynaklarında
aynen görmeden başlamaz. Sabit fiziksel adresi doğrudan map etmez; kısıtlı DRAM
alanı, başka HSI2C denetleyicisi, GPIO, PMIC ve dokunmatik firmware güncelleme
yolu yoktur. Transferler 100 ms ile sınırlıdır ve D0 çıkışında denetleyici
susturulur.

Dokunmatik güç rayları ve pinmux UEFI `--touch` profilinde açılır. Windows
sürücüsü bunları yeniden programlamaz veya kapatmaz.

## WSL üzerinden derleme ve WinPE'ye ekleme

Windows'ta WDK, Visual Studio C++ ARM64 build tools ve Windows SDK Signing Tools
kurulu olmalıdır. Komutlar WSL terminalinde çalıştırılır:

```bash
cd ~/beyond2lte-uefi-8549

# TSP0 ACPI tanımı + dokunmatik güç/pin hazırlığı olan BOOT
bash yap.sh --touch --acpi --clean

# Mevcut boot.wim içine ARM64 SYS/INF/CAT ekle
bash tools_b2ltouch.sh

# WinPE ESP'sini SYSTEM bölümü için paketle
bash cache_esp.sh --win --system
```

`tools_b2ltouch.sh`, PowerShell'i WSL'den tek satırda çağırır. PowerShell
kaynakları Windows TEMP'e kopyalar, yerel `cl.exe/link.exe` ile ARM64 KMDF 1.15
ve `VhfKm.lib` kullanarak derler. Ardından WSL tarafı mevcut beyond2lte test
sertifikasıyla SYS/CAT paketini imzalar, WinPE WIM'e ekler ve geri okuyup hash
doğrulaması yapar.

PowerShell derlemesini tek başına çalıştırmak gerekirse Windows PowerShell'de
satır sonuna `\` koymadan tek satır kullan:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi-8549\windows\b2ltouch\build-b2ltouch.ps1"
```

Sonrasında WSL'de hazır SYS'yi paketlemek için:

```bash
B2L_SKIP_MSVC_BUILD=1 bash tools_b2ltouch.sh
```

## WinPE'de beklenen sonuç

`startnet.cmd`, `wpeinit` sonrasında `b2ltouch.inf` paketini kurar ve PnP
taraması yapar. Başarılı durumda Device Manager/PnP görünümünde kaynak aygıt
`ACPI\BYN0001`, onun VHF çocuğu ise HID uyumlu dokunmatik ekran olarak görünür.
Kurulum çıktısı `X:\B2LTOUCH-PNP.TXT` dosyasındadır.
