#!/usr/bin/env bash
# beyond2lte (SM-G975F, Exynos 9820) donanim dump'i
# Mac'te calistir: bash dump_device.sh
# Gereksinim: adb (brew install android-platform-tools), USB debugging acik

set -u
OUT="$(cd "$(dirname "$0")" && pwd)/dump"
mkdir -p "$OUT"

echo "==> Cihaz bekleniyor..."
adb wait-for-device || exit 1

# Root var mi?
if adb shell 'su -c id' 2>/dev/null | grep -q 'uid=0'; then
  ROOT=1; SH() { adb shell "su -c '$1'"; }
  echo "==> Root: VAR"
else
  ROOT=0; SH() { adb shell "$1"; }
  echo "==> Root: YOK (bazi dump'lar eksik/maskeli olacak)"
fi

echo "==> Kimlik bilgileri"
adb shell getprop > "$OUT/getprop.txt" 2>&1
grep -E 'ro\.product|ro\.boot|ro\.hardware|ro\.board|ro\.revision' "$OUT/getprop.txt" \
  > "$OUT/identity.txt" 2>/dev/null

echo "==> Canli device tree (EN ONEMLI)"
adb pull /sys/firmware/fdt "$OUT/live.dtb" 2>&1 | tail -1
# Yedek yol: /proc/device-tree agaci
adb shell 'tar -c -C /proc device-tree 2>/dev/null' > "$OUT/device-tree.tar" 2>/dev/null
[ -s "$OUT/device-tree.tar" ] || rm -f "$OUT/device-tree.tar"

echo "==> Kernel / bellek / CPU"
adb shell cat /proc/cmdline   > "$OUT/cmdline.txt"   2>&1
adb shell cat /proc/cpuinfo   > "$OUT/cpuinfo.txt"   2>&1
adb shell cat /proc/meminfo   > "$OUT/meminfo.txt"   2>&1
adb shell cat /proc/version   > "$OUT/version.txt"   2>&1
adb shell 'cat /proc/config.gz' > "$OUT/config.gz"   2>/dev/null
[ -s "$OUT/config.gz" ] || rm -f "$OUT/config.gz"

echo "==> Bellek/IO haritasi (root gerekir)"
SH 'cat /proc/iomem'      > "$OUT/iomem.txt"      2>&1
SH 'cat /proc/interrupts' > "$OUT/interrupts.txt" 2>&1

echo "==> Partition tablosu"
adb shell 'ls -la /dev/block/by-name/' > "$OUT/partitions_byname.txt" 2>&1
adb shell 'cat /proc/partitions'       > "$OUT/partitions.txt"        2>&1

echo "==> Panel / ekran"
adb shell 'cat /sys/class/lcd/panel/lcd_type'  > "$OUT/panel_type.txt"  2>&1
adb shell 'cat /sys/class/lcd/panel/window_type' >> "$OUT/panel_type.txt" 2>&1
adb shell 'dumpsys display | head -60'         > "$OUT/display.txt"     2>&1

echo "==> UFS / depolama"
adb shell 'ls /sys/class/scsi_host/'                    > "$OUT/ufs.txt" 2>&1
adb shell 'cat /sys/block/sda/device/model 2>/dev/null' >> "$OUT/ufs.txt" 2>&1
adb shell 'cat /sys/block/sda/size 2>/dev/null'         >> "$OUT/ufs.txt" 2>&1

echo "==> Clock / regulator (root gerekir)"
SH 'cat /sys/kernel/debug/clk/clk_summary' > "$OUT/clk_summary.txt" 2>&1
SH 'cat /sys/kernel/debug/regulator/regulator_summary' > "$OUT/regulator_summary.txt" 2>&1

echo
echo "==> Bitti. Dosyalar: $OUT"
ls -la "$OUT"
echo
echo "En kritik dosya: live.dtb  ($(du -h "$OUT/live.dtb" 2>/dev/null | cut -f1))"
