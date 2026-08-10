#!/usr/bin/env bash
#
# ACPI testi icin initramfs uretir.
#
#   bash linux_test/kur.sh
#
# Cikti: esp/initramfs-test.gz   (cache_esp.sh bunu otomatik iceri alir)
#
# NEREDEN BUSYBOX:
# busybox.net'te aarch64 static binary YOK. Oradaki "armv8l" 32 bit ARM,
# bu cihazda calismaz (ilk surumde o URL denendi, 404 dondu; donseydi de
# yanlis mimari olurdu).
#
# Onun yerine elimizdeki Alpine initramfs-lts'i taban aliyoruz: icinde
# dogru mimaride busybox, musl kutuphanesi ve kernel modulleri hazir duruyor.
# Tek yaptigimiz /init'i kendi script'imizle degistirmek. Boylece hicbir sey
# indirmiyoruz ve mimari uyusmazligi imkansiz.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(dirname "$HERE")"
SRC="$PROJ/esp/initramfs-lts"
OUT="$PROJ/esp/initramfs-test.gz"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -f "$SRC" ] || {
  echo "esp/initramfs-lts bulunamadi."
  echo "Alpine aarch64 netboot'tan indir:"
  echo "  cd esp && curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/netboot/initramfs-lts"
  exit 1
}

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/root"

say "initramfs-lts aciliyor"
# Sikistirma bicimi surume gore degisiyor (gzip / xz / zstd). Sihirli sayiya bak.
MAGIC=$(od -An -tx1 -N4 "$SRC" | tr -d ' ')
case "$MAGIC" in
  1f8b*)    DECOMP="gzip -dc"  ;;
  fd377a*)  DECOMP="xz -dc"    ;;
  28b52ffd) DECOMP="zstd -dc"  ;;
  *)        DECOMP="cat"       ;;   # sikistirilmamis cpio
esac
echo "  sihirli sayi: $MAGIC -> $DECOMP"

( cd "$W/root" && $DECOMP "$SRC" | cpio -idm --quiet )

[ -x "$W/root/bin/busybox" ] || { echo "HATA: icinde bin/busybox yok"; ls "$W/root"; exit 1; }
if command -v file >/dev/null; then
  file "$W/root/bin/busybox" | grep -q 'aarch64' \
    || { echo "HATA: busybox aarch64 degil:"; file "$W/root/bin/busybox"; exit 1; }
  echo "  busybox: $(file -b "$W/root/bin/busybox" | cut -c1-60)"
fi

say "kendi /init'imiz konuyor"
cp "$HERE/init" "$W/root/init"
chmod +x "$W/root/init"

say "yeniden paketleniyor"
( cd "$W/root" && find . | cpio -o -H newc --quiet | gzip -9 ) > "$OUT"

say "Cikti"
ls -lh "$OUT"
echo
echo "Sonra:"
echo "  bash cache_esp.sh"
echo "  cache'i flasla, ac, ses tusuyla Linux'u baslat"
