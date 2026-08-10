# Donanım ve Ortam Referansı

Bu dosya **projeden bağımsızdır.** Orhun'un cihazı ve bilgisayarlarıyla ilgili,
her yeni sohbette baştan keşfedilmesi zaman kaybı olan bilgileri tutar.

İlk kez VoLTE/LineageOS projesinde (Ağustos 2026) derlendi, ama içeriğin çoğu
o projeye özel değil.

> Kural: burada **kişisel tanımlayıcı yok.** IMEI, IMSI, telefon numarası,
> SIP kimlik bilgileri bu dosyaya yazılmaz. Onlar `capture/` altında duruyor ve
> `.gitignore` ile dışarıda tutuluyor.

---

## 1. Telefon — Samsung Galaxy S10+ (SM-G975F)

| | |
|---|---|
| SoC | Exynos 9820 |
| Kod adı | `beyond2lte` |
| Ortak ağaç | `exynos9820-common` |
| Bootloader | **Açık** (unlocked) |
| PIT | `BEYOND2LTE_EUR_OPEN.pit` |
| CSC bölgesi | `TUR` (OXM çoklu-CSC içinden) |
| Operatör | Turkcell, PLMN 28601 |

### Şu anki durum

**LineageOS 23.2 (Android 16)**, kendi derlediğimiz UNOFFICIAL build,
**test-keys** ile imzalı. PhhIms (`me.phh.ims`) ile VoLTE çalışıyor.

Kendi build'leri hep aynı anahtarla imzalandığı için **dirty flash** güvenli —
veri silmeye gerek yok. `format data` sadece imzalayan anahtar değişirse gerekir.

### Önceki durum (gerekirse geri dönülebilir)

Stock **G975FXXSGHWC1** (Android 12), Magisk ile rootlu.
Firmware tar'ları `CustomIMS/firmware/SM-G975F_firmware/` altında duruyor
(AP / BL / CP / CSC / HOME_CSC).

### Bölüm yapısı — önemli

S10 **klasik bölüm** kullanıyor. Dinamik bölüm yok, `super.img` yok,
`lpunpack` gerekmiyor. `system.img`, `vendor.img`, `product.img` ayrı ayrı.

Firmware'den dosya çıkarmanın yolu: `lz4` aç → sparse'tan raw'a çevir →
imajı **mount etmeden** `debugfs` ile hedefli çek (disk tasarrufu için).

> `simg2img` sihirli sayısı `0xED26FF3A`. Byte sırasını ters yazarsan
> "geçersiz sparse imaj" dersin ve saatini yakarsın.

### Donanım notları

- **Ses:** Samsung ABOX + Cirrus Logic Madera kodek (`beyond-madera`) +
  CS35L41 akıllı amfiler. Kulaklık (receiver) CS35L41'den sürülüyor;
  `HPOUT2` 3.5 mm jak, receiver **değil**.
- **Stock'ta `tinymix` yok.** tinyalsa'dan statik derlenmiş aarch64 binary'si
  `CustomIMS/capture/tinymix-arm64` altında hazır duruyor (Apache-2.0).
- **pstore/ramoops var** — bootloop teşhisinde hayat kurtarıyor:
  ```
  /sys/fs/pstore/dmesg-ramoops-0    # çekirdek
  /sys/fs/pstore/pmsg-ramoops-0     # userspace (system_server çökmeleri burada)
  ```
  Recovery'den okunabiliyor; `/data` mount edilemese bile erişilir.
- `persist.logd.size` **16M** olarak ayarlandı — logcat halka tamponu kabaca
  20–40 dakika geriye gidiyor. Aralıklı hataları sonradan yakalamak için.

### Flash / kurtarma

| İş | Araç | Nerede |
|---|---|---|
| Stock firmware | Odin | Windows |
| Custom ROM zip | LineageOS Recovery (Apply Update / adb sideload) | her yerden |
| Bölüm imajı | `fastboot` veya Odin | Windows |

Download moduna: cihaz kapalıyken **Bixby + Ses Kısma + USB**.
Recovery: **Bixby + Ses Açma + USB**.

---

## 2. Bilgisayarlar

### Windows PC — ana iş makinesi

| | |
|---|---|
| RAM | 31.26 GB |
| İş parçacığı | 16 |
| Kabuk | PowerShell |
| platform-tools | `C:\Users\orhun\OneDrive\Masaüstü\projectfiles\platform-tools` |

Odin, adb ve fastboot burada çalışıyor. USB doğrudan buraya bağlı.

> Yol içinde Türkçe karakter var (`Masaüstü`). Yolları **daima tırnak içine al**,
> özellikle `$env:USERPROFILE` ile birleştirirken.

### WSL2 (Ubuntu) — derleme ortamı

| | |
|---|---|
| Kaynak ağacı | `~/android/lineage` (WSL'in kendi ext4 VHD'si içinde) |
| `.wslconfig` bellek | **27 GB**'a çıkarıldı |
| USB geçişi | **Yok** |

Üç kural:

1. **Kaynak ağacı asla `/mnt/c/` üzerinde olmasın.** Windows diskine 9p/drvfs
   erişimi AOSP için felaket yavaş ve büyük/küçük harf sorunları çıkarıyor.
