# Cihazdan log çekme (recovery üzerinden)

`startup.nsh` çıktıları `fs0:\LOG_*.txt` altına yazıyor. `fs0` = **CACHE**
bölümü (`HD(28)`). Recovery'den çekiyoruz.

Fotoğraf çekmeye gerek yok — tam, okunabilir metin.

## Sıra

1. UEFI'yi aç, `startup.nsh` çalışsın, bitsin (ekranda "Log dosyaları yazıldı")
2. Kapat, **Bixby + Ses Açma + USB** ile recovery'ye gir
3. Aşağıdaki yollardan biri

## Yol A — recovery FAT'i mount edebiliyorsa (hızlı)

PowerShell:

```powershell
cd "$env:USERPROFILE\OneDrive\Masaüstü\projectfiles\platform-tools"
./adb shell "mkdir -p /tmp/esp; mount -t vfat /dev/block/by-name/cache /tmp/esp && ls -l /tmp/esp/LOG_*"
```

Listeleniyorsa:

```powershell
./adb pull /tmp/esp ./beyond2lte-log
./adb shell "umount /tmp/esp"
```

LineageOS recovery kernel'inde vfat desteği genelde var (USB-OTG için).

## Yol B — mount olmuyorsa: bölümü çek (garantili)

**PowerShell'de `>` KULLANMA.** Metin yönlendirmesidir, çıktıyı UTF-16'ya
çevirir ve ikili veriyi bozar. Denendi: 75 MB'lık imaj yerine 40 baytlık
UTF-16 hata metni geldi.

Doğrusu: cihazda dosyaya yaz, sonra `adb pull` ile çek. `adb pull` ikili güvenli.

```powershell
cd "$env:USERPROFILE\OneDrive\Masaüstü\projectfiles\platform-tools"
./adb shell "dd if=/dev/block/by-name/cache of=/tmp/cache_read.img bs=4M"
./adb pull /tmp/cache_read.img
./adb shell "rm /tmp/cache_read.img"
```

`dd` çıktısında yine `18+1 records` görmelisin. `/tmp` 75 MB'ı kaldırıyor —
imajı oraya push ederken zaten doğrulanmıştı.

Sonra WSL'de `mtools` ile içinden oku (mount ve sudo gerekmez):

```bash
export MTOOLS_SKIP_CHECK=1
mdir  -i cache_read.img ::/
mcopy -i cache_read.img "::/LOG_*" ~/beyond2lte-log/
```

## Neden `>a`

UEFI Shell varsayılan olarak **UCS-2** yazar; dosya PC'de her karakter arasında
sıfır bayt içerir. `>a` ASCII yazdırır, doğrudan okunur.

## Sınır

Linux kernel'i başladıktan sonraki çıktı dosyaya yazılamaz — o aşamada Shell
devrede değil. Kernel logu için hâlâ ekran fotoğrafı gerekiyor.

Çözüm ileride: `efi_earlycon` yerine kernel'in `efivar` üzerinden log
bırakması ya da bir `initramfs` script'iyle logu ESP'ye yazmak. Şimdilik
ACPI doğrulaması dosyadan, Linux çıktısı ekrandan.

## mtools yoksa

Depoda `tools_fatread.py` var — saf Python, FAT16 okur, mount/sudo/mtools
gerektirmez. Bu imajı çözmek için bir kez yazıldı, duruyor:

```bash
python3 tools_fatread.py cache_read.img            # listele
python3 tools_fatread.py cache_read.img ./cikan    # cikar
```

## Doğrulama: doğru startup.nsh mi gitti

Bir tur, cihazdan okunan imajda **eski** `startup.nsh` çıktı — kernel'ler
eklendikten sonra ama script düzenlenmeden önce üretilmiş bir imaj
flaşlanmıştı, o yüzden hiç log oluşmadı. `cache_esp.sh` artık koyduğu
dosyanın boyut+sha1'ini basıyor. Geri okurken karşılaştır:

```bash
sha1sum esp/startup.nsh
python3 tools_fatread.py cache_read.img ./cikan && sha1sum cikan/EFI/BOOT/STARTUP.NSH
```

## Bölüm boyutu notu

CACHE **600 MiB** (75 MB değil). PIT'teki blok sayıları 4096 baytlık UFS
mantıksal bloğu cinsinden; ilk çıkarımda 512 varsayılmıştı. `cache_esp.sh`
hâlâ 75 MB'lık imaj yazıyor — çalışıyor, bölümün geri kalanı boş kalıyor.
Büyütmek gerekirse `SZ=629145600` ve `-s 8` (32K küme): 19200 küme çıkar,
FAT16'nın 4085-65525 aralığında kalır.
