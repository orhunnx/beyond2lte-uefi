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

Sürücü UEFI'de cihaz üzerinde doğrulanmış parser ile aynı 12-bit X/Y formatını
kullanır. Paralel HID raporu en fazla 10 eşzamanlı `sec_ts` temasını, kararlı
Contact Identifier değerleriyle aynı pakette taşır. Parmak kalkışı son koordinat
korunarak Tip Switch=0 ile bir kez bildirilir. Feature raporu maksimum temas
sayısını 10 olarak yayınlar.

HSI2C olay kuyruğu 20 ms aralıkla passive-level, tek-seferlik KMDF timer ile
okunur. Passive timer periyodik yapılandırılmaz; callback başarılı kaldığı
sürece kendisini yeniden kurar.

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
bash yap.sh --touch-test --acpi --clean

# Mevcut boot.wim içine ARM64 SYS/INF/CAT ekle
bash tools_b2ltouch.sh

# WinPE ESP'sini SYSTEM bölümü için paketle
bash cache_esp.sh --win --system --baseline

# Son paketi listele
tar -tf out/beyond2lte-touch-diag.tar
```

`tools_b2ltouch.sh`, PowerShell'i WSL'den tek satırda çağırır. PowerShell
kaynakları Windows TEMP'e kopyalar, yerel `cl.exe/link.exe` ile ARM64 KMDF 1.15
ve `VhfKm.lib` kullanarak sürücüyü; `/MT` ile de bağımsız ARM64 multi-touch test
uygulamasını derler. Ardından WSL tarafı mevcut beyond2lte test sertifikasıyla
SYS/CAT/EXE paketini imzalar, WinPE WIM'e ekler ve geri okuyup hash doğrulaması
yapar.

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

`startnet.cmd`, `wpeinit` sonrasında WinPE'de dosyası bulunduğu halde eksik olan
Microsoft `vhf` kernel-service kaydını mevcut `reg.exe` ile oturumluk oluşturur, ardından
`b2ltouch.inf` paketini kurar ve PnP taraması yapar. Başarılı durumda Device Manager/PnP görünümünde kaynak aygıt
`ACPI\BYN0001`, onun VHF çocuğu ise HID uyumlu dokunmatik ekran olarak görünür.
Kurulum çıktısı `X:\B2LTOUCH-PNP.TXT` dosyasındadır. Test sonunda WinPE/Windows
içindeki yerel dokunmatik klavye (`TabTip.exe`), o yoksa yerel ekran klavyesi
(`osk.exe`) açılır; paket ayrı bir klavye uygulaması içermez. Çıplak WinPE
imajında bu iki Windows bileşeni de yoksa `tools_b2ltouch.sh`, aynı build'deki
ARM64 Windows ISO/install.wim içinden Microsoft'un klasik `osk.exe` dosyasını,
MUI kaynağını ve WinPE'de eksik olan PE bağımlılık kapanımını alır. Mimariler
ve Windows major/minor sürümü eşleşmelidir. Aynı build tercih edilir; daha eski
native OSK daha yeni WinPE üzerinde kabul edilir, fakat daha yeni OSK'nin eski
WinPE'ye eklenmesi reddedilir. Kaynak otomatik
bulunamazsa `B2L_WINDOWS_ARM64_SOURCE=/mnt/c/.../Windows_Arm64.iso` ayarlanır;
özel bir klavye derlenmez veya paketlenmez.

Multi-touch tanısı yeniden gerekirse konsoldan `%SystemRoot%\B2LTOUCH\b2lmt.exe`
çalıştırılır; native OSK'nin üstünü kapatmaması için artık otomatik açılmaz.
Uygulama Windows'un gerçek
`WM_POINTER`/`WM_TOUCH` olaylarını dinler; aktif parmak sayısını, her temasın
Windows pointer kimliğini ve piksel X/Y konumunu canlı gösterir. Yalnız imleç
emülasyonu gelirse üstte `MOUSE ONLY` yazar. İki parmakla aynı anda dokunulduğunda
sayaç `2` ve iki ayrı ID görünüyorsa multi-touch yolu uçtan uca doğrulanmıştır.