2. **USB yok** — `usbipd-win` ile uğraşmak yerine iş bölümü yap:
   derleme WSL'de, flash ve adb Windows'ta. Zip'i `cp out/.../*.zip /mnt/c/...`
   ile taşı.
3. VHD kendiliğinden küçülmüyor. C: sürücüsünde gerçekten 400–500 GB boş olmalı.

### Mac

Analiz ve adb için kullanıldı. **Disk sıkışık** — son bakıldığında 461 GB'ın
%98'i doluydu, ~11 GB boş. Büyük imaj açma işlerini burada yapma; önce
`df -h` ile kontrol et.

---

## 3. Öğrenilmiş tuzaklar

Bunların her biri bir kez zaman kaybettirdi.

### PowerShell

- `grep`, `tee ~/dosya`, arka planda `&` **çalışmıyor.**
  Karşılıkları: `Select-String`, `Out-File -Encoding utf8`, `Start-Job`.
- **`adb logcat | Out-File` tamponlanıyor.** Ctrl+C ile kesersen dosya
  **0 byte** çıkar. Bunun yerine dump al:
  ```powershell
  ./adb logcat -b all -d > "$env:USERPROFILE\Desktop\log.txt"
  ```
  `-d` dump alıp çıkıyor, yönlendirme düzgün yazıyor.
- `dumpsys` çıktısı için `findstr /i "..."` iş görüyor.

### AOSP derlemesi

- **`soong_build` tek süreç ve 21 GiB'a kadar çıkabiliyor.** OOM ile ölürse
  `-j4` **yardım etmez** — soong, ninja'dan önce çalışıyor, paralellik bayrağı
  onu ilgilendirmiyor. Çözüm `.wslconfig`'te bellek artırmak.
  Teşhis: `dmesg | grep -i oom-kill`
- `Got signal: terminated` / `exit status 143` derleme çıktısında **neredeyse
  her zaman OOM demek.** Windows Update veya uyku sanma; önce `dmesg`'e bak.
- **ccache bozulabiliyor** (`ccache: error: Not a directory`). Sıfırla:
  ```bash
  rm -rf ~/.cache/ccache ~/.ccache && ccache -M 50G
  ```
- **Android 16'da `lunch` formatı değişti:** `<ürün>-<release>-<varyant>`.
  Elle yazma — `breakfast beyond2lte` doğru kombinasyonu seçiyor,
  sonra `mka bacon -j4`.
- **Git submodule'leri `--recursive` ile al.** Aksi halde iç içe bağımlılıklar
  (ör. `webrtc-aec3`) eksik kalıyor ve hata derleme ortasında çıkıyor.
- `extract-files.py` adb üzerinden çekerken firmware bölümünde patlıyor
  (`cm.bin`, `modem.bin`, `sboot.bin`). `proprietary-firmware.txt` satırını
  yorum satırı yap; cihazda zaten doğru firmware var.

### Android sistem uygulamaları

- `ro.control_privapp_permissions=enforce` altında bir priv-app'in
  `signature|privileged` izinlerinden **biri bile** beyaz listede eksikse
  cihaz **açılmıyor** (`system_server` fatal exception).
  Fazla giriş yok sayılıyor. Asimetri budamanın aleyhine — şüphedeysen ekle.
- Teşhis için `pmsg-ramoops-0`'a bak, `dmesg-ramoops-0`'a değil.
  Java istisnası userspace tarafında.

---

## 4. Sık kullanılan komutlar

### Windows / PowerShell

```powershell
cd "$env:USERPROFILE\OneDrive\Masaüstü\projectfiles\platform-tools"

./adb devices
./adb shell getprop | Select-String "ro.build"
./adb logcat -b all -d > "$env:USERPROFILE\Desktop\log.txt"
./adb logcat -b crash -d
./adb shell dumpsys telephony.registry | findstr /i "mVoiceRegState"

# bootloop teşhisi (recovery'den de çalışır)
./adb shell cat /sys/fs/pstore/pmsg-ramoops-0
./adb shell cat /sys/fs/pstore/dmesg-ramoops-0
```

### WSL

```bash
cd ~/android/lineage
source build/envsetup.sh
breakfast beyond2lte
mka bacon -j4

dmesg | grep -i oom-kill        # derleme öldüyse İLK buraya bak
ccache -s
df -h .
```

---

## 5. Bu klasördeki diğer belgeler

VoLTE projesine özel; başka bir iş yapıyorsan gerekmez.

| Dosya | İçerik |
|---|---|
| `README.md` | Proje özeti, klasör kuralları, yayınlama politikası |
| `work/notes/01_bulgular.md` | Stock firmware + ses yolu analizi |
| `work/notes/02_linux_derleme_ortami.md` | Derleme ortamı kurulumu |
| `work/notes/03_adim_adim_derleme.md` | Sekiz aşamalı derleme rehberi |
| `work/notes/04_agac_guncelleme.md` | Kaynak ağacını güncel tutma |
| `work/patches/exynos9820-common/` | PhhIms entegrasyon yamaları + `apply.sh` |
| `work/scripts/update_tree.sh` | Sync + yamaları yeniden uygula |
| `capture/` | Cihaz dökümleri — **hassas veri içerir, paylaşma** |
