# Cihazdan log çekme (recovery üzerinden)

`startup.nsh` çıktıları `fs0:\log\*.txt` altına yazıyor. `fs0` = **CACHE**
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
./adb shell "mkdir -p /tmp/esp; mount -t vfat /dev/block/by-name/cache /tmp/esp && ls -l /tmp/esp/log"
```

Listeleniyorsa:

```powershell
./adb pull /tmp/esp/log ./beyond2lte-log
./adb shell "umount /tmp/esp"
```

LineageOS recovery kernel'inde vfat desteği genelde var (USB-OTG için).

## Yol B — mount olmuyorsa: bölümü ham çek (garantili)

75 MB, USB üzerinden yarım dakika sürer.

```powershell
./adb exec-out "dd if=/dev/block/by-name/cache bs=4M" > cache_read.img
```

`exec-out` şart — `adb shell` ikili veriyi bozar.

Sonra WSL'de `mtools` ile içinden oku (mount ve sudo gerekmez):

```bash
cd /mnt/c/Users/orhun/OneDrive/Masaüstü/projectfiles/platform-tools
export MTOOLS_SKIP_CHECK=1
mdir  -i cache_read.img ::/log
mcopy -i cache_read.img -s ::/log ~/beyond2lte-log/
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
