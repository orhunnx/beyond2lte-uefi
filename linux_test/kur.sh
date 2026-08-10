#!/usr/bin/env bash
#
# ACPI testi icin minik initramfs uretir.
#
#   bash linux_test/kur.sh
#
# Cikti: esp/initramfs-test.gz   (cache_esp.sh bunu otomatik iceri alir)
#
# Icinde sadece static busybox ve linux_test/init var. Alpine'in hazir
# initramfs-lts'i kullanilmiyor cunku o kendi init'ini calistirip root
# ariyor ve bulamayinca panik veriyor - biz onun yerine dosyaya log
# yazdirmak istiyoruz.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(dirname "$HERE")"
OUT="$PROJ/esp/initramfs-test.gz"

BB_URL="https://busybox.net/downloads/binaries/1.35.0-aarch64-linux-musl/busybox"
BB="$HERE/busybox-aarch64"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

if [ ! -f "$BB" ]; then
  say "busybox indiriliyor (aarch64, static)"
  curl -fL "$BB_URL" -o "$BB"
fi

# Dogru mimari mi? Yanlis busybox sessizce calismaz, kernel "no init found" der.
if command -v file >/dev/null; then
  file "$BB" | grep -q 'ARM aarch64' || { echo "HATA: busybox aarch64 degil:"; file "$BB"; exit 1; }
fi
chmod +x "$BB"

say "initramfs kuruluyor"
W="$(mktemp -d)"
mkdir -p "$W"/{bin,dev,proc,sys,mnt}
cp "$BB" "$W/bin/busybox"
cp "$HERE/init" "$W/init"
chmod +x "$W/init" "$W/bin/busybox"

( cd "$W" && find . | cpio -o -H newc --quiet | gzip -9 ) > "$OUT"
rm -rf "$W"

say "Cikti"
ls -lh "$OUT"
echo
echo "Sonra:"
echo "  bash cache_esp.sh     # initramfs'i ESP'ye koyar"
echo "  cache'i flasla, ac, startup.nsh kernel'i baslatacak"
echo "  bitince recovery'den cache'i cekip KLOG.TXT'i oku"
