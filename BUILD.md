# beyond2lte UEFI — derleme ve flash

Bu adımlar Mu-Silicium'un gerçek `build_uefi.py` script'ine göre doğrulandı.
İş bölümü ORTAM.md'ye uygun: **derleme WSL'de, flash Windows'ta** (WSL'de USB yok).

---

## 1. WSL — kaynağı al

```bash
cd ~
git clone --recursive https://github.com/Project-Silicium/Mu-Silicium.git
cd Mu-Silicium
```

`--recursive` **şart** — `Mu_Basecore`, `Common/Mu`, `Silicium-ACPI` submodule.
Toplam ~5 GB. WSL'in kendi ext4'ünde olsun, `/mnt/c/` üzerinde **değil**.

## 2. Bağımlılıklar

```bash
./setup_env.sh -p apt
python3 -m pip install --upgrade -r pip-requirements.txt
```

`setup_env.sh` clang/llvm dahil her şeyi apt'ten kuruyor. Elle LLVM indirmene gerek yok.

## 3. beyond2lte dosyalarını içeri kopyala

Bu klasördeki `mu-silicium-overlay/`, Mu-Silicium'un dizin yapısını birebir
yansıtıyor. Tek komutla yerleşiyor:

```bash
cp -r /mnt/c/Users/orhun/OneDrive/Masaüstü/UEFI_beyond2lte/mu-silicium-overlay/* ~/Mu-Silicium/
```

Yerleşen üç şey:

| Dosya | Ne |
|---|---|
| `Platforms/Samsung/beyond2ltePkg/` | Platform tanımı (dsc/fdf/memory map/keypad) |
| `Resources/Configs/beyond2lte.toml` | FD adresi, boot image ayarları |
| `Resources/DTBs/beyond2lte.dtb` | Cihaz DTB'si — boot image'a ekleniyor |
| `Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.inf` | **Upstream dosyanın üzerine yazıyor** — aşağıya bak |

> **UfsDxe.inf neden değişti:** `UfsDxe.c` `<Library/TimerLib.h>` include edip
> `MicroSecondDelay` çağırıyor, ama `.inf` `TimerLib`'i `[LibraryClasses]`'ta
> bildirmiyor. Bu upstream hatası. 9830/9810'da fark edilmiyor çünkü onların
> `PlatformUfsLib`'i `TimerLib`'i dolaylı getiriyor; 9820'de o kütüphane yok,
> Null'a düşüyoruz ve link `undefined symbol: MicroSecondDelay` ile patlıyor.
> Tek satır ekledik. Upstream'e PR açmaya değer.
>
> Mu-Silicium'u güncellersen (`-u`) bu dosya geri gelebilir — overlay'i
> yeniden kopyala.

> `beyond2lte.dtb`'yi kendi stock firmware'inden ürettim: `dt.img` base DTB'si +
> `dtbo.img` rev 26 overlay'i `fdtoverlay` ile birleştirildi, `model` özelliği eklendi.
> Yani sboot'un beklediği gerçek beyond2lte tanımı.

Merkezi bir cihaz listesi **yok** — `build_uefi.py` `Platforms/` altında
`<isim>Pkg` klasörünü glob'layıp `Resources/Configs/<isim>.toml`'u okuyor. Kayıt gerekmez.

## 4. Derle

Proje sarmalayıcısını WSL'deki proje dizininden kullan. Bilinen çalışan normal
boot ve deneysel UEFI dokunmatik derlemeleri birbirinden ayrıdır:

```bash
cd ~/beyond2lte-uefi-8549

# Bilinen çalışan profil (dokunmatik kapalı)
bash yap.sh --acpi --clean

# Deneysel UEFI dokunmatik profili
bash yap.sh --touch --acpi --clean

# Windows'tan once kareli fabrika tipi dokunmatik testi
bash yap.sh --touch-test --acpi --clean

# Salt-okuma USB bring-up + Windows dokunmatik hazırlığını birlikte koru
bash yap.sh --touch --usb-test --clean
```

`--touch`, `--acpi` seçeneğini zaten zorunlu kılar; komutta ayrıca yazılması
niyeti görünür tutar. Dokunmatik denemesinde yalnız `out/uefi-boot.tar`
değişir ve Odin'de AP alanından, **Re-Partition kapalıyken** flashlanır.
BCD, WIM, SYSTEM ve CACHE yeniden hazırlanmaz veya flashlanmaz. Geri dönüş
için `bash yap.sh --acpi --clean` çalıştırıp oluşan BOOT tar'ını flashla.

Bu sürücü UEFI `AbsolutePointer` sağlar. `ExitBootServices` sırasında I²C'yi
susturur; dolayısıyla Windows açıldıktan sonraki dokunmatik için ayrıca bir
Windows sürücüsü gerekir.

