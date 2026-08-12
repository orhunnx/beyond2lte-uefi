#!/usr/bin/env bash
#
# boot.wim'i baska bir sikistirmayla yeniden yazar.
#
#   bash tools_wim_sikistirma.sh none      <- sikistirma yok  (en hizli okuma)
#   bash tools_wim_sikistirma.sh fast      <- XPRESS
#   bash tools_wim_sikistirma.sh lzx       <- varsayilana don
#
# ---------------------------------------------------------------------------
# NEDEN
# ---------------------------------------------------------------------------
# Uc olcum ayni sayiyi verdi: Windows KENDI CPU'sunda ~10 saniye calisip
# oluyor. Duvar saati 10 ile 26 saniye arasinda geziniyor ama bu sayi
# degismiyor - fren deneyinde CPU'yu yariya indirince duvar saati iki katina
# ciktigi halde CPU suresi yerinde kaldi.
#
# Sonra butun acilis surucileri kapatildi (korumali kume disinda) ve TIK yine
# ~100 cikti. Yani o 10 saniyelik is SURUCULERDEN gelmiyor.
#
# Geriye WinPE'nin kendi yapisi kaliyor: RAM diskteki boot.wim LZX ile
# sikistirilmis ve her dosya erisiminde ACILIYOR. O asamadaki en agir CPU
# tuketicisi bu.
#
# Sikistirmayi kaldirmak, is miktarini buyuk olcude degistirebilecegimiz ilk
# gercek kaldirac:
#
#   TIK belirgin duserse -> o 10 saniye ACMA isiydi ve olum bir DIZI
#       NOKTASINDA oluyor. Windows oraya daha cabuk variyor.
#   TIK ~100'de kalirsa  -> is acma degil, ve "sabit CPU suresi" gercekten
#       islenen is miktarindan bagimsiz bir esik demek. O cok daha tuhaf ve
#       cok daha dar bir aday kumesi birakir.
#
# ---------------------------------------------------------------------------
# BOYUT
# ---------------------------------------------------------------------------
# LZX'li hali 742 MB, acilmis icerik 1650 MiB. SYSTEM bolumu 5.57 GB, yani
# sikistirmasiz hali rahat siger. Winload ramdiski RAM'e okurken daha uzun
# surer ama o sure b2lfb baslamadan ONCE geciyor ve TIK'e girmiyor.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$HERE/esp/sources/boot.wim"
MOD="${1:-}"

case "$MOD" in
  none|fast|lzx) ;;
  *) sed -n '2,10p' "$0"; exit 1 ;;
esac

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -s "$WIM" ] || { echo "boot.wim yok: $WIM"; exit 1; }
command -v wimlib-imagex >/dev/null || { echo "wimlib-imagex yok"; exit 1; }

ONCE=$(stat -c%s "$WIM")
say "Mevcut boot.wim: $((ONCE/1024/1024)) MB"

#
# DOGRULAMA ICIN ONCE ICERIGI SAY.
#
# export butun dosyalari kopyalar ama bunu VARSAYMAK, bu projede defalarca
# bedel odedigimiz sey. Once/sonra dosya sayisi ve b2lfb.sys'in varligi
# karsilastiriliyor; tutmuyorsa is durduruluyor ve eski dosya yerinde kaliyor.
#
ADET_ONCE=$(wimlib-imagex dir "$WIM" 1 2>/dev/null | wc -l)
wimlib-imagex dir "$WIM" 1 2>/dev/null | grep -qi 'b2lfb.sys' \
  || { echo "b2lfb.sys mevcut WIM'de yok - once tools_b2lfb.sh calistir"; exit 1; }

YENI="$(mktemp -d)/boot.wim"
say "Yeniden yaziliyor: --compress=$MOD"
wimlib-imagex export "$WIM" 1 "$YENI" --compress="$MOD" >/dev/null

ADET_SONRA=$(wimlib-imagex dir "$YENI" 1 2>/dev/null | wc -l)
wimlib-imagex dir "$YENI" 1 2>/dev/null | grep -qi 'b2lfb.sys' \
  || { echo "! b2lfb.sys yeni WIM'de YOK - eski dosya korundu"; exit 1; }

if [ "$ADET_ONCE" != "$ADET_SONRA" ]; then
  echo "! dosya sayisi tutmadi: $ADET_ONCE -> $ADET_SONRA. Eski dosya korundu."
  exit 1
fi

mv -f "$YENI" "$WIM"
SONRA=$(stat -c%s "$WIM")
echo "  $((ONCE/1024/1024)) MB -> $((SONRA/1024/1024)) MB, $ADET_SONRA kayit dogrulandi"
echo
echo "Simdi: bash cache_esp.sh --win --system"
