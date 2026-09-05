#!/usr/bin/env bash
# WSL: keep diagnostic inbox INFs outside Inf2Cat's package directory.
set -euo pipefail

[ "$#" -eq 2 ] || { echo "Kullanim: build-catalog.sh OUT INF2CAT" >&2; exit 1; }
OUT="$(cd "$1" && pwd)"
INF2CAT="$2"
# Never leave a previous catalog looking like this run's successful output.
rm -f "$OUT/b2lusb.cat"
STAGE="$(mktemp -d "$OUT/.inf2cat.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

# Exact distribution payload. Do not glob *.inf or copy the output tree:
# check-winpe-runtime.py intentionally preserves original inbox INF evidence.
for Item in b2lusb.inf b2lusb-host.inf b2lusb-function.inf b2lmax77705.inf \
            b2lusb.sys b2lusbmon.sys b2lmax77705.sys; do
  [ -s "$OUT/$Item" ] || { echo "HATA: Katalog paket dosyasi yok/bos: $Item" >&2; exit 1; }
  cp "$OUT/$Item" "$STAGE/$Item"
done

echo "Inf2Cat: yalniz B2LUSB paketi (4 INF, 3 SYS)"
"$INF2CAT" /driver:"$(wslpath -w "$STAGE")" /os:10_GE_ARM64
[ -s "$STAGE/b2lusb.cat" ] || { echo "HATA: Inf2Cat b2lusb.cat uretmedi" >&2; exit 1; }
mv "$STAGE/b2lusb.cat" "$OUT/b2lusb.cat"