### EFI Shell USB salt-okuma tanısı

USB host veya Windows sürücüsü eklenmeden önce DWC3/xHCI/PHY handoff durumu
ayrı bir EFI Shell uygulamasıyla ölçülür:

```bash
bash yap.sh --touch --usb-test --clean
bash tools_b2ltouch.sh
bash cache_esp.sh --win --system --baseline
```

`B2LUSB.EFI` SYSTEM ESP'ye konur ve `startup.nsh` tarafından Windows'tan önce
çalıştırılır. Her sayfayı fotoğrafladıktan sonra bir tuşla sonraki MMIO
penceresine geçilir; `Esc` veya `Q` yeni pencereyi okumadan çıkar. Uygulama
register yazmaz, reset/clock/regulator işlemi yapmaz, MAX77705'e I²C komutu
göndermez ve USB transferi başlatmaz. R3'te platform memory map de değiştiği
için `cache_esp.sh`, BOOT ve SYSTEM'i `out/beyond2lte-usb-diag.tar` içinde
birleştirir. Odin AP alanına yalnız bu tek arşiv verilir; Re-Partition kapalı
kalır.

Bu üç komut WSL'de çalıştırılır. Birinci komuttaki `--touch`, BOOT içinde
B2LTouchDxe, HSI2C hazırlığı ve TSP0 ACPI tanımını korurken `--usb-test` ayrı
salt-okuma uygulamasını ekler. İkinci komut ARM64 `b2ltouch.sys` paketini
WinPE'ye yeniden yerleştirir. USB tanı dosyası varken `cache_esp.sh`, yerel
`b2ltouch.sys` yoksa veya WIM manifestindeki SYS özeti onunla aynı değilse
flash arşivi üretmez.

Sayfalar sırasıyla FSYS0A'nın doğrulanmış `CONFIG/STATUS/OPTION` alanlarını,
PMU USB kontrollerini, DWC3+xHCI, device/OTG rolü, USB DRD PHY, combo PCS ve
combo PMA değerlerini gösterir. R1 cihaz testinde `FSYS0A + 0x14` erişimi
synchronous external abort verdiğinden R2, `+0x10/+0x14` duration alanlarını
okumaz ve PMU USB kontrollerini ayrı bir kullanıcı-onaylı aşamada açar. Bir
sonraki yazmalı host-init turu ancak bu değerler cihazda fotoğraflanıp DWC3
kimliği ve xHCI capability
alanları doğrulandıktan sonra hazırlanır.

R2 cihaz ölçümünde FSYS0A `CONFIG/STATUS=1`, `OPTION=0x10`; PMU
`USBDP/USBPHY=0` bulundu. İlk DWC3 okuması `0x10C0C120` üzerinde level-2
translation fault verdi. R3, merged stok DT'de `okay` olan DWC3 ve üç PHY
penceresini mevcut güvenli `MemoryMapLib` device-MMIO mekanizmasıyla eşler;
DRAM ve reserved-memory descriptor'larına dokunmaz.

R3 ölçümünde eşleme çalıştı: `GSNPSID=0x33313130` bir DWC_usb31 çekirdeğidir;
xHCI 1.10 capability geçerli, denetleyici `DEVICE` rolde, host halted fakat
hatasız ve iki port powered/bağlantısızdır. R4, Linux'un doğruladığı
`DWC31_IP=0x3331` kimliğini kabul eder, ayrı `VER_NUMBER/VER_TYPE` alanlarını
okur ve kalan role/PHY salt-okuma aşamalarına devam eder.

R4 ölçümü çekirdeği `VER_NUMBER=0x3137302A` ile DWC_usb31 1.70a olarak
doğruladı. Device registerleri okunabildi; OTG registerleri sıfır, ana USB DRD
PHY'nin ilk `0x40` baytı erişilebilir bulundu. Combo PCS'nin `0x10AE000C`
adresi ise eşlenmiş device-MMIO içinden synchronous external abort verdi. Bu,
sayfa tablosu eksikliğinden farklı olarak erişim anındaki bus/power/clock veya
izolasyon durumuna işaret eder; sebep henüz tek başına kanıtlanmış değildir.
R5, bilinen `PCS +0x0C` erişimini atlar, PCS'de yalnız `+0/+4/+8` ve PMA'da
yalnız ilk dört 32-bit kaydı okur. Her adres değerden önce ayrı yazdırılır ve
her MMIO çağrısı ayrı tam ifadedir; yeni bir abortun kesin kaydı ve sırası
fotoğraftan belirlenebilir.

