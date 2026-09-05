# beyond2lte — Windows on ARM projesi, sıfırdan devir belgesi

Bu belge dışarıdan bakan biri için yazıldı. Hiçbir ön bilgi varsaymıyor.
Amaç: projeyi hiç görmemiş bir insanın ya da modelin, neyi neden yaptığımızı,
neyi ölçtüğümüzü, neyi eledigimizi ve şu an nerede tıkandığımızı tek
dosyadan anlayabilmesi.

Belgedeki her "elendi" ifadesi bir **ölçüme** dayanıyor. Tahminle elenen
hiçbir şey burada elenmiş sayılmıyor; tahminler ayrı başlık altında.

---

## 0. GÜNCEL BAŞLANGIÇ NOKTASI — 5 Eylül 2026

> **Bu bölüm güncel tek devir kaynağıdır.** Aşağıdaki 1–14. bölümler, projenin
> önceki UEFI, Windows açılışı ve UFS araştırmalarını koruyan tarihsel günlüktür.
> Eski bölümlerde geçen “şu an”, “sıradaki iş” ve eski sürüm numaraları güncel
> talimat sayılmamalıdır. Güncel USB gerçeğinin ayrıntılı kaynağı ayrıca
> `specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md` dosyasıdır.

> **5 Eylül / `.33` kaynak güncellemesi:** Runtime BYP/APCMD/WATER/CC/PD
> uygulanan sırası gerçek pasif MAX ISR'ye taşındı; MUIC WRITE/READ isteği
> asenkron. Foto/registry işi artık nested handler'ları bekletmez. İlk
> CC/su/PD kontrolü Linux probe sırasına alındı. `MAX FOTO v6` 40 satırda
> MUIC_NT/Q/OK alanlarıyla bekleyen isteği gerçek read-back'ten ayırır.
> Tek atımlık timeout yalnız başarısız iptal yapar, IRQ yerine ilerletmez.
> Ayrıntılar Linux karşılaştırmasının `.33` bölümünde. OS rolü, UFP ve
> PD/kurtarma açık; kaynak çalışması flaş beklemeden devam eder.

> **5 Eylül / `.32` kaynak güncellemesi:** `.31` GPIO/opcode commit'i
> `f01a40e` sonrasında CC güç rolü ve gerçek PD veri rolü ayrıldı; MUIC USB_AP
> DFP olayına taşındı. BYP aşırı akım, su kapısı ve stok VBUSDet detach
> kontrolü eklendi. `MAX FOTO v5` güç/veri rollerini ayrı gösterir. En güncel
> ayrıntı Linux karşılaştırmasının `.32` bölümüdür. Flaş beklenmiyor;
> dinamik DWC3/xHCI, asenkron bildirim ve PD/kurtarma işleri sürüyor.

> **5 Eylül / `.31` kaynak güncellemesi:** HSI2C GSIV114'e ek olarak özgün
> MAX77705 GPA1-6 / GSIV51 pasif kesmesi, MFD/opcode kuyruğu ve timer'sız
> politika işi eklendi. `MAX FOTO v4` iki IRQ zincirini ayrı gösterir.
> Linux karşılaştırmasının başındaki `.31` bölümü önceliklidir. Flaş beklenmeden
> kaynak eksikleri kapatılıyor; PD veri rolü/kurtarma ve dinamik DWC3/xHCI
> zinciri henüz tamamlanmadı. `.31` WSL/telefon sonucu yoktur.

> **5 Eylül / `.30` kaynak güncellemesi:** HSI2C45 aktarımı artık gerçek
> GSIV114/ISR/DPC yolunda; eski FIFO polling yolu kaldırıldı. `.29` foto/FIFO
> düzeltmelerinden sonraki bu değişiklik için **0.20** ve Linux karşılaştırma
> dosyasının başı önceliklidir. WSL/telefon doğrulaması bekleniyor. MAX GPIO
> kesmesi ve MAX -> DWC3 rol zinciri hâlâ eksik; hedef tamamlanmış değildir.

### 0.1 Altmış saniyelik özet

Hedef, Samsung Galaxy S10+ `SM-G975F / beyond2lte / Exynos 9820` üzerinde
Mu-Silicium tabanlı UEFI üzerinden Windows ARM64 WinPE çalıştırmak ve cihazın
özgün donanımlarını Windows sürücüleriyle sırayla ayağa kaldırmaktır.

Bugün itibarıyla:

- UEFI, ekran, yan tuşlar, ACPI, UEFI UFS, Windows Boot Manager, Windows
  çekirdeği ve WinPE erken kullanıcı modu çalışıyor.
- Windows `b2ltouch.sys` + VHF yolu çalışıyor; on temaslı gerçek multi-touch ve
  imleç hareketi cihazda doğrulandı.
- Windows USB tarafında sabit USB2 HOST denetleyicisi `Started/RUN` görülüyor.
  Root-hub/alıcı/HID zincirinin bütünü bu fotoğraflarla doğrulanmış değil;
  boş özetlerde eksik `findstr` etkisi var. USB henüz çalışmıyor.
- MAX77705, Windows'tan HSI2C45 üzerinden erişiliyor; kimlik, charger kilidi,
  opcode zinciri, kesme maskeleri, CCCTRL1 ve Linux probe-sonu CONTROL3 işlemi
  başarıyla tamamlanıyor. Ancak `.28` fotoğraflarındaki bütün sekiz baytlık
  status okumaları timeout veriyor; taşıma katmanı tamamlanmış değil.
- Fakat fiziksel Type-C attach kararı oluşmuyor. Takılı Logitech `046d:c52e`
  alıcısıyla önceki flaşta CC1, yeni fotoğraflarda CC2 pin durumu görülmesine
  rağmen MAX77705 rolü `SOURCE` olmuyor.
  Bu yüzden güvenlik gereği VBUS kapalı, MUIC analog yolu `OPEN=0x3f` ve xHCI
  portu `RxDetect/CCS=0` kalıyor. Fare/HID katmanına henüz gelinmedi.
- En erken doğrulanmış hata artık **3. adım: sekiz baytlık I2C okuması**.
  `.30` HSI2C GSIV114 aktarımını, `.31` MAX GPIO GSIV51/opcode kuyruğunu,
  `.32` CC güç rolü/PD veri rolü ayrımını, `.33` asenkron MUIC ve nested
  kesme sırasını kaynakta uygular. Bu yeni sürümlerin
  cihaz sonucu yoktur. MAX -> Windows role-switch/DWC3 bildirimi eksiktir.
- Yeni fotoğraflar aynı D0 oturumunda 13 -> 15 olayı gösteriyor; tamamının
  status snapshot'ı geçersiz. ForceError veya firmware reset gerektiği bu
  kayıtlardan çıkarılamaz. Eksik `findstr` ve seri registry sorguları tanı
  akışında ayrıca düzeltildi; cihazdaki toplam süre henüz ölçülmedi.

Tam Windows masaüstü kurulumu henüz hedeflenen günlük test değildir. WinPE,
Android `SYSTEM` bölümüne konan FAT32 ESP'deki `boot.wim`den RAM diske açılır.

### 0.2 Kaynakların öncelik sırası

Yeni bir inceleme çelişki görürse şu sırayı kullansın:

1. Gerçek çalışan kaynak ve betikler: `yap.sh`, `cache_esp.sh`,
   `tools_b2lusb.sh`, `windows/startnet.cmd`, `windows/b2lusb/` ve aktif
   `mu-silicium-overlay/` ağacı.
2. Güncel Linux/Windows USB karşılaştırması:
   `specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md`.
3. Bu bölüm (`DEVIR.md`, bölüm 0).
4. Alt sistem ayrıntıları için `windows/b2lusb/README.md`,
   `windows/b2ltouch/README.md`, `windows/b2lufs/README.md` ve `BUILD.md`.
5. `STATUS.md` ve bu dosyanın aşağıdaki eski bölümleri tarihsel kanıt
   günlüğüdür; güncel komut veya öncelik için tek başlarına kullanılmamalıdır.

Önemli kaynak tuzağı: aktif UEFI platformu
`mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/` altındadır. Depo
kökündeki eski `beyond2ltePkg/` anlık görüntüsüne yapılan değişiklikler
`yap.sh` tarafından derlenmez.

### 0.3 Güncel repo ve sürüm çıpası

