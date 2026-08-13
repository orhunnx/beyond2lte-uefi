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

Windows'un USB kernel-debug seçeneği de bunun kısayolu değildir: hedefte
Intel xHCI Debug Capability (DbC) bulunan bir xHCI denetleyicisi ve özel
USB 3.0 debug kablosu ister. Exynos DWC3 OTG için böyle bir DbC desteği
doğrulanmış değildir; ayrıca bu platformda PCI/xHCI aygıtı yoktur.

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

PIT, `BOOTLOADER` bölümünü `sboot.bin` olarak tanımlar; tam boyutu 4 MiB'dir.
Önce recovery'nin bu bölümü hangi Android `by-name` takma adıyla gösterdiği
okunur, sonra yalnız o takma ad alınır. İlk üç alma işlemi yalnız okumaydı;
bu işlem de aynıdır. `sboot`, `uh`, `uhcfg`, `boot` veya `vbmeta`ya yazılmaz
ve Odin kullanılmaz.

Custom recovery içindeki `adb shell` ile Windows PowerShell'de çalışacak
komutlar:

```powershell
$adb = "C:\Users\orhun\OneDrive\Masaüstü\projectfiles\platform-tools\adb.exe"
& $adb devices
& $adb shell "ls -l /dev/block/by-name | grep -Ei 'sboot|bootloader'"
```

Bu çıktıda görünen gerçek takma adı `SBOOT` değişkenine koyduktan sonra:

```powershell
$SBOOT = "sboot"   # önceki komutun gösterdiği TAKMA AD ile degistir
& $adb shell "readlink -f /dev/block/by-name/$SBOOT"
& $adb shell "dd if=/dev/block/by-name/$SBOOT of=/tmp/sboot-G975F.img bs=4M"
& $adb shell "wc -c /tmp/sboot-G975F.img; sha256sum /tmp/sboot-G975F.img"
& $adb pull /tmp/sboot-G975F.img "$env:USERPROFILE\Desktop\sboot-G975F.img"
& $adb shell "rm /tmp/sboot-G975F.img"
Get-FileHash "$env:USERPROFILE\Desktop\sboot-G975F.img" -Algorithm SHA256
```

Beklenen boyut tam `4194304` bayttır. `adb pull`dan önce ve sonra görünen
SHA-256 aynı olmalıdır. `dd`nin yönü özellikle `if=/dev/block/by-name/$SBOOT`
olup `of=/tmp/...`dir; tersine çevrilmez. Dosya alındıktan sonra HWC1
`sboot.bin` ile karşılaştırılır ve yalnız HWC2'nin gerçek çağrı koşulundan
sonuç çıkarılır. Yazma veya bölüm değiştirme için ayrıca, eldeki ikili
kanıtın belirlediği bir gerekçe gerekir.

Kaynaklar: stok `BL_G975FXXSGHWC1...tar.md5` içindeki `sboot.bin.lz4` ve
`uh.bin.lz4`; [Samsung Exynos KVM arayüzü incelemesi](https://github.com/sleirsgoevy/exynos-kvm-patch),
[aynı S10 stok açılış günlüğü](https://gist.github.com/Piecuuu/390f776533c7fddc1278de3d1e2612bc).

Normal hale dönüş için yalnızca `bash yap.sh` çalıştırmak yeterlidir:
script FADT'yi yeniden SMC konduitine alır ve EL2 sürücüsünü tekrar derleme
dışında bırakır.

Diğer FAT biriminde eski bir ESP kalmamalı. SYSTEM'e bu imaj yazılıyorsa,
CACHE'i ayrıca `bash cache_esp.sh --bos` ile silip iki tar'ı aynı flash
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