`--touch-test` önce Android DT'deki 47 HSI2C denetleyicisinin adres ve açık/
kapalı haritasını, ardından DT'de ilan edilmiş slave adlarını gösterir. Kamera,
EEPROM, PMIC ve şarj cihazlarına kör aktif tarama göndermez. Yalnız firmware'in
zaten map edip başlattığı HSI2C10 için canlı SCL/SDA, hata, saat ve timing
register'ları okunur. REV5 ayrıca DT'deki dokunmatik beslemelerini mevcut PMIC
protokolüyle açma sonucunu, ilk boşta SCL/SDA seviyelerini ve Samsung
touch-function komutunun sonucunu gösterir. Başka bir regülatöre dokunulmaz.
Diğer tanı ekranları probe sonucunu, son komutu, son ham 8 baytlık olayı, son 32
yanıt baytını ve aktarım/olay/hata sayaçlarını gösterir. Kareli testten sonra aynı
canlı tanılar yeniden açılır. Her tanı sayfası tuşla veya 20 saniyede, kareli
test ise tuşla veya 45 saniyede kapanır ve Windows yolu devam eder.
REV5 sürücüsü 32'den büyük olay kuyruğunu bozuk sayıp temizlemez; olayları
32'şer adet güvenli turlarla tüketir ve dokunmatik polling'ini açık tutar.

### Windows ARM64 dokunmatik

UEFI kareli testinde doğrulanan `sec_ts` yolu Windows'a standart KMDF/VHF HID
kaynağı olarak aktarılmıştır. Panel yerel HID-over-I²C konuşmadığı için inbox
`HIDI2C.sys` doğrudan kullanılamaz; `b2ltouch.sys` Samsung olaylarını Windows
Touch Screen HID raporlarına dönüştürür. ACPI yalnız doğrulanmış HSI2C10 ve USI
4 KiB device-MMIO sayfalarını yayınlar; kısıtlı bellek, PMIC ve diğer I²C
aygıtları bu sürücünün kapsamı dışındadır.

WSL'de Windows WDK/Visual Studio ARM64 araçlarını kullanarak tam sıra:

```bash
cd ~/beyond2lte-uefi-8549
bash yap.sh --touch-test --acpi --clean
bash tools_b2ltouch.sh
bash cache_esp.sh --win --system --baseline
tar -tf out/beyond2lte-touch-diag.tar
```

Bu dört komut cihazda doğrulanan günlük yoldur. İlk komut BOOT/ACPI ve UEFI
dokunmatik hazırlığını, ikinci komut Windows ARM64 SYS/CAT/INF ile tanı
uygulamasını, üçüncü komut SYSTEM imajı ve tek Odin AP arşivini üretir. Son
komut arşivde `boot.img` ile `system.img` bulunduğunu gözle doğrular.

ADK `boot.wim` sıfırdan kurulacaksa günlük sıranın önüne yalnız bir kez şunu
ekle:

```bash
bash tools_winpe_adk.sh "/mnt/c/Program Files (x86)/Windows Kits/10/Assessment and Deployment Kit/Windows Preinstallation Environment/arm64/Media"
bash tools_b2lufs.sh       # UFS paketi de bu yeni WIM'e girecekse
```

Yalnız dokunmatik C kaynakları değiştiyse UEFI'yi yeniden derlemek gerekmez;
mevcut `out/uefi-boot.tar` korunarak `bash tools_b2ltouch.sh` ve paketleme
adımları çalıştırılır. Windows PowerShell'de doğrudan `.ps1` çağrılırken
Bash'teki `\` satır devamı kullanılmaz. Ayrıntılı sürücü açıklaması
`windows/b2ltouch/README.md`, bütün sürücüler için tekrar kullanılabilir
tasarım/build/flash rehberi ise
`windows/b2lufs/AI_DRIVER_BUILD_REFERENCE.md` içindedir.

Test uygulaması artık DXE aşamasında çalışmaz; EFI Shell açıldıktan sonra
başlatılır. Dokunmatik build'inden sonra test uygulamasını SYSTEM ESP'ye koy:

```bash
bash cache_esp.sh --win --system --baseline
```

Test uygulaması bulunduğunda ikinci komut BOOT ve SYSTEM sparse imajlarını
`out/beyond2lte-touch-diag.tar` içinde birleştirir. Odin'de yalnızca bu tek
dosyayı AP alanından, **Re-Partition kapalıyken** flashla. BCD yeniden
üretilmez; mevcut `esp/` içeriği aynen kullanılır.

Dokunmatik cihazda doğrulanmıştır: Windows PnP kaynağı `ACPI\BYN0001` started
durumuna geliyor, VHF çocuk aygıtı standart HID Touch Screen olarak açılıyor,
imleç hareketi ve eşzamanlı birden fazla temas çalışıyor. `%SystemRoot%\B2LTOUCH\b2lmt.exe`
aktif temas sayısını, pointer kimliklerini ve X/Y konumlarını canlı gösterir.