`.33` kaynak sürümü (bu güncellemenin commit'i için `git log -1`):

```text
dal:    codex/touch-from-8549bfa
taban:  7313c9d  docs: clarify remaining Type-C role-switch dependencies
önceki: 13e96fe  fix: separate MAX77705 power and data role events
```

Yeni WSL build'inde beklenen paket sürümleri (henüz cihazda doğrulanmadı):

| Bileşen | Sürüm | ACPI kimliği / işlev |
|---|---:|---|
| `b2ltouch.sys` | `0.2.0.1` | `ACPI\BYN0001`, Samsung sec_ts -> KMDF/VHF touch |
| `b2lusb.sys` parent | `0.1.0.18` | `ACPI\BYN0002`, PHY/DWC3 platform hazırlığı |
| inbox host | `0.1.0.18` INF | `ACPI\BYN0003`, `USBXHCI` + GSIV 273 |
| `b2lmax77705.sys` | `0.1.0.33` | `ACPI\BYN0004`, HSI2C45/MAX77705 Type-C/OTG |
| `b2lufs.sys` | `0.0.4.5` kaynak sürümü | Deneysel Windows UFS miniport; güncel USB kapısı değil |

Yalnız MAX77705 değiştiğinde parent/host sürümünün aynı kalması normaldir.
Paket manifesti üç sürümü ayrı ayrı INF dosyalarından türetir. Cihaz fotoğrafını
hangi kaynakla eşleştirdiğimizi anlamak için özellikle
`B2LUSB_MAX77705_DRIVER_VERSION` satırına bakılır.

Çalışma ağacında `out/` derleme/ölçüm artıkları ve
`specs/UEFI_beyond2lte_reverse_spec.md` kullanıcıya ait izlenmeyen dosya olarak
bulunabilir. Kör `git add .` yapılmamalıdır; yalnız amaçlanan takipli dosyalar
seçilmelidir.

### 0.4 Açılış ve sürücü mimarisi

```text
Samsung S-Boot
  -> BOOT bölümündeki Mu-Silicium UEFI boot.img
  -> UEFI Shell / startup.nsh
  -> SYSTEM bölümündeki FAT32 ESP
  -> bootmgfw.efi -> boot.sdi + boot.wim
  -> WinPE RAM disk
  -> Windows/System32/startnet.cmd
       +-> BYN0001 b2ltouch -> VHF -> HID Touch Screen
       +-> BYN0004 b2lmax77705 -> HSI2C45 -> MAX77705
       +-> BYN0002 b2lusb -> Exynos USB PHY/DWC3 HOST
       `-> BYN0003 USBXHCI -> USBHUB3 -> HIDUSB/MOUHID/MOUCLASS
```

USB paketi **USB2-only sabit HOST** profilidir. SuperSpeed combo PCS/PMA
sayfaları cihazda synchronous external abort verdiği için Windows ACPI'sine ve
sürücü yazma yoluna alınmaz. `LINK_PORT.U3_DISABLE` açıktır. Rastgele combo-PHY
okuması/yazması eklenmemelidir.

Şu anda ACPI `_DEP`, BYN0002'nin BYN0004 sürücüsünün başlamasını beklemesini
sağlar; ancak MAX probe/politika işi PostInterruptsEnabled sonrasında ayrı
çalışır. `_DEP` gerçek CC/PD rolünü beklemez. Statik `.18` parent bu nedenle
konektör DFP olmadan xHCI başlatabilir. Doğru eşdeğerlik yalnız READY kontrolü
değildir: gerçek Type-C/UCM veri rolü bildirimi ve OS'nin host/function start-stop
yaşam döngüsü kurulmalıdır. Bu kaynak işi için kullanıcıdan flaş beklenmiyor.

### 0.5 Sıfırdan ortam hazırlama

Desteklenen iş bölümü:

- Kaynak, UEFI, ACPI, WIM ve Windows ARM64 sürücü paketleme: WSL2 Ubuntu.
- MSVC/WDK ARM64 derlemesi: WSL içinden çağrılan Windows PowerShell.
- Odin flaşı ve telefona USB erişimi: Windows.
- macOS analiz için kullanılabilir; Windows sürücüsü üretmek için desteklenen
  build ortamı değildir.

Mu-Silicium WSL'nin Linux dosya sisteminde olmalı; `/mnt/c` üzerinde build
etmek hem yavaş hem büyük/küçük harf bakımından risklidir:

```bash
cd ~
git clone --recursive https://github.com/Project-Silicium/Mu-Silicium.git
cd Mu-Silicium
./setup_env.sh -p apt
python3 -m pip install --upgrade -r pip-requirements.txt
```

Gerekli temel araçlar arasında Python 3, Clang/EDK2 bağımlılıkları, `iasl`,
`wimlib-imagex`, `mtools`, `img2simg`, `openssl` ve `osslsigncode` bulunur.
Windows tarafında ARM64 C/C++ bileşenli Visual Studio ve WDK gerekir. Son
doğrulanan ortam WDK `10.0.28000.0` ve Visual Studio 18 Community'dir.

`boot.wim` hiç yoksa ADK ARM64 WinPE medyasından bir kez oluşturulur:

```bash
bash tools_winpe_adk.sh "/mnt/c/Program Files (x86)/Windows Kits/10/Assessment and Deployment Kit/Windows Preinstallation Environment/arm64/Media"
```

Bu işlemden sonra ilgili özel paketler yeniden WIM'e enjekte edilmelidir.
Güncel `--touch --usb-test` turu touch ve USB paketini birlikte ekler. Windows
UFS paketi de yeni WIM'de isteniyorsa ayrıca `bash tools_b2lufs.sh` çalıştırılır.

Betikler kendi proje konumlarını bulur. Eski dokümanlardaki kullanıcıya özel
mutlak repo yolları kopyalanmamalıdır. Harici Mu yolu farklıysa:

```bash
MU_SILICIUM=/gercek/Mu-Silicium/yolu bash yap.sh --touch --usb-test --clean
```

### 0.6 Güncel tam derleme ve flaş üretim turu

MAX77705/USB + Windows touch için kullanılan günlük tam sıra:

```bash
cd /bu/deponun/WSLdeki/yolu
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

Birinci komut tek turda:

1. bellek yerleşimi ve Windows reserved-memory denetimlerini çalıştırır;
2. ACPI tablolarını yeniden üretir;
3. overlay'i harici Mu-Silicium ağacına kopyalar;
4. UEFI'de `B2LTouchDxe` ve `B2LMax77705Dxe` profilini açar;
5. UEFI `boot.img` ve `out/uefi-boot.tar` üretir;
6. ARM64 `b2ltouch` ile `b2lusb/b2lusbmon/b2lmax77705/b2lusbctl` çıktısını
   Windows MSVC/WDK ile derler ve test-imzalar;
7. touch + USB paketlerini tek DISM mount/commit ve tek UAC istemiyle
   `esp/sources/boot.wim` DriverStore'una offline ekler;
8. manifest, hash, inbox USB/HID dosyaları ve yeni `startnet.cmd` içeriğini
   geri okuyarak doğrular.

İkinci komut büyük WIM'i taşıyan Windows SYSTEM ESP'sini üretir, WIM'i
optimize/verify eder ve yerel sürücü hash'leriyle manifestin aynı olduğunu
yeniden kontrol eder.

Derleme sırasında Windows'ta UAC penceresi gelmesi normaldir; offline DISM
servislemesi yönetici yetkisi ister. Kaynak değiştiği halde eski `.sys`
kullanılmamalıdır.

### 0.7 `yap.sh` bayrakları nasıl çalışır

| Bayrak | Etki | Ne zaman kullanılır |
|---|---|---|
| bayraksız | DEBUG UEFI; mevcut Mu ACPI çıktısını kullanabilir | Güncel donanım deneyi için önerilmez |
| `--acpi` | PPTT + bütün S5E9820 ACPI tablolarını yeniden derler ve etkinleştirir | ACPI/normal temiz UEFI turu |
| `--touch` | `--acpi`yi zorlar; UEFI touch DXE'yi ve Windows VHF paketini açar | Windows touch korunacaksa |
| `--touch-test` | `--touch`ü içerir; Windows öncesi kareli UEFI touch test uygulamasını ESP'ye koyar | Yalnız dokunmatik tanısı |
| `--usb-test` | `--acpi`yi zorlar; MAX DXE ve `B2LUSB.EFI` üretir, Windows statik HOST paketini derler/stage eder | Güncel USB/MAX geliştirme turu |
| `--psci-hvc` | ACPI FADT'yi HVC yapar ve EL2 köprüsünü açar | Ayrı, deneysel PSCI turu; USB günlük turuyla karıştırma |
| `--clean` | Mu build'ini temiz yapar | Kaynak/INF/ACPI değiştiğinde kullan |
| `-r RELEASE` veya `--release RELEASE` | RELEASE UEFI üretir | Tanı gerekmeyen boyut/hız turu |
| `--tar` | Geriye uyumluluk için etkisiz | Tar zaten her başarılı build'de üretilir |

`--touch` ve `--usb-test` birlikteyse touch paketi önce hazırlanır, offline
DriverStore stage'i ertelenir ve USB aracı ikisini `UsbTouch` paketi olarak tek
mount/commit işleminde birleştirir. Bu yüzden güncel tam turda ayrıca
`tools_b2ltouch.sh` çağırmak gerekmez.

Önemli ortam değişkenleri:

| Değişken | Varsayılan | Anlam / risk |
|---|---:|---|
| `MU_SILICIUM` | `~/Mu-Silicium` | Harici Mu ağacının yolu |
| `OUTDIR` | otomatik Windows masaüstü araması | Üretilen flaş dosyalarının ek kopya hedefi |
| `B2L_WDT` | `1` | UEFI watchdog düzeltmesini uygular; `0` yalnız kontrollü karşılaştırma içindir |
| `B2L_UFS_PWM` | `0` | `0` = HS Gear 3. `1` çok yavaş PWM G1 yamalarını açar; güncel USB turunda kullanma |
| `B2L_USB_AUTO_HOST` | `yap.sh --usb-test` içinde `1` | WIM'e otomatik HOST test marker'ı koyar; `0` tanı akışını kapatır |
| `B2L_SKIP_WINDOWS_TOUCH_BUILD` | `0` | `1` touch Windows build/stage'ini atlar; normal tam turda kullanma |
| `B2L_SKIP_WINDOWS_USB_BUILD` | `0` | `1` yalnız UEFI USB uygulamasını bırakır; güncel Windows testi için kullanma |
| `B2L_SKIP_MSVC_BUILD` | `0` | `1` mevcut ARM64 çıktıları yeniden paketler; yalnız kaynaklardan eski değillerse |
| `B2L_ALLOW_STALE_SYS` | `0` | Eski `.sys` korumasını bilinçli aşar; normal testte kesinlikle kullanma |
| `B2L_WINROOT`, `B2L_WDK_ROOT` | `/mnt/c`, otomatik WDK | Windows/WDK farklı diskteyse ileri düzey yol override'ı |
| `B2L_WINDOWS_ARM64_SOURCE` | yok | Native OSK/runtime taşıma deneyleri için Windows ARM64 ISO/install.wim yolu; çekirdek USB turu için gerekmez |

`B2L_STAGE_TOUCH_WITH_USB` ve `B2L_DEFER_OFFLINE_STAGE` dahili koordinasyon
değişkenleridir; günlük komutta elle verilmemelidir.

### 0.8 `cache_esp.sh` bayrakları ve gerçek çıktılar

| Bayrak | Anlam |
|---|---|
| bayraksız | Linux ESP'yi `CACHE` için üretir; Windows dosyalarını dışlar |
| `--win` | Windows bootmgfw/BCD/boot.sdi/boot.wim içeren ESP üretir |
| `--system` | 600 MiB CACHE yerine büyük Android `SYSTEM` bölümünü hedefler; büyük WIM için gereklidir |
| `--linux` | Linux'u bilerek SYSTEM'e yazacağını açık eder; güvenlik kilidini kaldırır |
| `--baseline` | Yalnız Windows. `B2LGOSTR/B2LUSB/B2LTOUCH/EBSTEST.EFI` uygulamalarını dışlar, fakat Windows sürücülerini WIM'de tutar |
| `--bos` | Yalnız `--win` ile, 600 MiB boş FAT32 CACHE üretir; `--system` ile birlikte kullanılamaz |
| `--psci-hvc` | Yalnız Windows HVC deneyi; `--baseline` ile birlikte kullanılamaz |

Kritik ayrımlar:

- `--system` verip `--win` vermemek geçmişte SYSTEM'e Linux ESP yazılmasına
  yol açtı. Betik artık bunu reddeder. Güncel Windows komutu tam olarak
  `--win --system --baseline` olmalıdır.
- `--baseline`, UEFI tanı **uygulamalarını** çıkarır; `b2lusb.sys`,
  `b2lmax77705.sys` ve `b2ltouch.sys` WIM içinde kalır. Güncel WinPE sürücü
  testi için doğru profildir.
- Güncel betik `--baseline` turunda birleşik `beyond2lte-usb-diag.tar`
  üretmez. Gerçek çıktılar `out/uefi-boot.tar` (`boot.img`) ve
  `out/system-esp.tar` (`system.img`) olur. Eski `BUILD.md` içindeki
  “baseline tek birleşik tar üretir” cümlesi bu bakımdan eskidir.
- `out/beyond2lte-usb-diag.tar` yalnız baseline **kapalı**, `B2LUSB.EFI`
  SYSTEM ESP'ye gerçekten konmuş aktif UEFI tanı turunda üretilir. İçinde
  `boot.img + system.img` vardır.
- `out/beyond2lte-touch-diag.tar` da yalnız baseline kapalı ve
  `B2LTOUCH.EFI` mevcut UEFI test turu içindir. Eski dosyaya adına bakarak
  güvenme; her zaman içeriğini listele.

### 0.9 Odin flaş sözleşmesi

Samsung Odin hedefi tar arşivinin içindeki dosya adına göre seçer:

| Tar üyesi | Hedef bölüm |
|---|---|
| `boot.img` | `BOOT` — Mu-Silicium UEFI |
| `system.img` | `SYSTEM` — Windows FAT32 ESP + WIM |
| `cache.img` | `CACHE` — Linux/boş/yardımcı ESP |

Flaş öncesi her arşivi doğrula:

```bash
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

Güncel baseline turunda ilk arşiv yalnız `boot.img`, ikincisi yalnız
`system.img` göstermelidir. Aynı kaynak turuna ait BOOT ve SYSTEM cihazda
birlikte bulunmalıdır; farklı turlardan kalan arşivler karıştırılmamalıdır.
Birleşik tanı profili kullanılıyorsa ilgili tar içinde hem `boot.img` hem
`system.img` gözle doğrulanır.

Odin'de arşiv AP alanından yüklenir ve **Re-Partition kesinlikle kapalı**
kalır. Bu çalışma `sboot`u değil Android BOOT/SYSTEM/CACHE bölümlerini
değiştirir. Yine de yanlış üye adlı veya yanlış tura ait tar ciddi risk
oluşturur. Download Mode ve stock/Lineage geri dönüş paketi hazır olmadan
flaş yapılmamalıdır.

### 0.10 Cihazda beklenen güncel akış

Başarılı paket/stage doğrulamasından sonra ilk ekranın paket kimliğinde şunlar
görülmelidir:

```text
B2LUSB_PACKAGE=exynos9820-kmdf-static-usb2-host-bringup
B2LUSB_DRIVER_VERSION=0.1.0.18
B2LUSB_HOST_DRIVER_VERSION=0.1.0.18
B2LUSB_MAX77705_DRIVER_VERSION=0.1.0.27
B2LUSB_ACPI_ID=ACPI\BYN0002
B2LUSB_HOST_ID=ACPI\BYN0003
B2LUSB_MAX77705_ID=ACPI\BYN0004
B2LUSB_SUPERSPEED=DISABLED_UNTIL_COMBO_PMA_SAFE
B2LUSB_AUTO_HOST=1
B2LUSB_OFFLINE_DRIVERSTORE=1
```

Sonra `pnputil` çıktısında BYN0002, BYN0003 ve BYN0004 `Started` olmalıdır.
BYN0003 servisinin `USBXHCI` olması ve IRQ 273/MMIO `0x10c00000` kaynaklarını
alması, xHCI katmanının çalıştığını gösterir. Bu tek başına kablonun veya
alıcının numaralandırıldığı anlamına gelmez.

Akış sonunda yazılımsal yeniden başlatma bu platformda kasıtlı yapılmaz; test
ekranı burada bekler. Bu bir çökme değildir. Gerekirse donanım tuşuyla yeniden
başlatılır. Dokunmatik test aracı `X:\Windows\B2LTOUCH\b2lmt.exe` sonunda
çalışabilir.

### 0.11 Son Linux incelemesi: dosyalar, konumlar ve ne kanıtlıyorlar

Son karşılaştırma iki yerden oluşur:

1. **Ham recovery/Linux kanıtı:**
   `out/B2LUSB-LINUX-HOST-MOUSE-RERUN.txt`
2. **Ham kanıttan çıkarılan, Windows ile adım adım güncel karşılaştırma:**
   `specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md`

Ham dosya yaklaşık 82 KiB'dir ve `out/` altında build/log artığı olarak
izlenmez. Yeni bir çalışma ağacına otomatik gelmeyebilir; mevcut makinede
korunmalı veya güvenli bir özel kanala ayrıca yedeklenmelidir. Ham snapshot
cihaz/boot ortamı ayrıntıları içerebildiği için herkese açık commit'e körlemesine
eklenmemelidir. Takipli ve paylaşılabilir sonuç belgesi `specs/` altındaki
karşılaştırmadır.

Linux testi recovery/Linux 4.14 ortamında, Windows testindekiyle aynı fiziksel
Logitech `046d:c52e` alıcı takılarak alındı. Dosyada özellikle:

- `309–375`: Type-C sınıfı, power-role/data-role, HSI2C45/MAX77705 sahipliği;
- `538–715`: attach anındaki `cc_SOURCE`, charger/VBUS, MUIC `06 09`, DWC3
  host/PHY ve opcode sırası;
- `747–789`: VBUS/OTG ve ilgili rol bildirimleri;
- `842–925`: USB bus, `046d:c52e`, descriptor ve HID bağlanması;
- `978–1016`: HSI2C IRQ114, MAX parent GPIO IRQ318 ve DWC3 IRQ273 ayrımı

incelenmelidir. Satırlar kaynak büyürse kayabilir; zaman damgası ve olay
adlarıyla da aranmalıdır.

Linux'ta gözlenen temel zincir:

```text
MAX77705 parent IRQ318
  -> CC1_ACTIVE / PLUG_ATTACHED / cc_SOURCE
  -> data role DFP, power role SRC
  -> charger OTG/VBUS
  -> MUIC CONTROL1_WRITE 06 09 (USB_AP)
  -> DWC3 role switch + PHY runtime resume
  -> xHCI
  -> yaklaşık 354 ms sonra Logitech 046d:c52e
  -> HID keyboard yaklaşık 543 ms
  -> HID mouse yaklaşık 610 ms
```

Linux'taki `IRQ114`, HSI2C45'in Linux sanal/GIC numarası; `IRQ318` MAX77705
GPA1_6 parent GPIO olayı; `IRQ273` DWC3/xHCI'dir. Windows ekranındaki IRQ 273,
MAX GPIO kesmesi değildir. ACPI'de doğrulanmış GPA1_6 kaynağı olmadan 114 veya
318 sayısını Windows'a kopyalayıp sahte ISR kurulmaz.

Karşılaştırma belgesi 16 aşamayı yan yana tutar. Çalışmaya devam etmeden önce
özellikle “En kısa sonuç”, 7. satır, “Kritik farklar” ve “Son teşhis ve bir
sonraki değişiklik sınırı” bölümleri okunmalıdır.

### 0.12 Son `.27` cihaz kanıtının kesin yorumu

Son flaşta aynı Logitech alıcı takılıyken ölçülen kritik değerler:

```text
STAGE=0x70                 STATUS=0x0
PLATFORM_OK=0x1            I2C_RETRY=0x0
PMIC_ID=0x15               PMIC_REV=0x2
USBC_HW/FW/MINOR=1a/1a/10  CHG_UNLOCK=0x1
UIC/CC/PD/VDM=00/20/00/f0  MASKS_OK=0x1
CCCTRL1=0x1f               CC0/CC1=0x40/0x00
CCOPEN_OK=0x1              CTRL3_BEFORE/AFTER=0x64/0x64
TOP_SEEN=0x9               CC_SEEN=0x6d
PD_SEEN=0xc0               CC_IRQS=0x4
CC_SAMPLES=0x71f           CC_TRANS=0x0
CC_STATE/PREV=0x0/0x0      CC_SRC_EV=0x0
MUIC_OK=0x1                MUIC_CTL1=0x3f
OTG_SW/HW=0/0
```

Bunların anlamı:

- HSI2C platform/saat/pin hazırlığı, TOP/USBC/charger erişimi ve init opcode
  zinciri çalışıyor.
- Linux çalışma maskeleri ile CCCTRL1 gerçekten yazılıp geri okunmuş.
- `.27`de eklenen probe-sonu CONTROL3 normalizasyonu çalışmış:
  `CCOPEN_OK=1`. `0x64` içinde bit 1 zaten sıfır olduğu için
  `LockErrorEn` baştan kapalıymış; `0x64 -> 0x64` beklenen no-op read-back'tir.
  Böylece “LockErrorEn attach'i engelliyor” hipotezi yanlışlandı.
- TOP/CC/PD olay registerları canlı; dört CC olayı görülmüş. Fakat fiziksel
  `CC1_ACTIVE` rol durum makinesinde `cc_SOURCE` üretmemiş.
- Sürücü güvenli davranıyor: source kanıtlanmadan VBUS veya USB_AP yolu
  zorlanmıyor. Bu yüzden `MUIC=0x3f`, OTG kapalı ve xHCI portu bağlantısız.
- xHCI `RUN=1`, command ring/DCBAA/CONFIG kurulmuş olsa da portlarda
  `CCS=0`, `PED=0`, `PLS=RxDetect`; USB/HID/Mouse için “No devices were
  found” bu sınırda beklenen sonuçtur.

Dolayısıyla HID sürücüsü, root hub, xHCI ring, HSI2C transferi, kesme maskesi
ve CONTROL3 şu an ilk hata noktası değildir.

### 0.13 On yedi dakikalık yavaşlığın güncel değerlendirmesi

Polling düzeltmesi işe yaradı:

- Eski xHCI gözleminde yaklaşık `7.887` örnek vardı.
- `.27`de 20 ms aralıklı 5 saniyelik sınırlı gözlem yalnız `160` örnek verdi;
  yaklaşık `%98` azalma.
- Konsoldaki “xHCI observer 12 saniye” metni eskimiş sabit mesajdır; gerçek
  sürücü davranışı 5 saniyedir.
- MAX `CC_SAMPLES=0x71f = 1.823`. İlk fotoğraftan önce başlayan sayaçla
  17 dakikaya bölündüğünde toplam hız en fazla yaklaşık `1,79 örnek/s`;
  ilk 50 hızlı örnek çıkarıldığında geç dönem en fazla `1,74 örnek/s`, yani
  örnek başına en az yaklaşık `575 ms` görünür.

Bu nedenle kalıcı 50 Hz MAX polling veya 1 ms xHCI polling, 17 dakikanın baskın
nedenleri değildir. Kesin suçlu hâlâ ölçülmemiştir; `startnet.cmd` aşama zaman
damgası basmıyor.

Mevcut en güçlü adaylar:

- `B2LUsbCollectMax77705State`, bir çağrıda 86 doğrudan ve 55 özet olmak üzere
  141 ayrı `reg.exe` işlemi açar; akışta iki kez çağrıldığı için en az 282
  registry süreci üretir.
- Birden fazla seri `pnputil /scan-devices` ve ayrıntılı
  `/enum-devices /services /stack /drivers /resources` çağrısı vardır.
- ACPI Enum altında özyinelemeli registry araması ve tam
  `setupapi.dev.log` taraması yapılır.
- `logman` ETW başlat/durdur işlemleri ve senkron `tracerpt` CSV dönüşümü
  yapılır.
- Büyük PnP/registry raporları dar ve yavaş WinPE konsoluna `type` ile basılır.
- Sürücü geniş tanı kümesini her on başarılı geç örnekte registry'ye yeniden
  yazar; bu ek yük olabilir ama tek başına 17 dakikayı kanıtlamaz.

Performans değişikliği önce her pahalı `startnet.cmd` fazının başına/sonuna
zaman damgası koymalıdır. Ölçmeden ikinci MAX dökümünü, ETW'yi veya PnP
sorgularını topluca silmek kanıt kalitesini düşürür ve hangi adımın yavaş
olduğunu yine bilinmez bırakır.

### 0.14 Sıradaki işlevsel değişiklik ve başarı ölçütü

İlk işlevsel uyuşmazlık **karşılaştırma adımı 7**dir. Sıradaki kaynak incelemesi
Samsung Linux MAX77705 CC interrupt/FSM yolunda şu sıralamayı birebir
çıkarmalıdır:

1. `CC1_ACTIVE` sonrasında hangi CC interrupt/status alanları okunuyor?
2. `PLUG_ATTACHED` ve `cc_SOURCE` hangi koşullarla üretiliyor?
3. `ForceError` veya firmware hata-kurtarma durumu bu karardan önce/sonra nasıl
   temizleniyor ya da koordine ediliyor?
4. Windows yolunda eksik ara durumları ayırmak için hangi read-back/sayaçlar
   eklenmeli?

Güvenli değişiklik sınırı:

- Gerçek `CC_SOURCE=2` kanıtlanmadan MUIC `USB_AP=0x09`, charger OTG veya VBUS
  zorlanmamalıdır.
- Başarı sırası önce `CC_STATE=2`, `CC_TRANS>0`, `CC_SRC_EV>0`; sonra MUIC
  read-back `0x09`, OTG/VBUS aktif; en son xHCI portunda `CCS=1` ve
  `046d:c52e` enumeration olmalıdır.
- Linux koordinasyon katmanının sonucu modellenmeli; ham Linux IRQ sayılarını
  Windows'a kopyalamak çözüm değildir.
- CC source güvenilir hale geldikten sonra ayrı değişiklikle MAX READY/source
  koşulları BYN0002 HOST başlangıcına kapı yapılmalıdır.

Performans enstrümantasyonu ile CC/FSM işlev değişikliği aynı committe
karıştırılmamalıdır; biri zamanlama/ölçüm, diğeri elektriksel rol davranışıdır.

### 0.15 Güvenlik kuralları ve kapanmış yollar

- CC source doğrulanmadan VBUS verme. Yanlış source/sink kararı porta veya
  karşı cihaza zarar verebilir.
- Combo PCS/PMA adreslerine kör MMIO erişme; cihazda abort kanıtı var.
- Linux IRQ114/IRQ318'i ACPI/GIC kaynağı doğrulamadan Windows ISR'sine sabitleme.
- `B2L_UFS_PWM=1` kullanma; WinPE WIM okumasını dakikalara çıkaran yaklaşık
  3 MB/s PWM G1 yoludur ve güncel USB sorununu çözmez.
- Eski `.sys`i `B2L_ALLOW_STALE_SYS=1` ile paketleyip yeni kaynakmış gibi test
  etme.
- `--system`i `--win` olmadan kullanma.
- `Re-Partition` açma; yanlış tar üyesine veya partition'a güvenme.
- `No devices were found` sonucunu HID hatası sayma; önce xHCI portunda
  `CCS=1` görülmeli.
- CONTROL3/LockErrorEn, `.27` kanıtıyla kapanmıştır. Aynı hipotezi yeni bir
  karşı kanıt olmadan tekrar açma.

### 0.16 Hızlı doğrulama ve sık sorunlar

Kaynak değişikliğinden sonra en az şu kontroller yapılmalıdır:

```bash
bash -n yap.sh cache_esp.sh tools_b2lusb.sh tools_b2ltouch.sh
git diff --check
git status --short
```

Flaş dosyası üretildikten sonra:

```bash
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

Sık karşılaşılan durumlar:

- `C2220` ile beraber `C4310`: uyarılar hata sayılıyor. CONTROL3 sabit maske
  truncation düzeltmesi `2e48112` commitindedir; doğru HEAD ve temiz build
  kullanıldığını doğrula.
- `Silicium-ACPI` boş: Mu-Silicium recursive alınmamış; submodule'leri başlat.
- `powershell.exe`/MSVC/WDK bulunmuyor: WSL-Windows köprüsü veya ARM64 VS/WDK
  bileşenleri eksik.
- WIM stage uzun süre görünürde duruyor: Windows UAC penceresini kontrol et.
- `boot.wim` hash/manifest hatası: yerel `.sys` ile WIM farklı turdan; yeniden
  `tools_b2lusb.sh` veya tam `yap.sh --touch --usb-test --clean` çalıştır.
- `CACHE`e WIM sığmıyor: güncel Windows yolu `--system` kullanır.
- Ekranda eski sürüm: yanlış BOOT/SYSTEM tar'ı flaşlanmış veya eski WIM
  paketlenmiş; tar üyeleri ile cihaz manifestini birlikte karşılaştır.

### 0.17 İlgili son commit zinciri

Güncel sınırı oluşturan son değişiklikler, eskiden yeniye özetle:

```text
44deb23  stale HSI2C durumundan transfer başlatma düzeltmesi
5608362  MAX polling'i tek boot CPU'sundan ayırma
423fd45  MAX77705 HSI2C platform/saat/pin hazırlığı
04ca231  Linux ile MAX77705 kesme-init düzeni eşleştirmesi
f295fe2  CC event teslim/örnek sırası eşleştirmesi
6bbf2d5  probe-sonu CC-open CONTROL3 eşleştirmesi
2e48112  MSVC C4310/C2220 sabit maske düzeltmesi
24af2a4  .27 cihaz, zamanlama ve CC sonuçlarının belgelenmesi
```

Bir sonraki çalışma, `24af2a4` üzerindeki kanıtı başlangıç kabul etmeli;
önceki başarısız hipotezleri yeniden denemeden Linux karşılaştırmasının 7.
adımına ilerlemelidir.

---

### 0.18 Tarihsel: `.28` olay tanısının ilk build/test planı

USB olmadan kullanıcı kanıtı artık **fotoğraf** üzerinden alınır. Güncel
`startnet.cmd`, test sonunda `b2lusbctl.exe maxscreen` açar: GENEL DURUM ve
numaralı olay sayfaları 20 saniyede bir otomatik döner; yeni turda kayıtlar
yenilenir. Komut/klavye veya TXT dışa aktarımı gerekmez. Son ekranda bu döngünün
sürmesi donma değildir. `MAX FOTO v1` ekranı görünmüyorsa yeni EXE/startnet
içeren WIM kullanılmıyor olabilir. MAX kernel sürümü `.28` kaldı; WSL build
ve WIM yenilemesi yine gereklidir. Ayrıntılı fotoğraf A/B adımları Linux
karşılaştırma dosyasının başındadır.

MAX77705 sürüm kaynağı `0.1.0.28` oldu; parent/host `0.1.0.18` kaldı.
Opcode öncesinde ve cevap beklerken tüketilen UIC olayları artık kaydediliyor.
SYSMSG, CC ve PD status snapshot'ları; ilk 16 + son 48 olay; zaman damgaları;
snapshot hata geçerliliği eklendi. Kayıt `b2lusbctl.exe maxevents` ile okunur
ve mevcut iki otomatik MAX dökümünde `B2LUSB-MAX77705-EVENTS*.TXT` olarak alınır.

Bu sürüm ForceError/reset/VBUS politikası değiştirmez. GPL kaynak incelemesi,
Linux'un SOURCE rolünü CC pin bilgisinden üretmediğini; doğrudan donanım
status'undan okuduğunu gösterdi. Eski bölüm 0.14'teki araştırma soruları için
güncel cevap ve A/B test yöntemi
`specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md` başındadır.

Windows ARM64/UEFI/WIM build'i bu macOS makinesinde çalıştırılmadı.
Build sözleşmesi değişmedi; güncel kaynakların bulunduğu WSL repo kökünde:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

Aynı turdan `boot.img` ve `system.img` flaşlanır. Yeni `.sys` ve `b2lusbctl.exe`
birlikte yeniden derlenmelidir; eski çıktı atlatma bayrakları kullanılmaz.
Manifest MAX sürümü `.28` olmalı; `MAX_EVENTS v=1`, `count>0`, `SNAP_FAIL=0`
ilk tanı beklentisidir. Sonra baştan takılı ve READY'den sonra takılı alıcı
senaryoları karşılaştırılır. `.28` henüz cihazda çalıştı veya fare düzeldi
olarak kayda geçirilmemelidir.

Yerel kontrol: mevcut Windows MAX/USB güvenlik testleri ve
`python3 tests/test_max77705_events.py` donanımsız hata benzetimi.
Bu test gerçek olay/opcode ve okuyucu fonksiyonlarını sahte I2C/registry
ile sınar; WDK derlemesinin veya fiziksel cihaz testinin yerine geçmez.

### 0.19 5 Eylül: `.28` fotoğrafları incelendi, `.29` kaynak düzeltmesi

Kullanıcının güncel şartı: **USB akışı Linux/Android'deki gerçek işlem sırasını
izleyecek; eksik aşamalar alternatif yolla veya başarılı gösterilerek
atlanmayacak.** Bu nedenle statik HOST ve polling nihai akışın eşdeğeri kabul
edilmiyor. Ayrıntılı 16 aşama denetimi, 15 olaylık fotoğraf dökümü ve kaynak
bağlantıları `specs/B2LUSB_LINUX_WINDOWS_FLOW_COMPARISON.md` başında.

Son cihaz kanıtı `.28` olay tanısını içeriyor: aynı D0 oturumunun iki turunda
13/15 kaydın tamamı `v=0, nt=c00000b5` ve `FAIL=13/15`. Bu 8 baytlık tanı
okuması hatasıdır; sıfır raw/SYSMSG alanları veri değildir. Kısa kimlik/CC/
opcode okumaları çalışıyor. CC `80/00 -> 00/00`, SOURCE/geçiş `0`, OTG `0/0`,
MUIC `3f`; xHCI Started/RUN fakat PORTSC CCS/PED `0`. `findstr` eksikliği
nedeniyle boş USB/SetupAPI özetleri negatif kanıt olarak kullanılamaz.

`.29` kaynak değişiklikleri:

- HSI2C, hazır RX/TX FIFO verilerini her baytta uyumadan tüketir/doldurur.
  Aynı 100 ms sınırında eski kodun 7/8 baytta timeout verdiği gecikmeli FIFO
  modeliyle yeni kod doğrulandı. Bu model cihazın ölçülmüş timer periyodu
  değildir. D0 boyunca kalıcı son RX hata status/uzunluk/ilerleme alanları var.
- Gerçek SOURCE sonrası Linux sırası düzeltildi: VBUS/OTG, ardından MUIC
  USB_AP. Önceki MUIC-before-VBUS kapısı Linux'a ait değildi. Kaynak pin
  durumundan rol üretmez; başarısız VBUS işleminden sonra MUIC açılmaz.
- `MAX FOTO v2`: geçersiz ham veri `OKUNAMADI`, RX_FAIL/NT/WANTED/READ görünür.
  Yeni `USB FOTO v2` sayfası güncel port ve gerçek USB/HID PnP kimliklerini
  okur; restart/rol komutu göndermez. Genel + USB + olay sayfaları 20 saniyede
  bir döner, tam 64 olayla tur 160 saniyedir; klavye/TXT gerekmez.
- Eksik `findstr` yerine mevcut kontrol aracına `filter` komutu eklendi.
  Eşleşmeme ve okuma hatası ayrı gösterilir. Tam registry dökümündeki 86
  ayrı süreç bir key sorgusuna indirildi; kısa özet alanları korundu.

**Henüz tamamlanmış sayılmayacak işler:** gerçek HSI2C/MAX IRQ işleme,
CC/PD veri/güç rolü bildirimi, MAX sonucuna bağlı DWC3/xHCI başlama/durma ve
runtime PM. Kaynak incelemesi “IRQ bilgisi yok” ifadesini düzeltti: birleşik
DT'de HSI2C SPI82 -> GSIV114; MAX GPA1_6 -> SPI19 -> GSIV51 bağlantısı var.
Mevcut ACPI/driver bunları sahiplenmiyor; EINT/pinctrl/ack ve Windows interrupt
yaşam döngüsü olmadan numara eklemek yeterli değil. BYN0002 hâlâ statik HOST;
`_DEP` yalnız PnP bağımlılığı, MAX READY/SOURCE bildirimi değildir.

MAX `0.1.0.29`, parent/host `0.1.0.18`, binary olay şeması v1, foto ekranı v2.
Mac'te Windows/UEFI/WIM build yapılmadı. WSL'deki güncel repo kökünde:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
tar -tf out/uefi-boot.tar
tar -tf out/system-esp.tar
```

Aynı turdan boot.img ve system.img flaşlanır. Önce `v=1`, `nt=0`, `RX_FAIL=0`
aranır. USB başarı ölçütü gerçek SOURCE=2, OTG=1/1, MUIC=09, port bağlantısı,
Logitech `VID_046D&PID_C52E`, ilgili HID ve fiziksel fare hareketidir. Bu
ölçütler henüz gerçekleşmiş değildir. Kaynak düzeltmesi USB çalıştı diye
raporlanmamalıdır; açık aşamalar tam Linux akışının gereğidir.

Yerel doğrulama: `tests/test_max77705_transport.py`,
`tests/test_max77705_connection.py`, `tests/test_max77705_events.py`,
`tests/test_b2lusb_log_filter.py`; mevcut Windows MAX/USB ve UEFI USB güvenlik
testleri. Donanımsız C testleri WDK/WinPE ve telefon doğrulamasının yerine geçmez.

### 0.20 5 Eylül: `.30` HSI2C45 gerçek IRQ portu

Aktif hedef değişmedi: özgün USB akışı, özgün IRQ ve gerçek rol/güç olayları.
Bu commit HSI2C45 aktarım kesmesini uygular; hedefin tamamlanması değildir.
Son cihaz kanıtı `.28` fotoğrafları; `.29/.30` henüz WSL/cihazda doğrulanmadı.

- ACPI BYN0004'e özgün SPI82 -> GSIV114 (`0x72`) eklendi. Kaynak yoksa,
  farklıysa veya edge/MSI ise driver başlangıcı hata verir.
- RX/TX polling ve TX preload kaldırıldı. Samsung'un RX almost-full/trailing,
  TX almost-empty, TRANSFER_DONE ve 0xf00 state grubu kullanılır. FIFO'yu
  gerçek ISR işler; DPC pasif bekleyiciyi uyandırır. Eski DPC veya hazır FIFO
  gerçek kesme gelmeden başarı oluşturamaz.
- IRQ bekleme sınırı Samsung kaynağındaki 1000 ms'dir. IRQ tamamlanınca DT'nin
  scl-clk-stretching INIT kontrolü 100 ms uygulanır. IRQ'suz fallback yoktur.
- Saat/pin/adapter hazırlığı D0Entry'de, istemci işlemleri IRQ açıldıktan
  sonra başlar. OTG/MUIC kapanışı IRQ kapatılmadan önce tamamlanır; hatalar
  döndürülür. ReleaseHardware'da I2C işlemi yapılmaz.
- MAX `.30`, `MAX FOTO v3`, yeni I2C_GSIV/IRQ/DONE alanları; genel sayfa
  32 satır. DONE sayacı hatalı tamamlanmaları da içerir; STATUS ile okunur.

Gerçek C ISR/DPC/transfer ve PnP/power callback testleri; kayıp/yanlış kesme,
partial RX, eksik DONE, geç DPC, NACK, clock-stretch ve kapanış hatalarını
kapsar. CC/olay/foto/log testleri ile mevcut kaynak güvenlik kontrolleri de
geçti. Mac'te Windows, UEFI veya WIM build yapılmadı.

WSL'de normal yöntem değişmedi:

```bash
bash yap.sh --touch --usb-test --clean
bash cache_esp.sh --win --system --baseline
```

ACPI ve SYS/EXE/startnet birlikte değişti: aynı turdan boot.img/system.img
kullanılmalı. İlk cihaz kanıtı I2C_GSIV=72, artan gerçek I2C_IRQ, geçerli
snapshot ve sıfır transfer hatasıdır. USB için ayrıca gerçek CC/PD attach,
VBUS/MUIC, rol bildirimi, port, enumeration, veri aktarımı ve detach gerekir.

Sıradaki kaynak işi MAX GPA1_6 EINT'dir. Özgün pinctrl'de bank offset 0x20,
EINT offset 0x04, CON/MASK/PEND 0x704/0x904/0xa04; pin6 bitleri ve level-low
+ one-shot Linux isteği doğrulandı. Mevcut opcode bekleyicisi BusLock tutuyor;
MAX IRQ'yu aynı kilide bağlamak deadlock oluşturur. EINT mask/ack ve gerçek
MFD/opcode yanıt dispatch/kilit düzeni birlikte uygulanmalı. Ayrıntılı kaynak
bağlantıları Linux karşılaştırmasının `.30` bölümündedir. Statik HOST ve MAX
polling son durum olarak kabul edilmiyor; hedef aktif kalır.

## TARİHSEL ARŞİV

Aşağıdaki bölümler projenin UEFI/Windows/UFS araştırma geçmişini korur. Ölçüm
kanıtları değerlidir; ancak “güncel durum” ve komutlar için yukarıdaki bölüm 0
esas alınmalıdır.

## 1. Hedef ve donanım

Samsung Galaxy S10+ (SM-G975F) üzerinde **Windows on ARM** çalıştırmak.

| | |
|---|---|
| Cihaz kod adı | `beyond2lte` |
| SoC | Samsung Exynos 9820 (S5E9820) |
| CPU | 2× Mongoose M4 + 2× Cortex-A75 + 4× Cortex-A55 |
| RAM | 8 GB, dört banka: `0x80000000`, `0x880000000`, `0x900000000`, `0x980000000` |
| Depolama | UFS 2.1 |
| Ekran | 1440×3040, DECON/DSIM/DPP hattı |
| Bootloader | Samsung S-Boot, kilidi açık |
| TrustZone | Samsung TEEgris, EL3'te |

Kısıtlar:

- **Ek donanım yok.** UART, JTAG, USB debug kablosu, hepsi baştan elendi.
- Android bölümlerinin kaybı kabul edilebilir.
- Cihaz Odin (indirme modu) ile flaşlanıyor; her deneme bir flaş turu.

---

## 2. Mimari: parçalar nasıl birleşiyor

```
S-Boot (Samsung, imzali, degistirilemez)
   |
   +-- EL3'e TEEgris yuklu, PSCI 0.1 sunuyor
   |
   v
boot.img yerine UEFI  (Mu-Silicium / Project Mu / EDK2)
   |
   +-- bizim overlay'imiz: bellek haritasi, ACPI tablolari, tani surucileri
   |
   v
UEFI Shell -> startup.nsh
   |
   +-- B2LGOSTR.EFI  (tani ekrani)
   +-- Windows Boot Manager -> winload.efi -> ntoskrnl
   |     ve icinde bizim b2lfb.sys surucumuz
   +-- ya da vmlinuz-lts (Linux kontrol deneyi)
```

Windows **RAM diskten** çalışıyor: `boot.wim` bellege yükleniyor, WinPE
oradan koşuyor. Diske kurulum henüz denenmedi.

---

## 3. Depo düzeni

```
UEFI_beyond2lte/
├── yap.sh                   UEFI derleme (WSL). Overlay'i Mu-Silicium'a kopyalar,
│                            ACPI derler, imaj + Odin .tar uretir
├── cache_esp.sh             ESP imaji uretir. --win Windows, --linux Linux,
│                            --system buyuk boluma yazar
├── kaydet.sh                git commit + push (Mac'te calisir)
│
├── mu-silicium-overlay/     UEFI kaynak katmanı (Mu-Silicium uzerine kopyalanir)
│   └── Platforms/Samsung/beyond2ltePkg/
│       ├── beyond2lte.dsc            platform tanimi
│       ├── Include/DXE.inc           firmware'e giren surucu listesi
│       ├── Library/MemoryMapLib/     BELLEK HARITASI - en kritik dosya
│       ├── Applications/
│       │   ├── B2LGoster/            tani ekrani (buyuk puntolu)
│       │   └── EbsTest/              ExitBootServices sonrasi kalp atisi
│       └── Drivers/
│           ├── B2LRtHookDxe/         UEFI calisma zamani servis kancalari
│           ├── B2LSehDxe/            EL3 Smart Exception Handler + PSCI sorgusu
│           ├── B2LGicSusDxe/         ExitBootServices'te GIC SPI susturma
│           ├── B2LResetHookDxe/      (kullanim disi)
│           └── B2LEl2Dxe/            (kullanim disi, hic calismadi)
│
├── silicium-acpi-overlay/SoCs/Samsung/S5E9820/
│   ├── DSDT_minimal.dsl     CPU1-7 _STA=0, UFS0 dugumu kapali
│   ├── APIC.dsl             MADT, GICv2, UID 1-7 devre disi
│   ├── FACP.dsl             Hardware Reduced, PSCI Compliant=1
│   ├── GTDT.dsl             mimari zamanlayici
│   └── PPTT.aml             tools_pptt_gen.py uretiyor (iasl degil)
│
├── windows/
│   ├── b2lfb/b2lfb.c        WINDOWS CEKIRDEK SURUCUSU - asil olcum araci
│   ├── bcd_olustur.cmd      BCD uretimi (Windows'ta calisir)
│   └── startnet.cmd         WinPE acilis betigi
│
├── tools_b2lfb.sh           b2lfb derle + imzala + WIM'e enjekte et
├── tools_winpe_surucu.sh    Start=0 acilis surucileri (liste/sadece/aralik)
├── tools_winpe_sistem.sh    Start=1 sistem servisleri (liste/sadece/kapat/ac)
├── tools_wim_sikistirma.sh  boot.wim sikistirmasini degistirir
├── tools_winboot.sh         Windows acilis dosyalarini ESP'ye kurar
├── linux_test/              Linux kontrol deneyi (initramfs + init)
├── dtb-analiz/              stok DTB (dts) + cikarilan initcall listeleri
├── stockfirmware/           Samsung fabrika firmware'i (.tar.md5)
├── esp/                     ESP icerigi (startup.nsh, boot.wim, EFI/...)
│
└── belgeler: STATUS.md (4100 satir gunluk), STOK_ACILIS.md,
             HARDWARE.md, BUILD.md, PARTITIONS.md, ORTAM.md, ...
```

---

## 4. Ölçüm araçları — bunlar olmadan hiçbir sonuç yorumlanamaz

Bu projede ilerlemenin tamamı, **her şeyi ölçülebilir hale getirmekten** geldi.
Kullanılan kanallar:

### 4.1 `b2lfb.sys` — Windows çekirdeğinin içinden

WDK olmadan, elle yazılmış imzalarla derlenen bir çekirdek sürücüsü.
`clang -target aarch64-windows-msvc` + `lld-link /driver`, `osslsigncode`
ile kendi sertifikamızla imzalanıyor, BCD'de `testsigning` açık.

Yaptıkları:

- `0xCA000000`'daki framebuffer'a doğrudan yazıyor (görsel kanıt)
- `0xCF030000`'a **sıcak sıfırlamayı aşan** ASCII kayıt yazıyor:
  - `TIK=nnnn` — çekirdek kaç 100 ms yaşadı
  - `BUG=H/E` — bugcheck oldu mu (doğrulanmış geri çağrı)
  - `SPN=xxxxxxxx` — boşta döngüsünün toplam tur sayısı
  - `MAX=xxxxxxxx` — bir tikteki en büyük tur sayısı (serbest hız)
- Öncelik 1'de sonsuz dönen bir iş parçacığı: CPU'nun boşa düşmesini engelliyor
- Bugcheck geri çağrısı: yeşil bant + saniye başına bir kare

`MAX` neden kritik: `SPN` tek başına anlamsız. `SPN/MAX` bizim döngünün
kaç tik CPU aldığını verir, `TIK - SPN/MAX` de **Windows'un kendi CPU
süresini**. Duvar saati DVFS yüzünden oynuyor, bu sayı oynamıyor.

### 4.2 `B2LGoster.efi` — UEFI tarafında büyük puntolu tanı ekranı

Konsolun yazı tipi 8×19'a sabit ve büyütülemiyor (`EFI_GLYPH_WIDTH` MdePkg'de
sabit). Bu uygulama glifleri HII font protokolünden alıp her pikseli 4×4 blok
olarak framebuffer'a basıyor, ayrıca onaltılık döküm yerine **yorumlanmış**
satır yazıyor.

Gösterdikleri: RAM/banka doğrulaması, EL3 SEH durumu, PSCI sorgu sonuçları,
bir önceki açılışın `TIK/BUG/SPN/MAX` değerleri, `RST_STAT`, GIC bekleyen
kesme sayısı, bellek tarama sonucu.

### 4.3 `EbsTest.efi`

`ExitBootServices` çağırıp **hiçbir şey kurmayan** bir döngü. Saniyede bir
kare çiziyor (mimari zamanlayıcıyla, meşgul döngüyle değil). "Sıfırlama
Windows'a mı özgü" sorusunu cevaplıyor.

### 4.4 Linux kontrol deneyi

Alpine 6.18 aarch64, `acpi=force` ile **bizim ACPI tablolarımızdan** açılıyor,
`linux_test/init` CPU'yu sürekli yakıp `/dev/kmsg`'e sayaç basıyor.

### 4.5 USB ile tanı — neden şu an bir kanal değil

Stok Android DTB'sinde Exynos DWC3 denetleyicisi `otg` olarak tanımlı ve
Linux'ta sürücüsü var. Bu, **UEFI'nin USB kullanabildiği anlamına gelmez**:
overlay'in DSC/FDF yapılandırmasında DWC3, xHCI, USB bus veya USB kütle
depolama DXE sürücüsü yoktur. Çalışan tek depolama yolu UFS `BlockIO`'dur.

Bu yüzden "USB'ye log yazan küçük bir sürücü" tek başına mümkün değildir.
Önce aşağıdaki alt katmanların çalışması gerekir:

1. DWC3 + USB PHY'nin saat, reset ve register başlatması,
2. MAX77705 üzerinden güvenli Type-C/OTG host rolü seçimi,
3. USB bus enumerasyonu ve kütle depolama `BlockIO`,
4. FAT/SimpleFileSystem üzerinden dosya yazma.

Bu zincir ancak `ExitBootServices` öncesindeki UEFI kayıtlarını USB belleğe
yazabilir. Mevcut 15 saniyelik Windows sıfırlamasında UEFI kodu artık
çalışmadığından, sıfırlama anında dosyaya yazma sağlamaz.

Klasik Windows USB kernel-debug (`KDUSB`) bunun kısayolu değildir: hedefte
xHCI Debug Capability (DbC) bulunan bir xHCI denetleyicisi ve özel USB 3.0
debug kablosu ister. Exynos DWC3 OTG için böyle bir DbC desteği doğrulanmış
değildir; ayrıca bu platformda PCI/xHCI aygıtı yoktur.

### 4.6 KDNET-EEM: ayrı bir Windows USB aygıt-modu olasılığı

Bu, klasik `KDUSB` ile karıştırılmamalıdır. Microsoft'un ARM için sunduğu
`KDNET-EEM-USB`, hedefte bir **Synopsys USB 3.0 dual-role controller**'ın
USB-C portuna bağlı olmasını ve firmware'in denetleyiciyi ACPI `DBG2`
tablosunda bir debug aygıtı olarak bildirmesini ister. ARM'de BCD
`busparams`, PCI BDF değil, bu tablodaki debug-device dizinidir.
([KDNET-EEM gereksinimleri](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-kernel-mode-debugging-over-usb-eem-arm-kdnet), [DBG2 biçimi](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table))

Bu cihaz için donanım adayı **olumlu ama henüz yeterli değildir**: stok DTB'de
`synopsys,dwc3`, `dr_mode = "otg"`, `0x10C00000/0x10000` denetleyici alanı,
USB2/USB3 PHY bağları ve etkin Exynos USBDRD PHY bulunur. Dolayısıyla EEM,
"imkânsız xHCI DbC" değil, araştırmaya değer bir sonraki nesil debug kanalıdır.

Burada kimlik seçimi kritik bir güvenlik sınırıdır. Microsoft'un ARM örneğindeki
`kd_8003_5143.dll` ve `DBG2` subtype `0x5143`, genel Synopsys DWC3 sürücüsü
değildir; `0x5143` Qualcomm'un PCI vendor kimliğidir ve bu inbox modül Qualcomm
USB denetleyicilerini destekler. Exynos/Samsung DWC3 için `0x5143` yayınlamak,
Windows'a yanlış donanım sözleşmesi vermek olur. PCI olmayan bir DBG2 aygıtının
taşıyıcı adı `kd_<PortType>_<PortSubtype>.dll` biçimindedir. Mevcut taşıyıcı
Exynos'u gerçekten desteklemiyorsa Samsung/Exynos'a ait, Microsoft ile ayrılmış
bir subtype ve buna karşılık gelen özel KDNET extensibility modülü gerekir.
Modülün tek açık export'u `KdInitializeLibrary` olmalı, hiç import'u olmamalı ve
kesmeler kapalıyken kilitsiz/tek iş parçacıklı çalışmalıdır.

Bugün kullanılamaz; etkin ACPI yalnız temel tabloları/UFS0'ı bildirir, `DBG2`
tablosu ve DWC3 aygıt-modu düğümü yoktur; WinPE PnP raporu da USB aygıtı
görmemektedir. Üstelik `kdnet.exe` ayarının hedefte Windows içinden bir kez
yazılması gerekir; giriş aygıtı/uzak kanal yokken bunun BCD'ye çevrimdışı
yerleştirilmesi ayrıca tasarlanmalıdır. Bu nedenle UFS Code 10 turuna
karıştırılmayacak.

EEM için güvenli, ayrı bring-up sırası şudur:

1. Stok Linux'tan DWC3/PHY saat-reset/Type-C device-role sırasını çıkar;
   UFS MMIO veya mevcut ACPI tanımlarını değiştirme.
2. Hedef WIM'de `UfxSynopsys.sys`, `Ufx01000.sys` ve `GenericUSBFn.sys`
   bulunduğunu doğrula ve desteklenen aygıt-modu kimliği olmadan yükleme yapma.
3. `DBG2` tablosını RSDT/XSDT'ye ekle ve ilk EEM girişinin dizinini sabitle.
   Bu madde yalnız aşağıdaki controller/transport doğrulamasından sonra
   uygulanır; mevcut buildde `DBG2` eklenmez.
4. WinPE'de `kdnet.exe` ile desteklenen USB controller/busparams çıktısını
   doğrula. Inbox taşıyıcı Exynos'u tanımıyorsa Microsoft ile ayrılmış doğru
   subtype ve özel KDNET modülü tamamlanmadan ilerleme; `0x5143` kullanma.
   Ancak bundan sonra test BCD'sine KDNET anahtarını yaz.

Bu dört koşulun herhangi biri yoksa host bilgisayara standart USB-C kablosu
takmak bir kernel debug bağlantısı oluşturmaz.

**Karar:** USB yığını, ayrı ve büyük bir UEFI portudur; 15 saniyelik
sıfırlamayı teşhis etmek için şimdi başlatılmayacak. Mevcut güvenli kanal
`b2lfb.sys`'in sıcak sıfırlamayı aşan RAM kaydı ve sonraki UEFI açılışında
onun ekrana çıkarılmasıdır. Daha fazla veri gerekirse bu kayıt genişletilir;
Windows'a yeni riskli callback kancaları eklenmez.

---

## 5. Ölçülerek elenenler

Aşağıdakilerin hiçbiri sıfırlamanın sebebi **değil**. Her satır bir ölçüme
dayanıyor.

| Eleme | Nasıl ölçüldü |
|---|---|
| Platform / firmware / ACPI tabloları | Linux aynı tablolarla açıldı, 170 saniye CPU yaktı, sıfırlanmadı |
| Bellek: banka beyanı | Harita 4 banka, tepe `0xA00000000`, toplam 8 GB — DTB ile uyumlu |
| Bellek: gerçeklik | Her bankanın sonuna farklı desen yazılıp hepsi sonradan okundu (aliasing dahil) |
| Bellek: korumalı bölge | 7.75 GB'ın tamamı (2.032.139 sayfa) gezildi, tek SPI kalkmadı |
| Kesme fırtınası | `ExitBootServices`'te bütün SPI'lar kapatıldı, `ISENABLER` geri okundu (`ACIK=0`), sonuç değişmedi |
| GIC güvenlik grupları | `IGROUPR` okundu, `GRUP1=0` — güvenlik uzantısı yok, engelleme yok |
| EL3 Smart Exception Handler | Windows'un kendi sanal adresiyle kaydedildi (`SANAL=0 KABUL`), hiç istisna gelmedi |
| Donanım watchdog | `WTCON` okundu, bit5 kapalı |
| EL3 kilitlenme dedektörü | `GET_LOCKUP_REASON` hem güç açılışında hem sıfırlama sonrasında `1` — sebep kaydı değil |
| Boşta kalma / PSCI CPU_SUSPEND | Öncelik 1 döngüsü 11 milyon tur döndü, CPU hiç boşa düşmedi, sıfırlama yine oldu |
| Isınma / güç | `EbsTest` meşgul döngüde 30+ saniye yaşadı |
| Servis bekleyen her şey | `EbsTest` hiçbir şey kurmadan yaşıyor — periyodik servis isteyen hiçbir mekanizma suçlu olamaz |
| Windows `ResetSystem` | Kanca sayacı 0; `GetVariable` 365 giriş / 365 çıkış (sağlıklı) |
| Windows sürümü | 23H2 ve 25H2 aynı davranıyor |
| `bootuxdisabled`, `sos` | Ayarlar doğrulandı; `sos` Win10/11'de büyük ölçüde işlevsiz |

---

## 6. Ölçülen olgular — açıklanması gereken tablo

1. **Windows çekirdeğinin ömrü seçili hizmet kümesiyle birlikte değişebiliyor,
   fakat tek açılış belirleyici değil.** `Npfs` tek açıkken bir açılış
   63.902 saniye yaşadı; aynı kümenin, kullanıcı-modu damgası eklenmiş sonraki
   açılışı 8.802 saniyede sessiz sıfırlandı. Her iki yolda da
   `RST_STAT = 0x20000000` (yazılım sıfırlaması), DRAM hayatta kalıyor.

2. **Bugcheck yok.** `BUG=H`. Geri çağrı iki yönde de doğrulandı (gerçek bir
   bugcheck olduğunda `BUG=E` ve yeşil bant çıkıyor).

3. **Windows `ResetSystem`'i çağırmıyor**, PSCI kapalıyken bile.

4. **Önceki kısa-yol ölçümlerinde duvar saati oynak, Windows CPU süresi
   tutarlıydı:**

   ```
   TIK=100  MAX=5.82M/100ms  SPN=12.6M   -> Windows CPU  9.78 s
   TIK=104  MAX=5.92M/100ms  SPN=21.4M   -> Windows CPU 10.04 s
   TIK=148  MAX=18.9M/100ms  SPN=870.8M  -> Windows CPU 10.20 s
   ```

   Üç yapılandırma tamamen farklı sürücü kümesine sahip. `MAX`'ın üç kat
   değişmesi CPU frekansının (DVFS) değiştiğini gösteriyor. Duvar saatindeki
   bütün oynaklığın sebebi bu ve boşta geçen süre.

5. **Fren deneyi modeli doğruluyor:** kalp atışı DPC'sine her tikte yarım
   saniye meşgul bekleme eklendi (CPU'nun yarısı), duvar saati 10 → 24
   saniyeye çıktı, Windows CPU süresi ~12 saniyede kaldı.

6. **`EbsTest` 30+ saniye yaşıyor** — `ExitBootServices` sonrası hiçbir şey
   kurmadan.

7. **Linux 170 saniye CPU yakıyor** — aynı UEFI, aynı ACPI tabloları, EL1,
   tek çekirdek, 8 GB görüyor.

8. **Sürücü kümesi küçültülünce Windows kullanıcı moduna varıyor** ve
   `0xc000021a` (SYSTEM_PROCESS_TERMINATED) veriyor — yani `smss.exe`
   çalışmış. Bu, ölüm noktasının geçildiği anlamına geliyor.

9. **PSCI 0.1.** Stok DTB:

   ```
   psci { compatible = "arm,psci"; method = "smc";
          cpu_suspend = <0xc4000001>; cpu_off = <0x84000002>;
          cpu_on = <0xc4000003>; };
   ```

   `"arm,psci"` yani 0.1 — `PSCI_VERSION`, `PSCI_FEATURES`, `SYSTEM_RESET`,
   `SYSTEM_OFF`, `AFFINITY_INFO` **yok**. Linux açılışta
   `PSCIv65535.65535 detected` yazıyor (yani `0xFFFFFFFF` dönüyor).
   FADT'deki `PSCI_COMPLIANT` biti sürümü adlandırmaz; yalnızca PSCI'nin
   uygulandığını beyan eder. Ancak ARM'nin PSCI 0.2 uyumluluk tablosunda
   `VERSION`, `AFFINITY_INFO`, `SYSTEM_OFF` ve `SYSTEM_RESET` zorunludur;
   stok monitor bunların hiçbirini sunmuyor. Biti kapatınca Windows
   `winload`'da duruyor, çekirdek hiç başlamıyor.

10. **Açılışta 8 SPI bekliyor**, UEFI kesmeleri hiç açmadığı halde:

    ```
    SPI  39  exynos-adv_tracer     APM posta kutusu
    SPI  43  speedy@15970000       PMIC veriyolu
    SPI 183  decon_f@19030000      ekran denetleyici
    SPI 184  decon_f@19030000      ekran denetleyici
    SPI 190  dsim@19080000         MIPI DSI
    SPI 198  dpp@19071000          ekran islemcisi
    ```

    S-Boot ekran hattını açılış logosu için çalışır bırakıyor. Kapatıp
    temizlesek de bitler anında geri geliyor (kaynaklar canlı). Ama
    kapatılmış kesme CPU'ya iletilmiyor ve sonuç değişmiyor.

---

## 7. Şu anki tıkanma noktası

> **Güncellik notu (14 Ağustos 2026):** bu bölüm ve altındaki deney
> zincirleri tarihsel kayıttır. WinPE kabuğu artık açılıyor ve DiskPart
> çalışıyor; güncel tıkanma UFS disk numaralandırmasında. Bkz. bölüm
> sonundaki "b2lufs: adaptör başlıyor ama disk numaralandırılmıyor".


**Windows, seçili hizmet kümesi ve açılış zamanlamasıyla birlikte kullanıcı
modu öncesinde ya da sonrasında sessizce sıfırlanıyor. Ne olduğu bilinmiyor.**
Normal kısa yol yaklaşık 10 saniye. `Npfs` tek açıkken bir kez 63.902 saniyeye
ulaştı, fakat sonraki aynı hizmet kümesi 8.802 saniyede kısa yola döndü.

Önceki kısa-yolun CPU-süresi ölçümü:

- sürücü kümesinden bağımsız
- CPU frekansından bağımsız
- duvar saatinden bağımsız
- boşta kalmayla ilgisiz (CPU hiç boşa düşmüyor)

Ve şunları tetiklemiyor:

- Linux'ta (170 saniye tam yük)
- `EbsTest`'te (30+ saniye meşgul döngü)

Yani sayaç yalnızca **Windows koşarken** işliyor.

### Geçerli ilk deney: temiz taban

Bu sonucu veren her açılışta kendi tanı kodumuz da vardı. Son commit ile
`B2LRtHookDxe`, `B2LSehDxe` ve `B2LGicSusDxe` `DXE.inc` içinden çıkarıldı;
`tools_b2lfb.sh kapat` da Windows sürücüsünü `Start=4` yapıyor.

Bir eksik daha vardı: normal `startup.nsh`, `B2LGOSTR.EFI`'yi otomatik
çalıştırıyor. Bu yalnızca ekran yazısı değil; bütün `EfiConventionalMemory`
sayfalarını yaz/oku/geri-yaz tarıyor. Windows'tan önce bitse de buna
"aletsiz" demek doğru değildi.

Bu yüzden ilk flash için:

```bash
bash yap.sh
bash tools_b2lfb.sh kapat
bash cache_esp.sh --win --system --baseline
```

`--baseline`, imajın içinden `B2LGOSTR.EFI`/`EBSTEST.EFI`'yi çıkarır ve
yalnızca `bootmgfw.efi`yi çağıran `startup-baseline.nsh`'i `startup.nsh`
olarak koyar. UFS/PWM ve Watchdog ayarlarını değiştirmez: bu turdaki tek
değişken B2L ölçüm araçlarıdır.

### Temiz taban sonucu ve sıradaki deney (12 Ağustos 2026)

Temiz taban da yaklaşık 10 saniye sonra **sıfırlanıyor**. Böylece b2lfb,
`B2LGoster`, `B2LRtHookDxe`, `B2LSehDxe` ve `B2LGicSusDxe` sıfırlamanın
nedeni değildir. `wpeutil shutdown` içeren `startnet.cmd` de çalışmıyor:
Microsoft'a göre bu komut WinPE oturumunu kapatır; cihazın kapanmak yerine
yeniden başlaması kullanıcı kabuğuna hiç varılmadığını gösterir.

En güçlü kalan yapısal aday, Windows'a PSCI beyan edip yalnızca PSCI 0.1
sunmamızdır. `--psci-hvc` bunun için **varsayılanı değiştirmeyen**, tek
amaçlı bir deney ekler:

```bash
bash yap.sh --psci-hvc
bash tools_b2lfb.sh kapat
bash cache_esp.sh --win --system --psci-hvc
```

Bu mod FADT'nin `PSCI_USE_HVC` bitini denetimli biçimde açar ve sadece
PSCI HVC çağrılarını alan küçük bir EL2 köprüsü koyar. Eski, bütün SMC'leri
tuzaklayan `HCR_EL2.TSC` deneyi **kullanılmaz**. Köprü PSCI 0.2 `VERSION`,
`AFFINITY_INFO` ve `MIGRATE_INFO_TYPE` yanıtlarını üretir; stokta var olan
`CPU_SUSPEND`/`CPU_OFF`/`CPU_ON` çağrılarını EL3'e iletir. `SYSTEM_OFF` ve
`SYSTEM_RESET` çağrılarını ilk turda `NOT_SUPPORTED` ile döndürüp HVC
kaydına bırakır: eksik sistem güç işlemlerini uydurmak bu turun amacı değil.

İlk HVC açılışında `PSCI HVC KAYDI YOK` görünmesi zorunlu olarak beklenir:
`B2LGoster`, kayıt tamponunu **Windows başlatılmadan önce** okur. Bu ekranda
bir tuşa basıp `bootmgfw.efi`yi çalıştır; telefon yeniden açıldığında ikinci
`B2LGoster` ekranında kayıt okunur. İkinci ekranda şu sonuçlar yorumlanır:

- `PSCI HVC KOPRUSU` + `HVC SAYI` sıfırdan büyük: Windows gerçekten HVC
  yolunu kullandı; `SON PSCI` hangi işlevde kaldığını gösterir.
- `PSCI HVC KAYDI YOK`: EL2 yükü çalışmadı ya da firmware FADT HVC bitini
  kullanmadı; sonuç PSCI hipotezini doğrulamaz.
- Cihaz WinPE kabuğuna kadar gelirse sonraki ayrı turda `startnet.cmd`
  `cmd.exe /k` ile kalıcı kabuğa çevrilir.

İlk uygulamada ikinci açılışta da `PSCI HVC KAYDI YOK` görüldü. Bu, EL2
yükünün başlangıçta yazdığı `B2HV` imzasının da oluşmadığı anlamına gelir;
henüz HVC/PSCI çağrısı hakkında sonuç çıkarılamaz. Sonraki aynı deney,
`HVC DXE DURUMU` ile iki olasılığı ayırır: kayıt yoksa sürücü FV'de/çalışmada
değildir; `EL2 SMC = 2 / GERI` görülür ama `B2HV` yoksa Samsung VMM arka
kapısı yükümüze atlamamış veya dönüşte HYP durumunu geri almıştır. `VMM X0` ve
`VMM X1` bu SMC'nin ham dönüşleridir.

Bu ayrım 12 Ağustos 2026'da ölçüldü:

```
EL2 SMC = 00000002  GERI
VMM X0 = FFFFFFFF
VMM X1 = CF000800
PSCI HVC KAYDI YOK
```

Yani DXE sürücüsü ve SMC dönüşü çalışıyor, fakat `0xC2000400` özel yük
isteğini `-1` ile reddediyor; `x1` de giriş adresi olarak değişmeden kalıyor.
`B2HV` imzası hiç yazılmadığı için payload'a EL2'de hiç dallanılmadı. Bu,
önbellek temizliği veya vektör kodu hatası değil, 9820'de S-Boot dışından
özel EL2 yükünün kabul edilmediğine dair doğrudan ölçümdür. `--psci-hvc`
PSCI 0.2 köprüsü kuramaz; bu deney tekrar edilmemeli ve normal SMC modu
kullanılmalıdır.

### Sonraki deney: `seclog` altı 1 GiB + çıplak ADK WinPE

EL2 gözlem yolu reddedildiği için sonraki tek değişken fiziksel bellek
aralığıdır. Stok DTB'nin `seclog` bölgesi `0xC0000000`'da başlar; DRAM ise
`0x80000000`'dan başlar. Bu nedenle 1 GiB deneyi `truncatememory`
değerini `0x40000000` yapamaz: o değer gerçek DRAM'in tamamını keser.
Doğru üst fiziksel sınır **`0xC0000000`**'dır ve Windows'a yalnızca
`0x80000000..0xBFFFFFFF` aralığını bırakır.

742 MB'lık ISO WinPE bu alanda güvenilir bir test değildir. `tools_winpe_adk.sh`,
ADK'nin yaklaşık 250 MB'lık çıplak ARM64 WinPE'sini kullanır; `guvenli1g`
BCD varyantı da tek çekirdeği zorlayıp bu fiziksel sınırı yazar. `startnet.cmd`
artık kapatma istemez, `cmd.exe /k` ile kabuğu açık bırakır. Sonuçlar:

- WinPE kabuğu görünür veya reset eşiği anlamlı biçimde ileri giderse,
  `0xC0000000` ve üstü bellek/firmware etkileşimi tekrar açılır.
- Yükleyici "yetersiz bellek" ile açık hata verirse imaj daha da küçültülür;
  sessiz resetle aynı şey değildir.
- Aynı sessiz reset sürerse bu bellek aralığı hipotezi kapanır.

ADK'nin `winpe.wim`i WIM başlığında salt-okunur işaretli gelirse normal dosya
kopyası yeterli değildir: `wimlib-imagex update` SYSTEM hive'ını eklemeyi
reddeder. Araç, kaynağı değiştirmeden tek indeksi `export --boot --compress=LZX`
ile yeni ve yazılabilir `esp/sources/boot.wim`e aktarır; ardından hive ve
`startnet.cmd` eklenir.

İlk `guvenli1g` ölçümünde `Loading files` tamamlandıktan sonra logo göründü
ve logo kararıp cihaz **15 saniye** sonra sıfırlandı; `cmd.exe /k` kabuğu
görünmedi. Bu, düşük bellek için açık bir yükleyici hatası değildir, fakat
henüz bellek hipotezini kapatmaz: aynı turda hem ADK WinPE hem de fiziksel
sınır değişti. Karşılaştırma zorunludur. Aynı ADK Media ile yalnızca
`bcd_olustur.cmd tekcpu` üretilecek, `windows/BCD` kopyalanacak ve
`tools_winpe_adk.sh` tekrar çalıştırılacaktır; UEFI yeniden derlenmez.

- Bu karşılaştırma da 15 saniyede aynı reseti verirse `0xC0000000` sınırı
  etkisizdir ve bellek-aralığı hipotezi kapanır.
- Süre belirgin biçimde kısalır ya da uzarsa sınır tahsis zamanlamasını
  değiştiriyordur; sonraki turda yalnız bellek sınırı basamaklı daraltılır.

Karşılaştırma tamamlandı: **aynı ADK Media ve aynı WinPE WIM** ile yalnızca
BCD `tekcpu` varyantına dönüldü (`truncatememory` yok). `Loading files`
sonrasında UEFI logosu yine göründü ve cihaz yine **15 saniye** sonra
sıfırlandı; `cmd.exe /k` kabuğu görünmedi. Böylece `0x80000000..0xBFFFFFFF`
ile sınırlama, `seclog` başlangıcı (`0xC0000000`) ve üst bellek bankaları bu
ölçekte sebep değildir. `azram`, `guvenli1g`, reserved-memory veya bellek
sınırı denemeleri tekrar edilmemeli; yeni bir bellek hipotezi için farklı,
adres-özgül bir kanıt gerekir.

Bu temiz A/B'nin ardından normal SMC akışında yazılımla denenmemiş doğrudan
bir düzeltme kalmadı: Windows'un ihtiyaç duyduğu PSCI 0.2 arayüzü stok EL3'te
yok, onu sağlayacak HVC köprüsünün EL2 yük isteği de 9820'de `-1` ile
reddedildi. Sonraki anlamlı ilerleme yeni bir BCD/WIM varyantı değil;
Windows'un son SMC/erken-kernel davranışını dışarıdan görebilen UART gibi bir
gözlem kanalı veya Samsung'un imzalı uH/EL3 arayüzü hakkında yeni, cihaz-özel
kanıttır.

### Sıradaki güvenli ayrım: boot aygıt başlatma sınırı

UART/USB yokken Windows'un içinden yeni görüntü veya süreç bildirimi kancası
eklemek doğru değildir: ikisi de önceki `b2lfb` denemelerinde `0x1E` bugcheck
üretmiş ve sinyal vermemiştir. Bunun yerine `b2lfb`, Windows'un belgelenmiş
`IoRegisterBootDriverReinitialization` yaşam döngüsü noktasını kullanır. I/O
Manager bunu **bütün aygıtları enumerate edip başlattıktan sonra**, PASSIVE_LEVEL
bir sistem thread'inde çağırır.

Sürücü, sıcak sıfırlamayı aşan `0xCF030000+0x70` alanına şunu yazar; sonraki
normal UEFI açılışında `B2LGoster` bunu büyük puntolu gösterir:

```
BRI=REG C=000    callback kaydedildi, henüz çağrılmadı
BRI=0010 C=001   aygıt başlatma tamamlandı; o anda TIK=10 (= 1.0 saniye)
```

Bu turda **normal SMC konduiti** kullanılır; `--psci-hvc`, bellek sınırı,
başka BCD varyantı ve `--baseline` kullanılmaz. `--baseline`, B2LGoster'i
imajdan çıkardığı için kaydı gösteremez.

1. Windows'ta yönetici Komut İstemi: `windows\bcd_olustur.cmd tekcpu`.
   Çıkan `BCD` dosyasını WSL'de `windows/BCD` üzerine kopyala.
2. WSL'de sırasıyla:

   ```bash
   cd ~/beyond2lte-uefi
   bash yap.sh --acpi
   bash tools_winpe_adk.sh "/mnt/c/Program Files (x86)/Windows Kits/10/Assessment and Deployment Kit/Windows Preinstallation Environment/arm64/Media"
   bash tools_b2lfb.sh
   bash cache_esp.sh --win --system
   ```

3. Üretilen normal UEFI, `system-esp.tar` ve daha önce kullanılan tek-ESP
   düzeniyle flaşlanır. Cihaz sıcak sıfırlanınca **elle kapatma yapmadan**
   sonraki `B2LGoster` ekranının fotoğrafı alınır.

Yorum:

- `BRI=REG C=000`: Windows, tüm aygıtları başlatma çizgisine ulaşmadan
  sıfırlanıyor; odak PnP/ACPI aygıt başlatma penceresinde kalır.
- `BRI=nnnn C=001`: o çizgi geçildi; `nnnn` ile son `TIK` arasındaki fark,
  sıfırlamanın aygıt başlatmadan sonraki ne kadar işte geldiğini ölçer.
- `B2LFB KAYDI YOK`: bu bir Windows sonucu değildir; sürücünün WIM'e girişi
  veya imza/yükleme doğrulaması önce çözülmelidir.

#### Sonuç: PnP tamamlandı; QPC, sıfırlamanın 43.002 s sonra geldiğini doğruladı

Bu deneyde sonraki tanı ekranı şunu gösterdi:

```
TIK=0420 BUG=H
MAX=011AE4D0
SPN=5C04AB23
BRI=0001 C=001
RST_STAT=20000000
```

`BRI=0001 C=001` geçerli ve güçlü bir sonuçtur: I/O Manager, boot sürücüsü
girdisinden sonraki ilk 100 ms tikinde aygıtları enumerate edip başlatma
sınırını geçmiş. Bu nedenle önceki Start=0/Start=1 blokaj merdiveninden
çıkan "PnP/ACPI sayımı on saniye sürüyor" yorumu artık geçerli değildir.
Sıfırlama bu noktadan **çok sonra** gelmektedir.

İlk ölçümden sonraki, doğru fotoğraf şu tam değerleri verdi:

```
TIK=0430 BUG=H
MAX=012D6FE2
SPN=5EB7DF42
BRI=0001 C=001
QPD=42A42A3A F=018CBA80
RST_STAT=20000000
```

`QPD=0x42A42A3A = 1,118,054,970` ve
`F=0x018CBA80 = 26,000,000 Hz`; dolayısıyla
`QPD / F = 43.002114 s`. Bu, `TIK=0430` ile tutarlıdır: DPC ortalaması
`100.005 ms` olmuştur. Yani tik sayacı duvar zamanından kopmamış; reset
`b2lfb`'nin `DriverEntry` anından yaklaşık **43 saniye sonra** gerçekleşmiş.
Önceki "logo sonrası 15 saniye" gözlemi aynı başlangıç anını ölçmüyordu.

`SPN / MAX = 80.440917` tik, yani boşta-engelleyici iş parçacığı yaklaşık
`8.044092 s` CPU aldı. Kalan yaklaşık `34.958022 s` Windows işi veya onun
beklemeleridir. PnP damgası ilk 100 ms'de geldiği için bu uzun pencere PnP
aygıt başlatmanın kendisi değildir.

### Sıradaki güvenli ayrım: Service Control Manager auto-start aşaması

Yeni callback, thread veya bildirim kancası eklemek yerine iki ayrı, çok
küçük sürücü kullanılır:

- `b2lfb.sys` (`Start=0`), her açılışta `0xCF030000+0xA0` alanını
  `AUT=REG C=000` olarak başlatır.
- `b2lauto.sys` (`Start=2`) yalnızca aynı alana `AUT=SCM C=001` yazar ve
  döner. Timer, framebuffer, PnP veya callback kullanmaz.

Microsoft'un sürücü yükleme sırası belgelerine göre `Start=2` auto-start
sürücüleri, boot/system-start sürücüler ve PnP'den sonra Service Control
Manager tarafından başlatılır. Bu nedenle kayıt yorumu nettir:

```
AUT=REG C=000   reset, SCM'nin auto-start sürücü aşamasından önce oldu
AUT=SCM C=001   Service Control Manager b2lauto.sys'yi başlattı
```

İkinci satır, WinPE'nin SCM/kullanıcı-modu başlangıç zincirine ulaştığını
gösterir; fakat `cmd.exe` kabuğunun çalıştığını tek başına kanıtlamaz. İlk
satırda ise `b2lauto.sys` WIM'e ve SYSTEM hive'a yazılıp geri okunarak
doğrulandığından, bir sonraki odak Start=1/SCM arasıdır.

İkinci, bağımsız tur da aynı sonucu verdi:

```
TIK=0427 BUG=H
MAX=01241608
SPN=5CBFDFA2
BRI=0001 C=001
QPD=422D5CB7 F=018CBA80
AUT=REG C=000
RST_STAT=20000000
```

Bu sefer `QPD/F = 42.702658 s` ve DPC ortalaması `100.006 ms`; önceki
42–43 saniyelik zaman aralığı yeniden üretildi. `AUT=REG C=000`,
`b2lfb`'nin yeni başlangıç damgası yazdığını fakat **SCM'nin auto-start
aşamasına ulaşılmadığını** gösterir. Böylece pencere PnP tamamlandıktan
sonra, fakat `Start=2` öncesindedir.

### Sıradaki ayrım: Start=1 sistem ilklendirmesi

Bu pencereyi ikiye bölmek için `b2lsys.sys` adlı ikinci bir minicik sürücü
eklendi. `Start=1`, `ErrorControl=0`, grup/dependency yoktur; yalnızca
`0xCF030000+0xB0` alanına yazar ve döner. Microsoft'un tanımladığı sıralama
gereği PnP aygıt sürücülerinden sonra kalan `SERVICE_SYSTEM_START`
sürücüleri yüklenir; bu aşama SCM auto-start'tan öncedir.

```
SYS=REG C=000   b2lsys'in DriverEntry'si çağrılmadı
SYS=INI C=001   Windows b2lsys'i çağırdı
```

Sonraki turda `AUT` ile birlikte yorumlanır:

| Kayıt | Anlamı |
|---|---|
| `SYS=REG`, `AUT=REG` | b2lsys ve SCM işaretçileri çağrılmadı; küresel yükleme fazı tek başına çıkarılamaz |
| `SYS=INI`, `AUT=REG` | b2lsys çağrıldı; SCM işaretçisi çağrılmadı |
| `SYS=INI`, `AUT=SCM` | iki işaretçi de çağrıldı |

`SYS=INI`, öteki bütün Start=1 sürücülerinin de yüklendiğini tek başına
kanıtlamaz; grup sırası bu sürücünün onlardan önce ya da sonra gelmesini
belirleyebilir. Bunun tersinin de doğru olduğu daha sonraki `0xc000021a`
sonucuyla görüldü: `SYS=REG`, sistemin genel olarak Start=1 veya kullanıcı
modu eşiğine gelmediğini kanıtlamaz; yalnız b2lsys'e sıra gelmediğini söyler.

Üçüncü turdaki ekran şöyledir:

```
TIK=0422 BUG=H
MAX=012D6F47
SPN=5C83BC07
BRI=0001 C=001
QPD=416693DF F=018CBA80
AUT=REG C=000
SYS=REG C=000
RST_STAT=20000000
```

`QPD/F = 42.201599 s`; 422 adet tik ile DPC aralığı yine yaklaşık
`100.004 ms`dir. Dolayısıyla yeni markerlar ölçüm süresini değiştirmedi.
`SYS=REG`, yalnızca **grupsuz** `b2lsys.sys`in `DriverEntry` noktasına hiç
ulaşılmadığını kanıtlar. Önceki Start=1 blokaj deneyi de bu sürücünün
grupsuz/en-sonraki konumda olmasına dayanıyordu; bu nedenle sonucu
"Start=1 evresi hiç başlamadı" diye okumak fazla güçlü olurdu.

### Sıradaki ayrım: sıralı Start=1 grubu ile grupsuz Start=1 sürücüsü

Windows, `SERVICE_SYSTEM_START` sürücülerinin yükleme sırasını `Group`
değeriyle de belirler. Yeni `b2lsg0.sys`, `Start=1` ve `Group="System Bus
Extender"` ile eklenmiştir; ana `b2lfb` de bu yerleşik grubu Start=0
aşamasında kullanır. `b2lsg0` hiçbir iş yapmadan `0xCF030000+0xC0` alanına
damga basar. Betik, WIM'den geri okuyarak hem Start değerini hem de Group
değerini doğrular.

| Kayıt | Dar yorum |
|---|---|
| `SYG=REG`, `SYS=REG`, `AUT=REG` | `b2lsg0` ve grupsuz `b2lsys`e sıra gelmedi |
| `SYG=INI`, `SYS=REG`, `AUT=REG` | sıralı sistem grubuna girildi; grupsuz işaretçiden önce reset |
| `SYG=INI`, `SYS=INI`, `AUT=REG` | iki Start=1 işaretçisi de geçti, SCM auto-start öncesinde reset |

Bu markerlar sürücüler arasındaki her iç sıralamayı tek başına kanıtlamaz;
ama önceki Start=1 blokajının bıraktığı grup-sırası belirsizliğini ölçülebilir
hale getirir.

#### Sonuç: iki Start=1 işaretçisi de gelmedi; önce yüklenebilirlik kontrolü

Sonraki fotoğrafta iki yeni damga da başlangıç değeriyle kaldı:

```
TIK=0442 BUG=H
MAX=012426EF
SPN=6A327BB0
BRI=0001 C=001
QPD=4485B884 F=018CBA80
AUT=REG C=000
SYS=REG C=000
SYG=REG C=000
RST_STAT=20000000
```

`QPD/F = 44.215931 s`; dolayısıyla bu tur önceki 42–43 saniyelik pencerenin
biraz uzun ama aynı karakterdeki tekraridir. `BRI=0001`, Microsoft'un
tanımladığı bütün aygıtların enumerate edilip başlatıldığı boot-reinit
sınırının yine ilk 100 ms'de geçildiğini gösterir. `SYG=REG` ve `SYS=REG`,
sırasıyla sıralı ve grupsuz test sürücülerinin `DriverEntry`lerine
ulaşılmadığını gösterir; `AUT=REG` de SCM auto-start'a geçilmediğini korur.

Ancak bu sonuç tek başına “Windows Start=1 aşamasına hiç girmedi” demek
değildir. WIM dosya özeti ve SYSTEM hive geri okumaları yalnızca dosya ve
servis kaydının doğru yerleştirildiğini kanıtlar; Start=1 konumundaki iki
yeni imajın gerçekten yüklenebilir olduğunun yürütme kanıtı değildir.

Bu ayrımı kapatmak için `bash tools_b2lfb.sh kontrol` eklendi. Bu kip, mevcut
imajları değiştirmeden yalnızca bir sonraki turda grupsuz `b2lsys`i
`Start=0`, `ErrorControl=3` yapar; `b2lsg0` ve `b2lauto`yu `Start=4` ile
devre dışı bırakır. Beklenen sonuç şudur:

| Kontrol sonucu | Dar yorum |
|---|---|
| `SYS=INI` | `b2lsys` imajı ve imza zinciri boot aşamasında çalışır; normal kipteki `SYS=REG` faz sınırı olarak yorumlanabilir. |
| Yükleyici hata ekranı | İmzalama/yükleme reddi vardır; ekrandaki kod izlenecek somut nedendir. |
| `SYS=REG` | `b2lsys`in çalıştırılabilirlik zinciri sorunludur; normal Start=1 sonucuna faz anlamı yüklenmez. |

Kontrolden sonra normal yapı için `bash tools_b2lfb.sh` çalıştırılır; bu hem
servis kayıtlarını hem de `ErrorControl=0` değerlerini normal deney haline
geri kurar.

#### Kontrol sonucu: `b2lsys` yüklenebilir; grup damgasının bu kipte negatif anlamı yok

Kontrol turunda ekran şunu verdi:

```
TIK=0426 BUG=H
MAX=012D43EC
SPN=5CD0F699
BRI=0001 C=001
QPD=42059E1B F=018CBA80
AUT=REG C=000
SYS=INI C=001
SYG=REG C=000
RST_STAT=20000000
```

`QPD/F = 42.602477 s`; süre yine yerleşik 42–44 saniye bandındadır.
`SYS=INI`, `b2lsys.sys`in dosya yolu, test imzası, ARM64 kodu ve
`MmMapIoSpace` ile kayıt yazma yolunun boot-start yüklemede gerçekten
çalıştığının yürütme kanıtıdır. Dolayısıyla normal kipte görülen
`SYS=REG`, yerleştirme ya da imza sorunu değil, Windows'un bu grupsuz
`SERVICE_SYSTEM_START` sürücüsünün `DriverEntry` noktasına ulaşmadığı
sonucunu artık güvenilir biçimde taşır.

Bu fotoğraftaki `SYG=REG` ters yönde kanıt değildir. Kontrolün eski
uygulamasında `b2lfb` ve `b2lsg0` ikisi de `Start=0`, aynı
`System Bus Extender` grubunda ve etiketsizdi. Windows, aynı grup içindeki
etiketsiz sürücülerin göreli sırasını garanti etmez. `b2lsg0` önce
`SYG=INI` yazmış, `b2lfb` daha sonra başlangıç damgası `SYG=REG` ile bunun
üzerine yazmış olabilir. Bu yüzden kontrol kipi güncellendi: sonraki
kontrollerde `b2lsg0` devre dışı kalır ve yalnız anlamlı `SYS` kontrolü
yapılır.

Şimdi normal deneye dönülür: `bash tools_b2lfb.sh` `b2lsys`i yeniden
`Start=1` yapar. Yeni normal turda `SYS=REG`, yalnız b2lsys çağrılmadı
anlamına gelir; yükleme evresine dair daha geniş bir iddia için bağımsız
kanıt gerekir.

#### Tüm özgün Start=1 hizmetleri kapalıyken ikinci yol: gerçek bugcheck

`tools_winpe_sistem.sh kapat` sonrasında, aynı yazılım imajıyla bir turda
yeşil bugcheck şeridi ve `0xc000021a` görüldü. Sonraki B2LGoster kaydı:

```
TIK=0086 BUG=E
MAX=005B2685
SPN=005B2685
BRI=0001 C=001
QPD=0D54EDBC F=018CBA80
AUT=REG C=000
SYS=REG C=000
SYG=REG C=000
RST_STAT=20000000
```

`QPD/F = 8.602680 s`; `TIK=0086` ile tutarlıdır. `BUG=E` tartışmasızdır:
yalnız `KeRegisterBugCheckCallback` bu baytı `E` yapar ve aynı callback
yeşil şeridi çizer. Bu tur, 42–44 saniye sonra görülen `BUG=H` sessiz
sıfırlamasından farklı, Windows çekirdeğinin farkında olduğu gerçek bir
bugcheck'tir.

`0xc000021a` `STATUS_SYSTEM_PROCESS_TERMINATED` demektir: Windows kritik
bir kullanıcı-modu alt sisteminin (örneğin CSRSS/Winlogon) sonlandığını
gördüğünde bu stop kodunu üretir. Aynı kayıtta `SYS=REG` kalması, b2lsys
işaretçisinin global Start=1 ya da kullanıcı-modu sınırı olmadığını
doğrudan kanıtlar. Bu nedenle bundan sonra `SYS=REG` üzerinden faz ismi
verilmeyecek.

Bu bugcheck artık yeniden üretilmeye çalışılmayacak. Tek gözlem şu iki
sonucu zaten yeterince kesinleştirir: Windows kullanıcı-modu başlangıcına
ulaşabiliyor ve **14 hizmetin tümünü kapatmak WinPE'nin kendi kritik
altyapısını bozuyor**. `0xc000021a` bu imajdaki platform hatasının doğrudan
kimliği değildir.

İlk ve en dar geri-dönüş deneyi yalnız `Npfs`'yi açık bırakır. `Npfs.sys`,
CSRSS'nin kullandığı named-pipe altyapısını sağlar; onsuz kritik süreçlerin
birbirleriyle konuşamaması `0xc000021a` için doğrudan ve beklenen bir
açıklamadır. Yeni `birak` modu önceki WIM durumundan bağımsızdır:

```bash
bash tools_b2lfb.sh
bash tools_winpe_sistem.sh birak Npfs
bash cache_esp.sh --win --system
```

Bu tur yalnız bir kez çalıştı; değerli fakat tek örnek olan şu kaydı verdi:

```
TIK=0639 BUG=H
MAX=012D3CFE
SPN=0D0B30AA
QPD=6307DBE3 F=018CBA80
```

`QPD/F = 63.902286 s`. `SPN/MAX = 11.085` tik olduğundan b2lfb'nin boşta
döngüsü yaklaşık 1.109 saniye CPU aldı; Windows'un kendisi yaklaşık **62.8
saniye CPU işi** yaptı. Bu, eski 8.6 saniyelik `0xc000021a` yolundan da,
önceki kısa sessiz resetlerden de ayrıdır. Dolayısıyla "sabit 10 saniyelik
sayaç", watchdog ve WFI/PMU zaman aşımı açıklamaları bu imaj için geçerli
değildir.

`0xc000021a` bu turda görünmedi. Bu, `Npfs` eksikliğinin kritik kullanıcı-modu
IPC'sini bozduğu hipotezini güçlendirir; fakat tek örnek nedenselliği
kesinleştirmez. `cmd.exe /k` görünmediği için 63.9 saniyelik resetin
`startnet.cmd` öncesinde mi, içinde mi olduğu da henüz bilinmiyordu.

Sonraki tek tur bunu ayırır. `tools_b2lfb.sh` artık `b2lcmd.exe` adlı 2 KB'lık
bir kullanıcı programını **ve onu çağıran güncel `startnet.cmd`yi** WIM'e
koyup ikisini SHA-1 ile geri doğrular. Program yalnız `Global\\B2LCmd` adlı
olayı oluşturur. Boot sürücüsü olayı düşük öncelikli sistem thread'inden
saniyede en fazla bir kez yoklar ve kalıcı kayda aşağıdakilerden birini yazar:

```
USR=REG C=000  b2lcmd olayi cekirdek surucusu tarafindan gorulmedi
USR=CMD C=001  Win32/startnet b2lcmd satirina ulasti (kabuk henuz degil)
```

Komutlar:

```bash
bash yap.sh                         # B2LGoster SURUM 27: USR satirini gosterir
bash tools_b2lfb.sh
bash tools_winpe_sistem.sh birak Npfs
bash cache_esp.sh --win --system
```

Bu sürümün sonucu şöyledir:

```
TIK=0088 BUG=H
QPD=0DA3E1E4 F=018CBA80     -> 8.801692 s
USR=REG C=000
```

Dolayısıyla ne `Npfs` ile 63.9 saniyelik yol kararlı çıktı ne de `USR=REG`
tek başına Startnet öncesini kanıtladı. Aynı durumu yeniden ölçmek yerine
sonraki tek değişiklik, temel kullanıcı/komut yolu adaylarından yalnız
`Null` hizmetini geri koymaktı. Bu, ağ, dosya sistemi veya ek sürücü
kümesini geri açmadan `Npfs`'ye tek hizmet ekledi:

```bash
bash yap.sh
bash tools_b2lfb.sh
bash tools_winpe_sistem.sh birak Npfs Null
bash cache_esp.sh --win --system
```

Sonuç yine kısa sessiz yoldu:

```
TIK=0090 BUG=H
QPD=0DF334DF F=018CBA80     -> 9.001638 s
USR=REG C=000
```

Bu, `Null`'ı tek başına çözüm adayı olmaktan çıkarır. Ancak bu sürümde
çekirdek, kullanıcı olayını yalnız saniyede bir yokluyordu; 9. saniyenin
sonunda başlayan `b2lcmd` sonraki yoklama gelmeden resetle kaçabilirdi.
Bu nedenle sonraki tur **hizmet kümesini değiştirmez**. Yalnız yoklama
aralığını 100 ms'ye indirir ve mevcut `Npfs + Null` SYSTEM hive'ını korur:

```bash
bash tools_b2lfb.sh
bash cache_esp.sh --win --system
```

Bu turda yalnız `system-esp.tar` **SYSTEM** bölümüne flaşlanır. `USR=CMD`
görülürse `startnet`in b2lcmd satırına ulaştığını; kabuğun açıldığını ise
yalnız ekrandaki WinPE isteminin görünmesi kanıtlar. `USR=REG` yine kalırsa,
100 ms çözünürlükte olayın hiç görülmediği kesinleşir; fakat olayı yaratan
Win32 çağrısının mı, yoksa Startnet yolunun mu eksik kaldığını ayırmak için
bir sonraki araç değişikliği gerekir.

100 ms yoklamalı tur da `USR=REG` bıraktı:

```
TIK=0086 BUG=H
MAX=003AA7B0
SPN=003AA7B0
BRI=0001 C=001
QPD=00546BA6 F=018CBA80
AUT=REG C=000
SYS=REG C=000
SYG=REG C=000
USR=REG C=000
RST_STAT=20000000
```

Bu, `b2lcmd.exe` olayı üç saniye açık kalsa bile Windows'un onu gören
çekirdek iş parçacığına ulaşamadığını gösterir. `BRI=0001`, PnP aygıt
başlatma çizgisinin ilk 100 ms'de tamamlandığını tekrar doğrular; `SYS`,
`SYG`, `AUT` ve `USR` ise sırasıyla sistem-başlangıç gruplarına, SCM
auto-start'e ve `startnet.cmd` içindeki kullanıcı koduna geçilmediğini
gösterir. Bu yüzden WinPE hizmet kümesini tekrar değiştirip aynı sınırı
ölçmek doğru sonraki adım değildir.

Bu turdaki `QPD/F = 0.213 s`, 86 adet 100 ms DPC tikiyle açıkça çelişir.
Önceki turlarda iki kanal birbirini doğrulamış olsa da, bu örnekte QPC
değeri kesintisiz zaman tabanı değildir; onunla yeni bir süre veya neden
çıkarımı yapılmaz. `TIK=0086` de DPC gecikirse gerçek duvar zamanını
olduğundan küçük sayabilir. Bu nedenle güvenli çıkarım yalnız şudur:
çekirdek PnP çizgisini geçmiş, fakat kullanıcı/SCM çizgilerine gelmeden
bugcheck vermeden yazılımsal sıfırlamaya gitmiştir.

### PSCI yerine "MP parking" protokolünü taklit etme sınırı

Windows'un PSCI desteklemeyen Arm platformları için tanımladığı alternatif,
UEFI'den Windows'a sürekli bilgi dönen bir protokol değildir. Firmware
`ExitBootServices()` öncesinde ACPI MADT'de her işlemcinin 4 KiB mailbox
fiziksel adresini verir. Sonra Windows, AP'yi başlatmak istediğinde mailbox'a
işlemci kimliği ve giriş adresini yazar; çekirdekler firmware'in önceden
kurduğu WFI/interrupt döngüsünde o mailbox'u izleyip verilen adrese dallanır.
Bu işlem `ExitBootServices()` sonrasındadır; sıradan bir DXE sürücüsü orada
Windows ile araya girip istek cevaplayamaz.

Mevcut ACPI bunun parking sürümü değildir: `APIC.dsl`de CPU1--CPU7 kapalı,
her GICC için `Parking Protocol Version = 0` ve `Parked Address = 0`dır.
FADT'deki PSCI biti kapatılıp sahte bir mailbox adresi vermek tek başına
işe yaramaz; her AP'nin gerçekten park edilmiş, aynı koherens alanında ve
GIC kesmesiyle uyanabilir olması gerekir. Exynos'ta bu, S-Boot/EL3'ün
ikincil çekirdek ve güç denetimine sahip olmayı gerektirir. Mevcut
`0xC2000400` özel EL2 yük isteği bunu vermediğinden, bu protokolü yalnız
DXE kodu ekleyerek taklit etmek yüksek olasılıkla winload'da daha erken
durmaya yol açar; flash turu harcamaya değmez.

Dolayısıyla PSCI çağrılarını gerçekten araya almak için gereken şey DXE
değil, Windows sonrası SMC/HVC geçişini yakalayabilecek çalışan EL2 ya da
EL3 kodudur. HVC yükünün reddedildiği deney bu yolun şimdilik kapalı
olduğunu gösterir. Kaynaklar: [Microsoft'un Windows Arm UEFI
gereksinimi](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms),
[Microsoft MP startup belirtimi](https://cdrdv2.intel.com/v1/dl/getContent/772757).

Kaynaklar: [Microsoft — AddService StartType](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addservice-directive),
[Microsoft — driver load order](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/specifying-driver-load-order),
[Microsoft — 0xc000021a](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0xc000021a--winlogin-fatal-error).

### 9820 S-Boot / uH araştırma notu (12–13 Ağustos 2026)

Stok `G975FXXSGHWC1` paketinin kendi `sboot.bin` ve `uh.bin` dosyaları
doğrudan incelendi. Bu, internetten çıkarım değil cihazda kullanılacak aynı
BL/UH ikilisinin ikili kanıtıdır.

- `UH` PIT'te `BOOT`tan ayrı 2 MiB bölümdür. Projenin Odin paketi yalnız
  `BOOT`u değiştirir; `S-BOOT`, `UH` ve `UHCFG` olduğu gibi kalır. Bu,
  U-H'nin S-Boot'taki yükleme yolunun kullanılabilir kaldığını kanıtlar;
  fakat bu yolun kilidi açık/custom `BOOT` yolunda gerçekten çağrılıp
  çağrılmadığı ayrı bir sorudur ve aşağıda çevrimdışı incelenir.
- `uh.bin`, 4 KiB'lik `GREENTEA` kapsül başlığı, `0x42388` baytlık AArch64
  ELF ve sonrasında 0x310 baytlık imza malzemesinden oluşur. S-Boot önce
  kapsül boyutunu, `GREENTEA` sihrini ve ECDSA imzasını denetler. Bu yüzden
  dosyayı veya özel bir payload'ı doğrudan bu alana kopyalamak geçerli bir
  U-H yükleme deneyi değildir.
- ELF'nin yük adresi `0x87000000`, entry point'i `0x87001000`, bellek sonu
  `0x87054000`dır. Başlangıçta yalnız `CurrentEL == EL2` kabul edilir; aksi
  halde `x1=0xffff` ile çıkış yoluna gider. Kod `VBAR_EL2 = 0x8701B800`
  kurar, EL2 vektörlerini kullanır ve stack alanı `0x87046000`a uzanır.
- Stok S-Boot yükleme çağrısı tamamen çözüldü:

  ```text
  dsb sy
  x0 = 0xC2000400       # VMM giriş kapısı
  x1 = 0x87001000       # U-H entry point
  x2 = 0x00002000       # çekirdek başına EL2 stack offset'i
  x3 = 1                # AArch64 kipi
  x4 = 0x87000000       # U-H image base (EL2 x0)
  x5 = 0x00100000       # ayrılmış U-H alanı (EL2 x1)
  smc #0
  isb
  ```

  U-H dönmek istediğinde `x0=0xC2000401`, `x1=status`, `smc #0` kullanır.
  Böylece önceki özel denemede görülen `x0=-1, x1=0xCF000800` sonucu artık
  daha dar yorumlanır: çağrı numarası doğruydu, fakat S-Boot'un imzalı U-H
  akışına ait adres/alan sözleşmesi sağlanmadı ve kendi payload'ımız EL2'ye
  hiç girmedi. Bu HVC payload deneyi kapalı kalır; yeniden denenmez.

#### Yeni, tek değişkenli düzeltme: stok U-H belleğini Windows'tan koru

`MemoryMapLib` bu alanı eskiden `EfiBootServicesCode` (`BsCode`) olarak
veriyordu. Bu tür `ExitBootServices()` sonrasında Windows'un tahsis
havuzuna geri dönebilir. Oysa aynı fiziksel sayfalarda çalışan U-H EL2
vektörü, kodu, verisi ve stack'i vardır. Windows'un bu sayfalardan birini
ilerleyen tahsislerde ezmesi, bugcheck üretmeden EL2'nin cihazı sıfırlaması
için somut ve zamanlamayla uyumlu bir mekanizmadır.

Bu yüzden tüm mevcut 2 MiB aralık (`0x87000000..0x871FFFFF`), `MEM_RES` /
`EfiReservedMemoryType` olarak bildirildi. Stok yükleyici yalnız 1 MiB
istese de mevcut iki MiB'lik platform rezervini daraltmak için kanıt yoktur.
Bu, önceki geniş bellek sınırı denemelerinden farklıdır: doğrudan stok
ikilisinin çalışan EL2 adreslerine dayalı, geri alınabilir tek bir
harita değişikliğidir.

Kontrol: ilk B2LGoster ekranında `RAM MB` eski `00001F29` yerine iki MiB
eksik olan `00001F27` görünmelidir. U-H belleğine UEFI'den ek bir okuma
yapılmaz: EL2'nin bu sayfaları koruyor olma ihtimali, salt-okunur olsa bile
testin tek değişkenini bozabilir. `RAM MB` koşulu görülmeden Windows'a
geçilmemeli. Görülürse yalnız temiz ADK WinPE ile bir açılış yapılır; önceki
sürücü, WIM, PSCI veya bellek-sınırı denemeleri bu turda tekrar edilmez.

#### Sonuç: U-H sayfalarının Windows tarafından ezilmesi elendi

Koruma uygulandıktan sonra üç ardışık açılışın tanı ekranı `RAM MB=00001F27`
gösterdi; yani iki MiB gerçekten Windows'un tahsis edebileceği RAM'den çıktı.
Windows'tan sonraki üçüncü ekranda ölçüm sürücüsünün geçerli kaydı da oluştu:

```text
TIK=0104 BUG=H
MAX=00B30A43
SPN=09D8E9D5
BRI=0001 C=001
QPD=101F37F8 F=018CBA80
AUT=REG C=000
SYS=REG C=000
SYG=REG C=000
USR=REG C=000
RST_STAT=20000000
```

`QPD/F = 10.403131 s`. `BRI=0001 C=001`, Windows'un PnP/boot aygıt
başlatma sınırını geçtiğini yeniden kanıtlar. Buna rağmen Start=1, SCM ve
WinPE kullanıcı-modu için yerleştirilen işaretçilerin hiçbiri çalışmamış ve
PMU yine yazılım reseti bildirmiştir. İlk iki ardışık açılışta `B2LFB KAYDI
YOK` görüldüğü için, o iki ekran Windows'un hangi faza ulaştığı hakkında
sonuç vermez; kaydın yokluğundan ek bir neden çıkarılmaz.

Dolayısıyla **U-H'nin 0x87000000 sayfalarının Windows tahsisiyle ezilmesi
ana neden değildir.** Rezerv doğru platform davranışı olduğu için geri
alınmaz. Bu sonuç, çalışan U-H/EL3'ün başka bir politika ihlaliyle reset
verdiği olasılığını tek başına elemez; fakat bu imajla aynı bellek-deneyi
tekrar edilmez. Sonraki çevrimdışı iş, S-Boot'ta U-H yükleyicisinin çağrı
koşulunu ve U-H'ın Windows öncesi gerçekten etkin olup olmadığını ayırmaktır.

#### Sonuç: cihazdaki `UHCFG` boş; ayar kapısı yok

`UHCFG`, PIT'te `UH`dan ayrı, 4 MiB'lik bölümdür. Stok BL paketinde bu bölüm
dosyası yoktur. Cihazdan salt-okunur alınan kopya `/dev/block/sda11` ile
eşleşti; boyutu `4194304` bayt ve hem cihazda hem çekilen dosyada SHA-256
değeri şudur:

```text
bb9f8df61474d25e71fa00722318cd387396ca1736605e1248821cc0de3d3af8
```

Dosyanın bütün 4 MiB'si `0x00`dır: sihir, kapsül, metin, imza ya da
etkinleştirme bayrağı yoktur. Dolayısıyla `UHCFG`den U-H'yi güvenle kapatan
bir ayar çıkarılamaz; bu bölüme yazmak için hiçbir gerekçe yoktur.

`UH` cihazda `/dev/block/sda10`dur ve bölümün tamamının SHA-256 değeri
`61cd66d5e24076fec67bd632c8d283b5f0001ca9fac712af138a091e1f2eb801`dir.
Bu, stok paketten açılan `uh.bin` ile cihazdan alınan kopyanın ilk `0x43698`
baytında karşılaştırıldı. Sonuç:

- Kapsülün `GREENTEA` başlığı ile `0x1000..0x43388` aralığındaki bütün
  `271240` baytlık AArch64 ELF yürütme kodu **byte-for-byte aynı**dır.
- Farkların tamamı `0x43388..0x43698` imza altbilgisindedir. Stok dosya
  `G975FXXSGHWC1` / `62768724R` / `20230302103951`, cihazdaki dosya ise
  `G975FXXSGHWC2` / `62822459R` / `20230303171316` sürümünü taşır; ECDSA
  imzası bu nedenle farklıdır.
- `0x43698..0x200000` bölüm sonu tamamen sıfırdır. Burada ikinci bir kapsül,
  ayar veya eski veri yoktur.

Yani U-H'nin incelediğimiz EL2 kodu cihazdaki kodla aynıdır. Buna karşılık
S-Boot'u HWC1 paketinden çözdük, cihazdaki U-H ise HWC2 imzalıdır; S-Boot'un
custom `BOOT` yolunda U-H yükleme çağrısını hangi koşulda yaptığı konusunda
HWC1'i HWC2 diye varsayamayız.

#### Sıradaki güvenli veri: cihazdaki `S-BOOT`un salt-okunur kopyası

PIT, `BOOTLOADER` bölümünü `sboot.bin` olarak tanımlar: UFS aygıt türünde,
başlangıç blok `0`, uzunluk `1024` blok, yani 4 MiB'dir. Recovery'deki
`/dev/block/by-name` listesinin `sboot` veya `bootloader` içermemesi hata
değildir: bu liste yalnız kullanıcı LUN'undaki GPT bölümlerini (`sda1…`)
gösterir. `BOOTLOADER` ise ayrı bir ham UFS boot LUN'udur.

Bu nedenle `/dev/block/sdb`yi tahminen seçmek doğru değildir. Önce bütün
görünen ham LUN'ların boyutu ve salt-okunur durumu okunur; 4 MiB olan aday
buradan belirlenir. İlk üç alma işlemi yalnız okumaydı; bu işlem de aynıdır.
`sboot`, `uh`, `uhcfg`, `boot` veya `vbmeta`ya yazılmaz ve Odin kullanılmaz.

Custom recovery içindeki `adb shell` ile Windows PowerShell'de çalışacak
komutlar:

```powershell
$adb = "C:\Users\orhun\OneDrive\Masaüstü\projectfiles\platform-tools\adb.exe"
& $adb devices
& $adb shell 'for x in sda sdb sdc sdd sde; do printf "%s bytes=" "$x"; blockdev --getsize64 "/dev/block/$x"; printf "%s ro=" "$x"; cat "/sys/block/$x/ro"; done'
```

Bu komut **yalnız okur**; çıktı gelmeden `dd` çalıştırılmaz. 2026-08-13
cihaz çıktısı şudur: `sda=127943049216`, `sdb=4194304 ro=1`,
`sdc=4194304 ro=1`, `sdd=8388608 ro=1`, `sde=8388608 ro=1`. Buna göre
`sdb` ve `sdc` iki adet 4 MiB boot-LUN adayıdır ve ikisi de donanımsal
salt-okunurdur. İlk 64 bayt iki boot kopyasında da aynı olabileceği için
hangisinin çalışma anında seçildiği yalnız başlıkla tahmin edilmez; iki aday
da salt-okunur biçimde alınır, sonra hash ve içerik karşılaştırılır.

Aşağıdaki komut yalnız `if=/dev/block/...` yönünde okur; `uh`, `uhcfg`,
`boot` veya bir GPT bölümüne erişmez:

```powershell
& $adb shell 'for x in sdb sdc; do dd if=/dev/block/$x of=/tmp/sboot-$x-G975F.img bs=4M; wc -c /tmp/sboot-$x-G975F.img; sha256sum /tmp/sboot-$x-G975F.img; done'
& $adb pull /tmp/sboot-sdb-G975F.img "$env:USERPROFILE\Desktop\sboot-sdb-G975F.img"
& $adb pull /tmp/sboot-sdc-G975F.img "$env:USERPROFILE\Desktop\sboot-sdc-G975F.img"
Get-FileHash "$env:USERPROFILE\Desktop\sboot-sdb-G975F.img" -Algorithm SHA256
Get-FileHash "$env:USERPROFILE\Desktop\sboot-sdc-G975F.img" -Algorithm SHA256
& $adb shell 'rm /tmp/sboot-sdb-G975F.img /tmp/sboot-sdc-G975F.img'
```

Her dosyanın beklenen boyutu tam `4194304` bayttır. `adb pull`dan önce ve
sonra görünen SHA-256, her dosya için aynı olmalıdır. Her `dd`nin yönü
özellikle `if=/dev/block/$x` olup `of=/tmp/...`dir; tersine çevrilmez.
Dosyalar alındıktan sonra ikisi de HWC1 `sboot.bin` ile karşılaştırılır;
yalnız HWC2'nin gerçek U-H yükleme/çağırma koşulu sonuç için kullanılacaktır.
Yazma veya bölüm değiştirme için ayrıca, eldeki ikili kanıtın belirlediği bir
gerekçe gerekir.

Kaynaklar: stok `BL_G975FXXSGHWC1...tar.md5` içindeki `sboot.bin.lz4` ve
`uh.bin.lz4`; [Samsung Exynos KVM arayüzü incelemesi](https://github.com/sleirsgoevy/exynos-kvm-patch),
[aynı S10 stok açılış günlüğü](https://gist.github.com/Piecuuu/390f776533c7fddc1278de3d1e2612bc).

#### Cihazdaki iki UFS boot LUN'unun sonucu (13 Ağustos 2026)

Recovery'den yalnız-okunur olarak iki 4 MiB aday alındı. İndirme öncesi ve
sonrası boyutlar tam `4194304` bayttır. Yanlış ilk `adb pull` yolu hiçbir
dosyaya erişemeden hata verdi; ardından doğru iki geçici dosya ayrı ayrı
alındı. Bu olay cihaza yazma yapmadı.

| LUN | SHA-256 | İkili kanıt | Sonuç |
| --- | --- | --- | --- |
| `/dev/block/sdb` | `b5dbc38ef77e67bc7a6b4ca772231f7a33baa101f2074626464be26d959c670c` | AArch64 başlangıç kodu, `G975FXXSGHWC2`, `f_load_hypervisor` ve U-H yükleme dizeleri | Cihazın güncel HWC2 S-BOOT imajı |
| `/dev/block/sdc` | `3fe6dcf6d313bde8d5ff4be7ede95f32bb4d3c053e63141c4021a277bf21f62e` | İlk 64 bayt sıfır; yalnız eski `G975FXXS9DTK9` imza metadatası kalmış | Eski/boş ikinci boot kopyası; analiz hedefi değildir |

`sdb`nin HWC2 kimliği, cihazdan daha önce alınan U-H footer'ındaki HWC2
kimliğiyle tutarlıdır. Böylece HWC1 stok ikilisinden çıkarılan U-H yolu,
artık cihazın gerçek S-BOOT sürümünde de denetlendi. HWC2'de:

- `f_load_hypervisor` dosya ofseti `0x15c688`dir. Bu ham LUN imajından
  çıkarılan **tek güvenli adres** budur; aşağıdaki yükleme/yer değiştirme
  notu nedeniyle çalışma adresi artık kesin diye yazılmaz.
- SMC sarmalayıcısı `0x15c618`dedir; `dsb sy; smc #0; isb; ret` biçimindedir.
- U-H yükleyici, HWC1'deki gibi aynen
  `x0=0xC2000400`, `x1=0x87001000`, `x2=0x2000`, `x3=1`,
  `x4=0x87000000`, `x5=0x100000` ile bu sarmalayıcıyı çağırır.
- HWC1 ile `0x15c618..0x15c8e8` arasındaki fark sadece 11 talimattaki
  tanı metni/tablo adresi kaymasıdır. Çağrı argümanları, doğrulama akışı,
  `smc` ve dönüş yolu aynıdır. Küçük `0x0f9008` sarmalayıcısı da bayt
  bayt aynıdır.

Bu, "HWC2 U-H için farklı bir SMC ABI kullanıyor" varsayımını kapatır.

#### HWC2 S-BOOT dispatch sınırı: ham imaj neyi kanıtlıyor, neyi kanıtlamıyor

İlk çözümlemede dosya ofsetine sabit bir DRAM tabanı eklenmişti. Bu doğru
değildir. İmajın erken önyükleme kodu `+0x10`da `0x02020018`e dallanır ve
`+0x70`deki literal `0x02023AAC`tır; bunlar platformdaki `0x02020000` IRAM
bölgesiyle uyumludur. Dört MiB'lik ana S-BOOT evresinin tamamı IRAM'a
sığamayacağı için daha sonra DRAM'a taşınır, fakat hedef adres/taşıma
koşulu salt ham LUN'dan henüz çıkarılmamıştır. Bu yüzden `+0x15c688` gibi
dosya ofsetleri kullanılmalı, türetilmiş "çalışma adresleri" kullanılmamalıdır.

Statik çağrı taraması aşağıdaki dar sonucu verir:

- `+0x0f9008`deki kısa sarmalayıcı önce boş bir hazırlık yordamını, ardından
  `f_load_hypervisor`ı çağırır.
- Bu sarmalayıcıya imajda doğrudan bir `b`/`bl` çağrısı veya onun adresini
  içeren sabit 32/64 bit işaretçi yoktur. Bu **çağrılmadığı anlamına gelmez**;
  yalnız doğrudan çağrı grafiğinin burada bittiğini söyler.
- `+0x0f9024`deki genel yürütücü, sıfır-sonlu bir işaretçi listesinde `blr`
  ile dolaşır. Liste imajda `+0x21FE40`dadır ve görünen ilk girdileri
  `0xC9054F68`, `0xC9003434`, `0xC9004350` … şeklindedir. Bunların hepsi
  projenin UEFI FD için yapılandırdığı `0xC9000000` penceresindedir. Bu
  adreslerin o anda S-BOOT'a mı, yüklenmiş boot imajına mı ait olduğu ham
  LUN tek başına ayırt edilemez.
- U-H sarmalayıcısının statik adresi bu listede yoktur. Liste başka bir
  önyükleme moduna ait olabilir veya çalışma sırasında değiştiriliyor olabilir;
  imajda bu listeyi dolduran doğrudan atama da bulunmadı.

Dolayısıyla bu imaj, U-H ABI'sini ve doğrulama/yükleme yordamını kanıtlar;
ama custom `BOOT` yolunda U-H'nin gerçekten çağrıldığını ya da atlandığını
kanıtlamaz. Mevcut ekran ölçümleri UEFI'nin EL1'de çalıştığını, HVC yükünün
reddedildiğini ve U-H bellek rezervinin reseti çözmediğini zaten gösteriyor.
Bu nedenle sonuç bir S-BOOT/U-H/UHCFG yazma veya yeni HVC flash deneyi için
gerekçe değildir. Yeni bir doğrulama, canlı S-BOOT/EL3 gözlemi ya da çağrı
seçimini etkileyen bir boot yapılandırmasının **salt-okunur** kanıtını
gerektirir.

Bu sonuçla HVC payload'ı, U-H belleği rezervi veya aynı Windows açılışları
yeniden denenmez.

#### Düzeltme: FD/stack çakışması ve açık kalan DTB rezervleri

Aktif `BOOT`, son üretilen UEFI imajıdır; cihazdan tekrar alınması bu aşamada
yeni bir kanıt üretmez. S-BOOT'taki `0xC9054F68`, `0xC9003434` ve
`0xC9004350` genel çağrı-listesi girdilerinin hepsi proje FD tabanı
`0xC9000000` içindedir. Mu-Silicium'un BootShim kaynağı, konfigurasyondaki
`FD_SIZE` kadar baytı bu tabana kopyalayıp doğrudan oraya dallandığını
gösterir. Konfigürasyonda `FD_SIZE=0x400000` iken eski bellek haritası yalnız
iki MiB `UEFI FD` beyan ediyor ve CPU stack'ini `0xC9200000`a, yani çalışan
FD kopyasının ikinci yarısına koyuyordu.

Bu bir Windows tahsis deneyi değil, doğrudan firmware yerleşim hatasıdır:

- FD `0xC9000000..0xC9400000` olarak 4 MiB beyan edilir.
- Birincil CPU stack `0xC9400000..0xC9440000`a taşınır.
- `tools_uefi_layout_check.py`, configteki FD, MemoryMapLib descriptorü,
  DSC stack PCD'si ve framebuffer çakışmasını derleme öncesi doğrular.

Windows'un korumalı RAM'e dokunup dokunamayacağı ayrıca statik olarak
denetlendi. Stok DTB'deki `/reserved-memory` altında sabit `reg` ile
tanımlanan **23** aralığın önceki haritada 7'si Windows'a tahsis edilebilir
görünüyordu: `secure_camera`, `camera_contig`, `camera`, `modem_spare`,
`modem_if`, `cp_ram_logging` ve ikinci DRAM bankındaki `sec_rdx_bootdev`.
İlk ikisi açıkça `ion,secure`; diğerleri de Linux'un ayırdığı sabit fiziksel
alanlardır. Bunların hepsinin TrustZone tarafından erişim-ihlalinde reset
üreteceği henüz kanıtlanmış değildir; fakat Windows'a verilmemeleri kesindir.

Yedi bölge ve kalan 16 sabit bölge, artık kapsayıcı `EfiReservedMemoryType`
aralıklarıyla korunur. `yap.sh`, `tools_reserved_memory_audit.py --check`
çağrısıyla sabit DTB bölgesinin tek bir tanesi bile açık kalırsa derlemeyi
flaştan önce durdurur.

Dinamik bölgeler için [beyond1lte/Exynos 9820 Linux açılış günlüğü](https://gist.github.com/kreatoo/874e618a706c1ac758fc9795af9fc780), ABOX'un
`0xDC400000..0xE0000000` içine, genel CMA'nın ise en üst DRAM bankına
yerleşebildiğini somut olarak gösterir. DTB'nin `ion_video_stream` penceresi
de düşük bankın içindedir. Bu satırlar Linux'un kendi başlangıcında oluşur;
S-Boot'un Windows yolunda aynı dinamik alanları hâlâ ayırdığını **kanıtlamaz**.
Fakat WinPE'nin az RAM gerektiren bu turunda gereksiz riske girmemek için
Windows'a dört 2 GiB banktan yalnız `0x900000000..0x980000000` aralığı
verilir; düşük bank ve ilk/son üst bank tamamen rezerve edilir. Bu bir
koruyucu bellek bütçesidir, yeni bir S-Boot olgusu değildir. `ion_crypto`nun
`alloc-ranges`i yoktur; yerini statik olarak kanıtlamak olanaksızdır.
B2LGoster'ın yaz/geri-oku banka ve sayfa taramaları da devre dışıdır:
belirsiz bir Android rezervine UEFI'nin kendisi de yazmaz.

Yeni B2LGoster `SURUM 28`, UEFI'nin fiilî bellek haritasından aynı 23 aralığı
yeniden denetler. Ekranda `DTB ACIK=00000000 KORUNDU` ve `RAM ... ATLANDI`
görülmeden Windows sonucu yorumlanmaz; farklı bir DTB değeri, Windows'un hâlâ
sabit bir rezervi görebildiğini ve testin geçersiz olduğunu söyler.

**Sonuç (13 Ağustos 2026):** Bu imajla cihaz, uzun WinPE yükleme süresinin
ardından `X:\Windows\System32>` kabuğuna ulaştı. Böylece önceki yaklaşık
10–15 saniyelik temiz `RST_STAT=0x20000000` sıfırlamasının Windows'un
verilmemesi gereken RAM'e tahsis yapmasından kaynaklandığı pratikte
doğrulandı. Bu hipotez için eski reset döngüsü ya da RAM yaz/geri-oku
taramaları yeniden çalıştırılmaz.

#### Giriş aygıtı olmadan WinPE tanısı

Kabuğun görünmesi, cihazda kullanılabilir bir klavye, fare veya dokunmatik
olduğu anlamına gelmez. Bu turda hiçbir giriş aygıtı yoktur; dolayısıyla
`startnet.cmd` artık `cmd.exe` öncesinde otomatik olarak şunları yapar:

- `wpeinit.exe` ile PnP/WMI başlangıcını tamamlar;
- DiskPart ile disk/birimleri; PnPUtil varsa UFS, sorunlu PnP aygıtlarını,
  `Keyboard`/`Mouse`/USB/ağ sınıflarını ve IP yapılandırmasını sorgular;
- kısa raporu doğrudan CMD penceresine basar ve hemen `wpeutil reboot` ile
  **yazılımsal** yeniden başlatır.

Bu reset, sonraki UEFI `B2LGoster` ekranında `b2lfb`nin `UFS ...` kaydını
okumak için gereklidir. CMD istemini açık bırakmak bu deneyde doğru değildir:
giriş aygıtı yoktur ve elle güç kesmek sıcak-reset DRAM kanıtını yok eder.

Bu mekanizma **giriş gerektirmez** ve kullanıcıdan komut yazmasını istemez.
HID sorgusu boş kalırsa bu, Windows'a herhangi bir klavye/fare düğümünün
gelmediğinin ölçümüdür; dokunmatiği var saymak için kanıt değildir. Sadece
SYSTEM ESP yeniden paketlenir; çalışan UEFI `BOOT` imajı değiştirilmez.

Bu rapor aynı zamanda bilgisayardan **canlı** komut vermek için önkoşulu
ölçer. Kabloyu bilgisayara takmak WinPE içinde ADB veya bir terminal kanalı
oluşturmaz: Android'in `adbd`si çalışan işletim sistemi değildir. Bir canlı
kanal için hedefte DWC3 aygıt-modunun, PHY/Type-C rolünün ve aşağıdakilerden
en az birinin Windows ARM64 sürücüsünün çalışması gerekir:

- USB CDC-ACM seri aygıtı; veya
- USB Ethernet/RNDIS ve ardından WinRM/SSH benzeri bir servis.

`USB` PnP satırları ve bir fiziksel ağ bağdaştırıcısı görünmeden bu yola
sunucu/istemci eklemek anlamlı değildir. Mevcut UEFI'de USB bus/host sürücüsü
olmadığı biliniyor; bu, WinPE çekirdeğinin DWC3 aygıt-modunu da çalıştırdığı
anlamına gelmez. Rapor boşsa kısa vadeli güvenilir kanal otomatik ESP günlüğü
yerine ekrandaki raporun fotoğrafı ve yeniden flaşlama döngüsüdür; canlı USB
kanalı ayrı bir DWC3 Windows sürücüsü portu olur.

İlk raporun kesin sonuçları şunlardır: ADK WinPE `10.0.28000.1` içinde
`wmic.exe` yoktur; onun hata metinleri ölçüm değildir. DiskPart ise doğrudan
`There are no fixed disks to show` ve `There are no volumes` yazdı. Aktif
DSDT'de `UFS0` bütünüyle yorumda olduğundan bu beklenen tabandır: Windows'a
UFS aygıtı hiç ilan edilmemiştir.

Sonraki turda UFS0, WinPE'nin artık kararlı olan bellek haritası üzerinde geri
eklendi. `_CRS`, stok DTB'den gelen tek standart `0x13D60000/0x1000` UFSHCI
alanını ve GSIV 281'i verir; Samsung vendor/UniPro/PHY alanları jenerik sürücüye
verilmez.

**İlk UFS0 WinPE sonucu:** PnP aygıtı gerçekten oluşturdu:
`ACPI\SMSN9820\0`, ancak `Problem Code 28 (CM_PROB_FAILED_INSTALL)` ile kaldı;
`DiskPart` fiziksel disk/birim bulmadı. Otomatik ikinci rapor, belirsizliği
kapattı: `storufs.sys`, DriverStore paketi ve boot-start servis kaydı WIM'de
mevcut. Windows, `_CLS={01,09,01}` için
`ACPI\CLS_0001&SUBCLS_0009&PI_01` üretti; `storufs.inf` ise
`ACPI\CC_010901` istiyordu. Yani ilk Code 28, MMIO veya kesme kullanılmadan
önceki PnP eşleşme aşamasındaydı.

Geçerli vendor `_HID` korunurken UFS0'a ACPI'nin tanımladığı standart
`_CID="PCI\CC_010901"` verildi. Bu, Windows'un `storufs.inf` paketinin
desteklediği yayımlanmış UFS uyumluluk kimliğidir; ACPI, `_CID` üzerinden PCI
sınıf kimliğinin bu biçimini tanımlar.
([ACPI _CID tanımı](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/06_Device_Configuration/Device_Configuration.html))

**Bu eşleşme turunun sonucu:** cihazdaki rapor artık `Driver Name:
storufs.inf`, `Matching Device ID: PCI\CC_010901`, `Service: storufs` ve
`Best Ranked / Installed` gösteriyor. Kimlik sorunu kapanmıştır. Aynı aygıt
ardından `Problem Code 10 (CM_PROB_FAILED_START)` ve `0xC0000182` ile kalır;
DiskPart yine fiziksel disk/birim bulmaz. Fotoğraftaki `X:` birimi UFS değildir,
WinPE `boot.wim` RAM diskidir. Böylece sonraki sorun sürücü seçimi değil,
`storufs`un Exynos denetleyicisini başlatma yoludur. Tanı betiği bundan sonra
çıkışı doğrudan konsola bölümler hâlinde basar ve her salt-okunur komuttan sonra
iki saniye bekler; `b2lcmd`/`findstr` yokluğu artık yalancı hata satırı üretmez.

**Sıradaki güvenli ölçüm:** `b2lfb`, PnP/storufs denetleyiciyi sahiplenmeden
önce yalnız bir kez `0x13D60000` UFS HCI penceresini `MmNonCached` ile eşleyip
`CAP`, `VER`, `HCE` ve Samsung `1US_TO_CNT` yazmaçlarını okur. Eşleme aynı
`DriverEntry` içinde `MmUnmapIoSpace` ile bırakılır; HCE, UIC/DME, kesme durumu
ve hiçbir UFS LUN'u değiştirilmez. Bu, daha önce geçersiz olan GIC deneyi gibi
HAL tarafından zaten sahiplenilmiş bir pencereyi kalıcı ikinci kez eşlemek
değildir. Sonraki B2LGoster ekranındaki iki `UFS ...` satırı boot devri
durumunu, `startnet.cmd` ise Code 10 sonrasındaki PnP raporunu gösterir.
Windows bellek yöneticisinin eşle/çöz sözleşmesi bu kullanımda özellikle
aynı aralığın çözülmesini ister. ([MmMapIoSpace](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmmapiospace), [MmUnmapIoSpace](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmunmapiospace))

Bu turdaki `startnet.cmd` düzeltmesi ayrı fakat gerekli bir kabuk belirsizliğini
ortadan kaldırır: önceki sürüm cihazda tam `[UFS: ...]` başlığından sonra
istemciye dönmüştü; fotoğraf tek başına bunun nedenini ayırmaz. UFS raporu bu
nedenle büyük PnP `IF (...)` bloğunun dışındaki `:UfsReport` alt yordamına
taşındı. Yeni yol her durumda `exit /b` ile ana rapora döner ve disk/driver
tanısını kesmez.

Kullanıcının verdiği Exynos kaynak dosyası ayrıca ikinci, sonraki aşamadaki
engeli netleştirir: Linux sürücüsü `LINKSTARTUP` **öncesinde** saat/refclk,
auto-clock-gating ve hata katmanlarını ayarlayıp `ufs_cal_pre_link()` çağırır;
**sonrasında** `ufs_cal_post_link()` ve güç-modu CAL tablolarını çalıştırır.
([ufs-exynos.c](https://github.com/ianmacd/d2s/blob/master/drivers/scsi/ufs/ufs-exynos.c))
Bu akış, bizim UEFI `PlatformUfsLib` CAL zincirimizle uyumludur ve jenerik
`storufs`un HCE resetinden sonra Exynos'a özgü kalibrasyonu yeniden kuramadığı
riskini doğrular. Code 28 artık geçilmiş olsa da, yeni Code 10 bu donanım
aşamasına ulaşıldığını tek başına kanıtlamaz: sürücünün hangi ilk başlatma
adımında `0xC0000182` döndüğünü ayırmak gerekir. Bu nedenle sıradaki tur yalnız
PnP yapılandırmasını ve DiskPart'i gözlemleyecek; UFS'ye yazma yapılmayacaktır.

Windows açılışında dönen çemberin görünmemesi ayrıca beklenir. Kendi BCD
üreticimiz önceki sessiz-reset tanısı için `bootuxdisabled on` ve
`bootmenupolicy Legacy` yazar; `quietboot` zaten `No`dur. Bu ayar WinPE'nin
çalışmasına dair kanıt değildir ve UFS turunda değiştirilmez.

Bu turda `boot.img` (UFS ACPI), `system.img` (WinPE raporu) ve boşaltılmış
`cache.img` birlikte güncellenmelidir. `tools_odin_winpe_bundle.sh`, ham
`out/system.img` ve `out/cache.img` girdilerini kendi içinde `img2simg` ile
Android sparse biçimine çevirir. Sparse sihirli sayısını (`ed26ff3a`) ve son
tar'dan çıkarılmış her imajın SHA-256 eşitliğini doğruladıktan sonra tek
`out/beyond2lte-winpe-ufs.tar` üretir. `boot.img` kasıtlı olarak ham kalır:
çalışan UEFI Odin paketi de bu bölümü ham taşıyordu. Böylece Odin'e tek AP
dosyası verilir; eski CACHE ESP'nin `startup.nsh` dosyasını seçmesi engellenir.

Normal hale dönüş için yalnızca `bash yap.sh` çalıştırmak yeterlidir:
script FADT'yi yeniden SMC konduitine alır ve EL2 sürücüsünü tekrar derleme
dışında bırakır.

Diğer FAT biriminde eski bir ESP kalmamalı. SYSTEM'e bu imaj yazılıyorsa,
CACHE'i ayrıca `bash cache_esp.sh --win --bos` ile silip iki tar'ı aynı flash
turunda yazmak gerekir; aksi halde Shell eski `startup.nsh`'i bulup bütün
deneyi geçersiz kılabilir.

### Sürücü bisect'inin yanlış okunması (dürüstlük notu)

Bir süre `storufs` ve `rdbss`'i "suçlu" olarak kaydettik. `storufs` için
iki yönlü A/B bile yapıldı ve tutarlıydı. Ama `MAX` ölçümü eklendikten ve
`Npfs` geri-dönüş deneyi yapıldıktan sonra anlaşıldı ki bisect'in ölçtüğü
şey suçluluk değil, başlangıç bağımlılıklarının hangi yolu seçtirdiğiydi:

> Bir sürücüyü çıkarmak kullanıcı-modu altyapısını da eksiltebilir.
> Özellikle Npfs çıkarıldığında Windows kullanıcı-moduna ulaşıp
> `0xc000021a` aldı. Npfs geri geldiğinde tek bir açılışta bu bugcheck
> görünmedi ve 63.9 saniyelik sessiz yol görüldü; sonraki açılış 8.8 saniyede
> sıfırlandı. Bu yüzden gözlem, kesin bir sürücü suçluluğu değildir.

Yani `storufs` ve `rdbss` bulguları **başlangıç bağımlılıkları ve iş
miktarı** üzerinden açıklanıyor, donanıma dokunmaları üzerinden değil. Bu
düzeltme geç yapıldı; belgeyi okuyan biri eski kayıtlarda "suçlu bulundu"
ifadesi görürse bu paragrafı esas almalı.

---

### Sessiz sıfırlamanın çözülmesi ve iki eski sonucun geri alınması

WinPE'nin açılmasını sağlayan değişiklik bellek haritasındaydı. İki şey
**birlikte** yapıldı:

1. **UEFI FD / yığın çakışması giderildi.** `BootShim`, `beyond2lte.toml`
   içindeki `FD_SIZE` kadar veriyi `0xC9000000`'a kopyalıyor. `FD_SIZE`
   4 MiB iken harita orada yalnız **2 MiB** tanımlıyordu ve `UEFI Stack`
   `0xC9200000`'daydı — yani yığın, firmware imajının ikinci yarısının
   içindeydi ve o yarı Windows'a kullanılabilir bellek olarak veriliyordu.
   Üstelik bölge `BsData` ilan edilmişti; `EfiBootServicesData`'nın tanımı
   gereği işletim sistemi `ExitBootServices`'ten sonra o sayfaları geri
   alıp kullanabilir. FD 4 MiB'a çıkarıldı, yığın `0xC9400000`'a taşındı.

2. **Windows'a tek banka bırakıldı.** `0x900000000..0x980000000` (2 GiB)
   dışındaki her şey `Reserved` yapıldı; alt banka, ilk üst banka ve son
   üst banka koruma altına alındı.

Bu, aylardır açıklayamadığımız tablonun tamamına oturuyor: işe orantılı
ölüm (ayırıcı ilerledikçe firmware ezilir), bugcheck olmaması (çöken
Windows değil), `EbsTest`'in yaşaması (hiçbir şey ayırmaz), sürücü
bisect'inin tutarsızlığı (her sürücü ayırma miktarını değiştirir).

**Geri alınan sonuç 1 — "bellek aklandı" yanlıştı.** Yaptığımız tam bellek
taraması yalnız `EfiConventionalMemory` bölgelerini geziyordu ve her sayfaya
bir 64 bitlik kelime yazıp geri okuyordu. `BsData` ilan edilmiş firmware
bölgesine hiç dokunmadı. Ayrıca tarama tamamen CPU tarafındaydı: aygıt
DMA'sını, TZASC'ın master'a göre korumasını ve ayırıcının o belleği
*kullanma biçimini* sınamadı. "Belleğin tamamı tarandı, temiz" ifadesi bu
nedenle fazla iddialıydı ve bir dalı erken kapattı.

**Geri alınan sonuç 2 — "Windows CPU süresi sabit ~10 saniye" bir yasa
değildi.** Üç ölçüm 9.78 / 10.04 / 10.20 vermişti, ama `Npfs` deneyinde bir
açılış 63.9 saniye sessiz gitti ve sonraki 8.8 saniyede sıfırlandı. Sayı
gerçekten de ayırma miktarına bağlıydı, sabit bir bütçeye değil.

### Ayrılmamış tek değişken

İki değişiklik birlikte yapıldığı için hangisinin gerekli olduğu **henüz
bilinmiyor**. Bu, ileride önemli olacak: gerçek bir Windows kurulumu
2 GiB ile rahat etmez.

Tek değişkenli ayrım şudur: FD/yığın düzeltmesi **korunarak**, üst iki
bankanın `Reserved` kaydı kaldırılır ve Windows'a yeniden çok bankalı RAM
verilir.

- WinPE yine açılırsa → asıl sebep FD/yığın çakışmasıydı, 2 GiB sınırı
  gereksiz bir tedbirdi ve kaldırılabilir.
- Sessiz sıfırlama dönerse → banka sınırlaması gerçekten gerekli; o zaman
  hangi bankanın sorunlu olduğu tek tek eklenerek daraltılır.

Bu ayrım yapılmadan "2 GiB gerekiyor" demek, FD hatasının etkisini banka
sınırlamasına yazmak olur.

**03 Eylül 2026 durum denetimi:** Bu ayrım uygulanmamıştır. `yap.sh`, kökteki
eski `beyond2ltePkg/` anlık görüntüsünü değil
`mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/` kaynağını derleme
ağacına kopyalar. Bu gerçek kaynakta alt banka ile iki üst banka guard'ı
hâlâ aktiftir ve güncel WinPE ancak bu haritayla kararlı açılmaktadır. USB
bring-up sırasında guard'lar kaldırılmayacak; xHCI'nin düşük adresli DMA
ihtiyacı, korunan bankaları Windows genel RAM'ine açmadan sınırlı common-buffer
canary ile ölçülecektir.

### b2lufs: adaptör başlıyor ama disk numaralandırılmıyor (14 Ağustos 2026)

Bu bölüm, belgenin üstündeki `storufs` + `PCI\CC_010901` anlatısını
**geçersiz kılar**. Kod artık başka bir yerde:

- `DSDT`'de `UFS0` yalnız `_CID="BYND9820"` yayınlıyor; inbox `storufs`
  aynı HBA'ya bilerek bağlanamıyor.
- `windows/b2lufs/` altında kendi StorPort miniportumuz var: HCE'ye
  dokunmuyor, firmware'in bıraktığı link durumunu devralıyor, tek UTRD
  yuvası kullanıyor, yalnız salt-okunur SCSI kabul ediyor.
- WinPE kabuğu açılıyor ve DiskPart çalışıyor. Belgenin 7. bölümündeki
  "tıkanma noktası" tanımı bu nedenle güncel değildir.

**Belirti:** aygıt PnP'de `Started`, kaynaklar haritalanıyor,
`HwInitialize` başarıyla dönüyor — ama DiskPart disk göstermiyor.

**İlk bulunan eksik:** `b2lufs_transport.c` içindeki hedef/başlatıcı
`ConfigInfo` sözleşmesiydi.

```c
ConfigInfo->MaximumNumberOfTargets = 1;   // taranabilir tek hedef: 0
// InitiatorBusId[0] hic kurulmamis       // ConfigInfo sifirli -> 0
```

SCSI port mantığında **başlatıcının kendi kimliği bus taramasından
dışlanır**. Taranabilir tek hedef 0 ve başlatıcı da 0 olunca StorPort
taranacak hiçbir hedef bulamıyor, disk PDO'su üretmiyor, DiskPart boş
çıkıyor.

Bu, `b2lufs` üzerinde yapılan son dört değişikliğin (BTL8 sözleşmesi,
`HwUnitControl`, PnP yaşam döngüsü, `BusChangeDetected`) neden sonucu
değiştirmediğini de açıklıyor: hepsi **hiç başlamayan** bir taramanın alt
aşamaları.

**Yapılan ilk düzeltme:** iki hedef ilan edilip başlatıcı 1'e alındı; böylece
dışlama kuralı uygulansa da uygulanmasa da hedef 0 taranabilir kalıyor.
Hedef 1'e giden her SRB `B2LUfsExecuteScsi`'de zaten `NO_DEVICE` ile
reddediliyor ve `HwUnitControl` LUN0 dışını kabul etmiyor, yani fazladan
hedef UFS'e tek komut bile göndermiyor.

**Son flaş bu açıklamanın eksik olduğunu kanıtladı:** hedef/başlatıcı
düzeltmesine rağmen `Disk 0` oluşmadı. Microsoft'un StorAHCI örneği ve
StorPort DDI sözleşmesi alan alan karşılaştırıldığında daha üst seviyede üç
doğrudan ihlal bulundu:

```c
InitData.TaggedQueuing = FALSE;          // StorPort: TRUE zorunlu
InitData.AutoRequestSense = TRUE;        // ilan var, UFS sense kopyası yoktu
InitData.MultipleRequestPerLu = FALSE;   // StorPort: TRUE zorunlu
```

Yeni sürüm üç bayrağı da StorPort'un istediği biçimde `TRUE` bırakıyor.
StorPort'un en az 16 scatter/gather girdisi şartına karşı 32 girişli PRDT
`NumberOfPhysicalBreaks=32` olarak ilan ediliyor. Tek aktif UTRD'yi doğru
yansıtmak için `MaxNumberOfIO`, `MaxIOsPerLun` ve başlangıç kuyruk derinliği
1; miniport `StorSynchronizeFullDuplex` kullanıyor. Donanımda yalnız tek UTRD
kullanma sınırı, StorPort'un StartIo kilidi altında komutu `HwStartIo`
dönmeden eşzamanlı tamamlayarak sağlanıyor. `QueueTag` UFS COMMAND UPIU'ya
taşınıyor ve RESPONSE UPIU'da doğrulanıyor.

Git geçmişi ile cihaz fotoğrafları ayrıca bir enumerasyon regresyonunu
gösterdi. `0.0.2.0`, `HwInitialize` sonunda `BusChangeDetected` gönderdiğinde
`Disk 0` PDO'su oluşmuştu (kapasite henüz 0 B idi). Bildirimin tek kopyasını
PASSIVE callback'e taşıyan `0.0.2.1/0.0.2.2` flaşlarında PDO tamamen
kayboldu. Ancak `0.0.2.4` ile iki erken bildirim birlikte gönderildiğinde de
normal `BLS1`/`BLP1` isteği oluşmadı. Microsoft'un yaşam döngüsü belgesi
başlangıç sırasını `HwFindAdapter`, `HwInitialize`, ardından
`HwAdapterControl(ScsiQuerySupportedControlTypes)` olarak tanımlar; PASSIVE
callback de bu başlangıç zincirine aittir. `0.0.2.5` iki erken bildirimi
kaldırdı; fakat cihaz çıktısı implicit `RequestTimerCall` isteğinin hiç
çalışmadığını gösterdi. `0.0.2.7`, Microsoft StorAHCI örneğindeki gibi
`HwFindAdapter` içinde `StorPortInitializeTimer` ile açık bir timer nesnesi
oluşturur; PASSIVE callback `StorPortRequestTimer` ile bunu 500 ms'ye kurar.
Dump stack'in yüksek IRQL yolu bu timer/PASSIVE tanısını atlar. Her iki
API'nin dönüş kodu kayıt altındadır. Timer çalışınca bus 0 için
`BusChangeDetected` gönderir ve ayrı `BLT1` Operational kaydı bırakır.
Böylece `BLT1` yokluğu timer
öncesi başlangıç sorununu, `BLT1` olup `BLS1` olmaması StorPort tarama
sorununu, `BLS1` oluşması ise gerçek SCSI/UFS keşif sonucunu ayırır.

`AutoRequestSense=TRUE` artık yalnız bir iddia değil: UFS RESPONSE UPIU'nun
DW8 alanındaki büyük-endian sense uzunluğu ve onu izleyen en çok 18 sense
baytı legacy SRB'nin `SenseInfoBuffer` alanına kopyalanıyor;
`SRB_STATUS_AUTOSENSE_VALID` yalnız gerçekten veri kopyalandığında ekleniyor.
Bu özellikle desteklenmeyen isteğe bağlı INQUIRY VPD sayfasının taşıma
arızası gibi yorumlanıp disk PDO'sunu düşürmesini önlüyor.

StorAHCI karşılaştırması iki PnP farkını daha ortaya çıkardı. Adapter-level
`StorQueryCapabilities` isteği artık caller'ın tamponu korunarak başarılı
tamamlanıyor; daha önce `INVALID_REQUEST` dönüyordu. Inbox `storufs` komut
yüzeyinde bulunan `START STOP UNIT`, bu salt-okunur aşamada donanıma
gönderilmeden yerel başarılı no-op olarak tamamlanıyor. Diğer yazma/mutasyon
CDB'leri hâlâ HCI'ye ulaşmadan reddediliyor.

Önceki `Master`/`ScatterGather` yorumu da geri alındı. Güncel StorPort
belgesi bu alanları fiziksel miniport için zaten `TRUE` başlatıyor ve
miniportun değiştirmemesini açıkça söylüyor; yeni kod bunlara yazmıyor.

UEFI şu anda birincil şüpheli değildir: aynı flaşta HCS `0x10f`, gerçek UFS
`READ CAPACITY(10)` sonucu 4096 bayt blokla başarılı ve `REPORT LUNS` beş
kimlik döndürüyor. postmarketOS Exynos9820 notu da S-Boot'un UFS'yi
başlattığını ve Linux'un aynı `13d60000.ufs` denetleyicisini kullandığını
doğruluyor. Bu ölçümler link/PHY/handoff'un çalıştığını, eksik Disk PDO'nun
StorPort/SCSI enumerasyon katmanında olduğunu gösteriyor.

`0.0.2.7` ayrıca Microsoft'un yaşam döngüsü sözleşmesindeki kalan farkları
kapatır: uzun süren UFS probları DIRQL `HwInitialize` yerine PASSIVE'a
taşındı; kilitsiz/eşzamanlı çağrılabilen `HwBuildIo` artık PnP durumu
değiştirmiyor; zorunlu `ScsiStopAdapter` gerçekten UTRL işlemeyi durduruyor,
`ScsiRestartAdapter` denetleyiciyi `HwInitialize` ile aynı taşıma yolundan
yeniden başlatıyor; bus/device/LUN reset SRB'leri ve `HwResetBus` yalnız UTP
kuyruk durumunu güvenli biçimde yeniden kuruyor. Stop yolundaki UTRLCLR de
UFSHCI/Linux sözleşmesine uyarlandı: temizlenecek slot biti `0`, değişmeyecek
bitler `1` yazılıyor (`~Doorbell`). HCE, UniPro, PHY ve UFS medya resetlenmiyor.
`PORT_CONFIGURATION_INFORMATION` karşılaştırmasında `ScatterGather` ve
`Master` alanlarının StorPort'a ait olduğu, miniportun bunları değiştirmemesi
gerektiği; `ResetTargetSupported` alanının ise obsolete olduğu doğrulandı.
Kod bu alanlara artık yazmıyor. Çok katmanlı reset gerçek reset SRB'leri ve
`HwResetBus` üzerinden uygulanıyor.
Normal I/O PRDT'si de `Srb->DataBuffer` sanal eşlemesini tekrar fiziksele
çevirmek yerine StorPort'un salt-okunur `StorPortGetScatterGatherList`
çıktısından kuruluyor. DDI'nin izin verdiği düşük bellek
`DataBuffer == NULL` durumunda fiziksel SG listesi kullanılmaya devam ediyor.

**15 Ağustos 2026 cihaz sonucu:** `0.0.2.7` ile açık timer gerçekten ateşledi,
StorPort normal INQUIRY yürüttü ve `Disk 0` PDO'su yeniden oluştu. Disk kimliği
`SAMSUNG KLUDG4U1EA-B0C1` doğru; fakat `detail disk` bus türünü `FIBRE`, boyutu
ise `0 B` gösterdi. Aynı açılıştaki bağımsız UFS probu hâlâ başarılıydı:
`ReadCapacityRaw=01DC9FFF00001000`, yani son LBA `0x01dc9fff` ve mantıksal
blok 4096 bayt. Bu sonuç UEFI/PHY/link veya medyanın değil, StorPort birim
sözleşmesinin kalan kısmının hatalı olduğunu kesinleştirir.

`0.0.2.8` bu iki üst-katman farkını Microsoft DDI'sine göre kapatır.
`HwFindAdapter` başında `StorPortSetFeatureList` ile
`StorportFeatureBusTypeUnitControl` kaydedilir; `HwUnitControl` destek
listesinde `ScsiUnitQueryBusType` ilan edilir ve yalnız BTL8 LUN 0 için
`BusTypeUfs` döndürülür. İki adım birlikte yapılmazsa Windows 10 21H1 ve
sonrasında StorPort sorguyu göndermediği için görülen `FIBRE` varsayılanı
değişmez. Kapasite için ön taramada gerçek UFS'den DMA ile alınmış ve
doğrulanmış aynı `last-LBA + block-length` anlık görüntüsü kullanılır.
Disk sınıfının READ CAPACITY SRB tamponu `Srb->DataBuffer` doğrudan
dereference edilmeden, Microsoft'un bu amaçla sağladığı
`StorPortGetSystemAddress` ile eşlenir; cevap büyük-endian READ CAPACITY(10)
veya (16) biçiminde tamamlanır. Medya değeri uydurulmaz ve yazma CDB'si
eklenmez. Tanı şeması 10'dur; `BusTypeFeatureStatus=0`, `UnitBusType=0x13`
ve disk taramasından sonra `BusTypeQueryCount>=1` beklenir.

Yeni tanı şeması `NumberOfBuses=1`, `MaximumTargets=2`, `InitiatorBusId=1`,
`ScatterGather=1`, `Master=1`, `TaggedQueuing=1`, `AutoRequestSense=1`,
`MultipleRequestPerLu=1`, `NumberOfPhysicalBreaks=32`,
`SynchronizationModel=1`, `MaxNumberOfIO=1`, `MaxIOsPerLun=1` ve
`InitialLunQueueDepth=1` değerlerini CMD'de yan yana
gösterir. `BusScanTimerHandleValid=1`, initialize/request durumlarının `0`
olması ve ardından `BLT1` görülmesi StorPort timer zincirinin çalıştığını
kanıtlar. Nihai doğrulama ölçütü ikinci DiskPart bloğunda `Disk 0` ve ardından
`list partition` ile Android GPT bölümleridir.

---

## 8. Denenmiş ama sonuç vermemiş yollar

| Deneme | Sonuç |
|---|---|
| EL2'ye çıkıp `HCR_EL2.TSC` ile SMC tuzaklamak | Eski, geniş SMC tuzağı imza üretmedi ve bir tur cihazı dondurdu. Bu yol terk edildi; `--psci-hvc` yalnızca FADT'nin yönlendirdiği HVC PSCI çağrılarını alan, ayrı ve dar bir deneydir |
| `PsSetLoadImageNotifyRoutine` ile yüklenen sürücüyü kaydetmek | `KMODE_EXCEPTION_NOT_HANDLED (0x1E)`, ayrıca sinyal vermiyordu (aynı görüntüde CNT 16, 3, 21) |
| `PsSetCreateProcessNotifyRoutine` | Bugcheck |
| Windows içinden GIC okumak (`MmMapIoSpace`) | `0x1E`. ARM'de aynı fiziksel adresi farklı niteliklerle iki kez haritalamak tanımsız; HAL GIC'i zaten Device olarak haritalamış |
| `sos` ile çekirdek açılış metni | Win10/11'de işlevsiz |
| KDNET | Desteklenen NIC yok, ACPI'de PCI host bridge yok, USB debug özel kablo ister |
| `boot.wim`'i sıkıştırmasız yapmak | `winload.efi 0xc0000487` — `export` önyüklenebilir işaretini taşımıyordu (`--boot` gerekiyor, sonradan düzeltildi) |
| FACP'de PSCI'yi kapatmak | Windows `winload`'da duruyor, çekirdek hiç başlamıyor |

---

## 9. Sık tekrarlanan hatalar — okuyan için uyarı

Bu proje boyunca aynı sınıf hatayı defalarca yaptık. Bir sonuç
yorumlanırken bunlar akılda tutulmalı:

1. **Doğrulanmamış aracın sessizliği kanıt değil.** Kanca ateşlemedi mi,
   yoksa hiç yüklenmedi mi? Her ölçüm aracının kendi çalıştığını
   kanıtlaması gerekiyor.

2. **Bir sonraki açılışta okunacak her kayıt, temizlenmeden önce
   kopyalanmalı.** DXE sürücüleri kabuktan önce çalışıyor; kaydı silip
   sonra okumaya çalışmak üç ayrı ölçümü yok etti.

3. **Nöbetçi değer, ölçtüğü şeyle çakışmamalı.** `0xFFFFFFFE`'yi "çalışmadı"
   diye seçtik, EL3 `-2` döndürdü, sonucu ters okuduk.

4. **Bir değeri geri almak için açıkça yazmak gerekiyor.** Listeden silmek
   "artık değiştirme" demek, "eski hâline dön" demek değil.

5. **Bir aracın hedef listesi, o aracın değiştirdiği duruma göre
   belirlenemez.** İki ayrı araçta bu hata bisect'i imkânsız kıldı.

6. **"Güvenli" ayarlar ölçümü körleştirir.** `ErrorControl=0`,
   `bootstatuspolicy IgnoreAllFailures`, `AutoReboot=0` — üçü de en
   bilgilendirici hatayı sessizliğe çevirdi.

7. **Tek yönlü sonuç kabul edilmemeli.** Hem "kapatınca düzeldi" hem
   "açınca bozuldu" görülmeli.

---

## 10. Dışarıdan bakan biri için sorular

Aşağıdakiler bizim cevaplayamadığımız, taze bir gözün faydalı olabileceği
noktalar:

1. **Windows ARM64'te, hizmet kümesine göre 8.6–63.9 saniye CPU işi
   sonrasında bugcheck üretmeden platformu sıfırlatabilecek bir mekanizma
   var mı?** Windows `ResetSystem` çağırmıyor, kısa yolda bugcheck yok,
   EL3 istisnası yok.

2. **PSCI 0.1 sunan bir platformda Windows ARM64 ne yapar?**
   `PSCI_VERSION` `0xFFFFFFFF` dönüyor. `CPU_SUSPEND` kimliği 0.2 ile aynı
   (`0xC4000001`) ama güç durumu kodlaması farklı. Windows 0.2 biçiminde bir
   parametre gönderirse Samsung EL3 onu kendi kodlamasıyla yorumlar —
   yanlış yorumlanan bir güç durumu kümeyi kapatmak olabilir mi?

3. **`0xc000021a`** — WinPE'de `smss.exe` neden ölüyor? Şu an kapalı olan
   servisler: `storufs` ve `Start=1` kümesinin bir kısmı. Hangi servisin
   eksikliği bu hatayı üretir?

4. **Exynos 9820'de, işletim sisteminden bağımsız çalışan ve AP'nin
   çalışma süresini sayan bir mekanizma var mı?** ACPM/APM firmware'i,
   ITMON, PPMPU, s2mps19 PMIC — hangisi böyle davranabilir?

5. **Ölçüm yöntemimizde gözden kaçan bir kusur var mı?** Özellikle
   `TIK - SPN/MAX` hesabı: bizim öncelik 1 iş parçacığımız gerçekten
   yalnızca Windows boştayken mi çalışıyor?

---

## 11. Tekrarlanabilirlik: komutların tamamı

### UEFI derleme (WSL)

```bash
bash yap.sh                 # DEBUG derle, Odin .tar uret
bash yap.sh --acpi          # ACPI .dsl dosyalarini da derle (iasl)
bash yap.sh --psci-hvc      # deneysel PSCI v0.2 HVC koprusu
bash yap.sh -r RELEASE      # RELEASE
bash yap.sh --clean         # temiz derleme
```

Çıktı: `uefi-boot.tar` → Odin'de **BOOT** yuvasına flaşlanır.

### ESP imajı

```bash
bash cache_esp.sh --win --system    # Windows dosyalari, SYSTEM bolumune
bash cache_esp.sh --linux --system  # Linux kernel+initramfs, SYSTEM'e
bash cache_esp.sh                   # Linux, CACHE bolumune
```

Çıktı: `system-esp.tar` → Odin'de **SYSTEM** yuvasına.

Koruma: `--system` verilip `--win`/`--linux` verilmezse betik duruyor —
bu kombinasyon bir turu yakmıştı.

### Windows sürücüsü

```bash
bash tools_b2lfb.sh         # derle, imzala, boot.wim'e enjekte et,
                            # SYSTEM hive'ina Start=0 servis olarak yaz
```

Derleme bayrakları: `-ffreestanding -fno-stack-protector
-mno-stack-arg-probe -ffixed-x18`. Sonuncusu şart: `x18` Windows ARM64'te
platform yazmacı, çekirdek modunda `KPCR`'yi gösteriyor.

`b2lufs.sys` derlemesi yalnız Windows PowerShell'den yapılır:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu-24.04\home\orhun\beyond2lte-uefi\windows\b2lufs\build-b2lufs.ps1"
```

Derleme başarılı olduktan sonra imza/WIM/ESP/Odin paketi yalnız WSL'den:

```bash
cd ~/beyond2lte-uefi
B2L_SKIP_MSVC_BUILD=1 bash tools_b2lufs.sh
bash yap.sh
bash cache_esp.sh --win --system
bash cache_esp.sh --win --bos
bash tools_odin_winpe_bundle.sh
```

### Servis / sürücü yönetimi

```bash
bash tools_winpe_surucu.sh liste                 # durum tablosu
bash tools_winpe_surucu.sh sadece storufs        # sadece adi verilen kapali
bash tools_winpe_surucu.sh aralik 1 14           # numarali aralik kapali
bash tools_winpe_surucu.sh hepsi-ac              # temel duruma don

bash tools_winpe_sistem.sh liste
bash tools_winpe_sistem.sh sadece rdbss
bash tools_winpe_sistem.sh birak Npfs
bash tools_winpe_sistem.sh birak Npfs Null
bash tools_winpe_sistem.sh kapat / ac
```

`sadece`, `birak` ve `aralik` **idempotent**: önceki durumdan bağımsız, her çağrı
tam olarak istenen durumu kurar ve yazdığını geri okuyup doğrular.

### BCD

Windows'ta `windows\bcd_olustur.cmd [tani|menu|menuram|disk|hatagoster]`
çalıştırılıp masaüstünde oluşan `BCD` dosyası `esp\EFI\Microsoft\Boot\`
altına kopyalanıyor. `testsigning` ve `nointegritychecks` her varyantta
açık (kendi sürücümüz imzalı ama kendi sertifikamızla).

### Linux kontrol deneyi

```bash
cd esp
curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/vmlinuz-lts
curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/initramfs-lts
cd ..
bash linux_test/kur.sh
bash cache_esp.sh --linux --system
```

### Stok çekirdeğin açılış sırasını çıkarma

```bash
tar -xf stockfirmware/AP_*.tar.md5 boot.img.lz4
python3 -c "import lz4.frame; open('boot.img','wb').write(lz4.frame.decompress(open('boot.img.lz4','rb').read()))"
# ANDROID! basligindan kernel'i kes (offset = page_size = 2048)
pip install vmlinux-to-elf
vmlinux-to-elf kernel.bin vmlinux.elf
```

Çekirdek sıkıştırılmamış ARM64 Image, `kallsyms` gömülü, 113.175 sembol.
`__initcall_*` sembolleri bağlantı sırasında duruyor: **1162 initcall**,
sıralı olarak `dtb-analiz/initcall_sirasi.txt` içinde.

### Depoya kaydetme

```bash
bash ~/UEFI_beyond2lte/kaydet.sh "mesaj"
```

Mac'te çalışır, sonra WSL'de `git pull`.

---

## 12. Bellek haritası — kritik adresler

`mu-silicium-overlay/.../Library/MemoryMapLib/MemoryMapLib.c`

| Adres | Boyut | Ne |
|---|---|---|
| `0x80000000` | — | DRAM tabanı |
| `0xCA000000` | 20 MB | Framebuffer (`Display Reserved`, BGRA) |
| `0xCB400000` | 60 MB | DXE heap |
| `0xCF000000` | 256 KB | `B2L EL2` (kullanım dışı, `Reserv`) |
| `0xCF030000` | 256 B | **b2lfb kaydı** (Windows sürücüsü yazıyor) |
| `0xCF040000` | 64 KB | **`B2L RT Log`** — `RtData` olmak zorunda |
| `0xCF041000` | | EL3 SEH yakalama |
| `0xCF041080` | | SEH/PSCI durum sözcükleri |
| `0xCF041100` | | önceki açılışın SEH kopyası |
| `0xCF041300` | | GIC susturma kaydı |
| `0x10101000` | 4 KB | GICD (dağıtıcı) |
| `0x15860404` | 4 B | PMU `RST_STAT` |

**Önemli tuzak:** `Reserv` (EfiReservedMemoryType) bölgeleri
`SetVirtualAddressMap` sonrası sanal haritaya **girmiyor**; `RtData`
giriyor. Bu bir bugcheck'e mal oldu (`SYSTEM_THREAD_EXCEPTION_NOT_HANDLED`).

`RST_STAT` okuması: `0x20000000` yazılım sıfırlaması, `0x40010000` güç
çevrimi.

---

## 13. Bilinen ve düzeltilmemiş işler

- Mu-Silicium'a gönderilmesi gereken düzeltmeler:
  - `UfsDxe.inf`'te `TimerLib` eksik
  - beyond1lte bellek haritasında DXE heap framebuffer'ı yutuyor
  - `UfsDxe.c` `Media.ReadOnly = FALSE` yazıyor, `bLUWriteProtect`'e bakmıyor
  - yeni `S5E9820Pkg/Library/PlatformUfsLib`
- `B2LEl2Dxe` ve `B2LResetHookDxe` kullanım dışı, kaynakları duruyor
- `boot.wim`'in orijinal `Start` değerleri hiç kaydedilmedi; geri dönüş için
  ISO'daki dokunulmamış kopyadan okunmalı

---

## 14. B2LUFS 0.0.3.0 — kapasite SRB yönü ve yazma yolu

Son cihaz turunda denetleyici `Started`, bus türü `UFS`, ön-prob cevabı
`01DC9FFF00001000` ve `ReadCapacityValid=1` olmasına rağmen Disk 0 kapasitesi
`0 B` kaldı. Sorun UEFI/UFS linkinde değil, legacy SRB yön yorumundaydı:
Microsoft `SRB_FLAGS_UNSPECIFIED_DIRECTION` değerini `DATA_IN | DATA_OUT`
olarak tanımlar. Önceki salt-okunur kapı yalnız `DATA_OUT` bitini sınadığı için
disk sınıfının geçerli READ CAPACITY isteğini yazma sanıp UFS veya kapasite
tamamlama yoluna ulaşmadan reddediyordu.

0.0.3.0 sürümü yönü şu sözleşmeyle çözer:

- açık `DATA_IN` ve `DATA_OUT` değerleri UTRD ile COMMAND UPIU'ya aynen çevrilir;
- iki bit birlikteyse normal disk CDB'sinden gerçek yön belirlenir;
- transfer uzunluğu sıfırsa no-data UPIU kurulur;
- bilinmeyen ama açık yönlü CDB'ler engellenmez;
- yazma payload'ı ETW'ye kopyalanmaz, yalnız okuma veya sense örneği tutulur.

Salt-okunur CDB allow-list'i kaldırıldı. LUN 0 için WRITE, UNMAP, MODE SELECT,
WRITE SAME ve diğer açık DATA_OUT istekleri gerçek UFS PRDT aktarımına gider.
Boot/RPMB LUN'ları hâlâ Windows'a sunulmaz. Ön-probda doğrulanan gerçek kapasite
Microsoft StorAHCI örneğindeki gibi `HwBuildIo` içinde mapped
`Srb->DataBuffer` adresine yazılır ve orada tamamlanır. Callback `FALSE`
döndürerek SRB'nin `HwStartIo`ya ikinci kez verilmesini engeller. `BLC3`
tanısında `Outcome=5`, `MapStatus=0`, `MapSource=1`, `CompletionPhase=1`,
`ReturnedLength=8` ve gerçek cevap baytları beklenir.

Microsoft'un boot/dump kısıtları da tamamlandı: NULL `DriverEntry` argümanları,
`ConfigInfo->DumpMode` ve `dump=1` tanınır; dump modunda timer, PASSIVE kayıt
yolu, registry ve ETW kapalıdır; bus reset isteği başarıyla yok sayılır. Fiziksel
miniport `STOR_FEATURE_DUMP_POINTERS` ilan etmediği için
`MINIPORT_DUMP_POINTERS` desteği isteğe bağlı kalır ve sahte bir sözleşme
eklenmez. Paylaşılan storage-class PnP sayfalarındaki `AddDevice` ve
`DispatchPnP` görevleri disk.sys/classpnp'ye aittir; miniportta bunları tekrar
uygulamak yerine mevcut StorPort SRB PnP, unit-control ve adapter-control yolu
korunmuştur. `ScsiUnitUsage` paging/hibernation/dump bildirimlerini kabul eder;
ilan edilmeyen `ScsiUnitPower` yerine gelen `SRB_FUNCTION_POWER` ise gerçek
UTRL stop/restart işini takip eden adapter-control callback'lerine bırakarak
başarıyla tamamlanır.
