#!/usr/bin/env bash
# UNIPRO mclk_rate'i cihazdan oku.
# LineageOS "Rooted debugging" acikken calistir.
#
# Windows PowerShell'de:  bash mclk_oku.sh   (veya komutlari tek tek elle)
# Mac/WSL'de:             bash mclk_oku.sh

set -u
adb wait-for-device

echo "==> adbd'yi root'a al (LineageOS 'Rooted debugging')"
adb root && sleep 2 && adb wait-for-device
echo "    whoami: $(adb shell whoami 2>&1)"
echo

echo "==> YOL 1: HCI_1US_TO_CNT_VAL register'i (en guvenilir)"
echo "    VS_HCI base 0x13D61100 + offset 0x0C = 0x13D6110C"
echo "    deger & 0x3FF = mclk_rate MHz cinsinden"
RAW=$(adb shell 'devmem 0x13D6110C 32' 2>&1)
echo "    ham: $RAW"
python3 - "$RAW" <<'EOF' 2>/dev/null || echo "    (devmem yok veya izin reddedildi -> YOL 2'ye bak)"
import sys, re
m = re.search(r'0x[0-9a-fA-F]+', sys.argv[1])
if m:
    v = int(m.group(0), 16) & 0x3FF
    print(f"    -> mclk_rate = {v} MHz  = {v*1000000} Hz")
    print(f"    -> UNIPRO_MCLK_PERIOD = {1000000000//(v*1000000)} ns")
EOF
echo

echo "==> YOL 2: clk_summary (debugfs)"
adb shell 'mount -t debugfs none /sys/kernel/debug 2>/dev/null; \
           grep -iE "unipro|ufs" /sys/kernel/debug/clk/clk_summary' 2>&1 | head -20
echo

echo "==> YOL 3: dmesg'de UFS izleri"
adb shell 'dmesg | grep -iE "ufs.*(gear|lane|link|mclk)"' 2>&1 | head -10
echo

echo "==> Bonus: UFS cihaz kimligi"
adb shell 'cat /sys/block/sda/device/model /sys/block/sda/device/rev' 2>&1