Microsoft native `osk.exe` denemesi şu an build/flash başarısı sayılmaz.
Hedef ADK WinPE `10.0.28000`, eldeki ARM64 Windows kaynağı `10.0.26200` olduğu
için OSK, MUI, eksik PE importları ve TSF COM kaydı taşınmasına rağmen kendi
`Could not start On-Screen Keyboard` penceresiyle kapanmaktadır. Dokunmatik ve
multi-touch bundan bağımsız olarak çalışır; OSK işi ertelenmiştir.

Doğrudan Mu-Silicium komutu aşağıda referans olarak duruyor; günlük beyond2lte
derlemelerinde yukarıdaki `yap.sh` yolu kullanılmalıdır.

```bash
cd ~/Mu-Silicium
python3 build_uefi.py -d beyond2lte
```

Faydalı bayraklar:

| Bayrak | Ne işe yarar |
|---|---|
| `-r DEBUG` | Debug build (daha çok log, daha büyük) |
| `-c` | Temiz build — eski çıktıları siler |
| `-u` | Derlemeden önce repoyu günceller |

**İlk denemede `-r DEBUG` kullanma.** Önce RELEASE ile derlenip derlenmediğini gör;
derleme hatalarıyla çalışma zamanı hatalarını aynı anda kovalamak istemezsin.

Çıktı: **`Mu-beyond2lte.img`** — hazır Android boot image. Script `mkbootimg`'i
kendisi çalıştırıyor, DTB'yi kendisi ekliyor. Elle paketleme yok.

## 5. Windows'a taşı

```bash
cp Mu-beyond2lte.img /mnt/c/Users/orhun/Desktop/
```

## 6. Flash — önce oku

**Bu adımda cihaz bricklenebilir.** Devam etmeden önce:

- [ ] Stock firmware elimizde (`stockfirmware/`, G975FXXSGHWC1) — Odin ile geri dönüş açık
- [ ] LineageOS 23.2 build'inin zip'i elinde — dirty flash ile geri dönersin
- [ ] Download Mode'a girebildiğini test ettin: cihaz kapalıyken **Bixby + Ses Kısma + USB**
- [ ] Recovery: **Bixby + Ses Açma + USB**

UEFI `boot` partition'ına yazılıyor, `sboot`'a **değil**. Samsung'un asıl
bootloader'ı el değmeden kalıyor, yani Download Mode her hâlükârda erişilebilir
olmalı. Riski büyük ölçüde azaltan şey bu — ama sıfırlamıyor.

Odin için tar'la (PowerShell'de, WSL'de tar'layıp kopyalamak daha kolay):

```bash
cd ~/Mu-Silicium
cp Mu-beyond2lte.img boot.img
tar -H ustar -cf uefi-boot.tar boot.img
cp uefi-boot.tar /mnt/c/Users/orhun/Desktop/
```

Windows'ta Odin → **AP** slotuna `uefi-boot.tar` → Start.
(Re-Partition **işaretli olmasın**.)

## 7. Ne bekliyoruz

Cihaz açıldığında ekranda Mu-Silicium logosu / UEFI ekranı. Bu kadar.

Çalışmayacak olanlar ve nedenleri `HARDWARE.md` ile `UFS_DRIVER.md`'de:
depolama (UFS sürücüsü yok), USB, OS boot.

Ses tuşlarının menüde çalışması iyi işaret — `KeypadDeviceLib`'in GPIO
tanımlarının doğru olduğunu gösterir.

## 8. Geri dönüş

Ekran siyah kalırsa ya da bootloop olursa:

1. **Bixby + Ses Kısma + USB** ile Download Mode
2. Odin → AP slotuna stock `AP_G975FXXSGHWC1_*.tar.md5`
   (sadece boot'u geri istiyorsan LineageOS zip'ini recovery'den sideload et)

`pstore/ramoops` burada **işe yaramaz** — o kernel'in halka tamponu, UEFI aşamasında
kernel yok. UEFI hata ayıklaması için ekran çıktısı ya da UART gerekiyor.

---

## Beklenen takılma noktaları

Bunlar tahmin edilebilir; çıkarsa hata metnini olduğu gibi gönder.

- **`Silicium-ACPI` boş** → `--recursive` unutulmuş.
  `git submodule update --init --recursive` ile düzelir.
- **`stuart_setup` / `edk2toolext` hatası** → pip paketleri eski.
  `python3 -m pip install --upgrade -r pip-requirements.txt`
- **Disk dolu** → clone 5 GB + build birkaç GB. `df -h ~` ile bak.
- **`ACPI.inc` ile ilgili hata** → beyond1lte'de o dosyanın tamamı yorum satırı,
  bizimki de öyle. Normal, ACPI henüz devrede değil.
