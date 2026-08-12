# beyond2lte — Windows on ARM projesi, sıfırdan devir belgesi

Bu belge dışarıdan bakan biri için yazıldı. Hiçbir ön bilgi varsaymıyor.
Amaç: projeyi hiç görmemiş bir insanın ya da modelin, neyi neden yaptığımızı,
neyi ölçtüğümüzü, neyi eledigimizi ve şu an nerede tıkandığımızı tek
dosyadan anlayabilmesi.

Belgedeki her "elendi" ifadesi bir **ölçüme** dayanıyor. Tahminle elenen
hiçbir şey burada elenmiş sayılmıyor; tahminler ayrı başlık altında.

---

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

1. **Windows çekirdeği ~10 saniye yaşayıp cihaz sessizce sıfırlanıyor.**
   `RST_STAT = 0x20000000` (yazılım sıfırlaması), DRAM hayatta kalıyor.

2. **Bugcheck yok.** `BUG=H`. Geri çağrı iki yönde de doğrulandı (gerçek bir
   bugcheck olduğunda `BUG=E` ve yeşil bant çıkıyor).

3. **Windows `ResetSystem`'i çağırmıyor**, PSCI kapalıyken bile.

4. **Duvar saati oynak, Windows'un CPU süresi sabit:**

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
   ACPI PSCI 0.1'i ifade edemiyor; FACP'deki bit "0.2 veya üstü" demek ve
   Windows ARM64 bunu şart koşuyor. Biti kapatınca Windows `winload`'da
   duruyor, çekirdek hiç başlamıyor.

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

**Windows, kendi CPU'sunda yaklaşık 10 saniye çalıştıktan sonra cihaz
sessizce sıfırlanıyor. Ne olduğu bilinmiyor.**

Bu sayı:

- sürücü kümesinden bağımsız
- CPU frekansından bağımsız
- duvar saatinden bağımsız
- boşta kalmayla ilgisiz (CPU hiç boşa düşmüyor)

Ve şunları tetiklemiyor:

- Linux'ta (170 saniye tam yük)
- `EbsTest`'te (30+ saniye meşgul döngü)

Yani sayaç yalnızca **Windows koşarken** işliyor.

### Sürücü bisect'inin yanlış okunması (dürüstlük notu)

Bir süre `storufs` ve `rdbss`'i "suçlu" olarak kaydettik. `storufs` için
iki yönlü A/B bile yapıldı ve tutarlıydı. Ama `MAX` ölçümü eklendikten
sonra anlaşıldı ki bisect'in ölçtüğü şey suçluluk değil:

> Sürücü çıkarınca iş azalıyor, Windows 10 saniyelik bütçe dolmadan
> kullanıcı moduna varıyor ve `0xc000021a` alıyoruz. İş fazla olunca bütçe
> önce doluyor ve sessizce sıfırlanıyoruz.

Yani `storufs` ve `rdbss` bulguları **iş miktarı** üzerinden açıklanıyor,
donanıma dokunmaları üzerinden değil. Bu düzeltme geç yapıldı; belgeyi
okuyan biri eski kayıtlarda "suçlu bulundu" ifadesi görürse bu paragrafı
esas almalı.

---

## 8. Denenmiş ama sonuç vermemiş yollar

| Deneme | Sonuç |
|---|---|
| EL2'ye çıkıp `HCR_EL2.TSC` ile SMC tuzaklamak | Samsung TrustZone arka kapısı (`smc #0`, `x0=0xC2000400`) hiç imza üretmedi. UEFI EL1'de çalışıyor (`EbsTest` ölçtü), EL2 boş ve erişilemez |
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

1. **Windows ARM64'te, çekirdeğin yaklaşık 10 saniyelik CPU çalışmasından
   sonra bugcheck üretmeden platformu sıfırlatabilecek bir mekanizma var
   mı?** Sayaç duvar saatine değil işe orantılı. Windows `ResetSystem`
   çağırmıyor, bugcheck yok, EL3 istisnası yok.

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

### Servis / sürücü yönetimi

```bash
bash tools_winpe_surucu.sh liste                 # durum tablosu
bash tools_winpe_surucu.sh sadece storufs        # sadece adi verilen kapali
bash tools_winpe_surucu.sh aralik 1 14           # numarali aralik kapali
bash tools_winpe_surucu.sh hepsi-ac              # temel duruma don

bash tools_winpe_sistem.sh liste
bash tools_winpe_sistem.sh sadece rdbss
bash tools_winpe_sistem.sh kapat / ac
```

`sadece` ve `aralik` **idempotent**: önceki durumdan bağımsız, her çağrı
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
