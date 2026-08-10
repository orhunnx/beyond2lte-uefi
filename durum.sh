#!/usr/bin/env bash
#
# Tek komutluk durum raporu. Tur kaybetmemek icin: "su dosya var mi, kac MB,
# imaja girdi mi" sorularinin hepsini bir cikti ile cevaplar.
#
#   bash durum.sh
#
set -uo pipefail
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

say "esp/ agaci"
if [ -d "$PROJ/esp" ]; then
  find "$PROJ/esp" -type f -printf '%10s  %p\n' 2>/dev/null \
    | sort -rn | head -20 | awk '{printf "  %8.1f MB  %s\n", $1/1048576, $2}'
  echo "  --------"
  echo "  TOPLAM: $(du -sh "$PROJ/esp" | cut -f1)   (bolum 600 MiB)"
else
  echo "  esp/ YOK"
fi

say "boot.wim"
if [ -f "$PROJ/esp/sources/boot.wim" ]; then
  ls -lh "$PROJ/esp/sources/boot.wim" | awk '{print "  "$5"  "$9}'
  command -v wimlib-imagex >/dev/null && \
    wimlib-imagex info "$PROJ/esp/sources/boot.wim" 2>/dev/null \
      | grep -E '^(Image Count|Index|Name|Compression|Chunk Size)' | sed 's/^/    /'
else
  echo "  YOK  ->  bash tools_winboot.sh"
fi

say "uretilmis imaj"
for f in "$PROJ/out/cache.img" "$PROJ/out/cache-esp.tar"; do
  [ -f "$f" ] && ls -lh "$f" | awk '{print "  "$5"  "$9}' || echo "  yok: $f"
done

if [ -f "$PROJ/out/cache.img" ] && command -v mdir >/dev/null; then
  export MTOOLS_SKIP_CHECK=1
  say "imajin icerigi"
  mdir -i "$PROJ/out/cache.img" ::/ 2>/dev/null | sed 's/^/  /'
  echo "  --- sources/ ---"
  mdir -i "$PROJ/out/cache.img" ::/sources 2>/dev/null | sed 's/^/  /' \
    || echo "  sources/ YOK"
fi

say "araclar"
for t in 7z wimlib-imagex mcopy mkfs.vfat img2simg iasl; do
  printf "  %-16s %s\n" "$t" "$(command -v $t || echo YOK)"
done

say "disk"
df -h "$PROJ" /tmp 2>/dev/null | sed 's/^/  /'
