#!/usr/bin/env bash
# WSL: keep diagnostic inbox INFs outside Inf2Cat's package directory.
set -euo pipefail

[ "$#" -ge 2 ] && [ "$#" -le 3 ] || { echo "Kullanim: build-catalog.sh OUT INF2CAT [RUNTIME_MANIFEST]" >&2; exit 1; }
OUT="$(cd "$1" && pwd)"
INF2CAT="$2"
# Never leave a previous catalog looking like this run's successful output.
rm -f "$OUT/b2lusb.cat"
# Inf2Cat runs as a Windows process. Do not stage under the WSL repository:
# its UNC provider does not have the same path semantics as a local drive.
# The command is static; never interpolate shell paths into PowerShell code.
WINDOWS_TEMP="$(powershell.exe -NoProfile -NonInteractive -Command \
  '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); [System.IO.Path]::GetTempPath()' | tr -d '\r')"
[[ "$WINDOWS_TEMP" =~ ^[A-Za-z]:[\\/] ]] || {
  echo "HATA: Windows TEMP yerel surucu yolu olmali: $WINDOWS_TEMP" >&2; exit 1;
}
WSL_TEMP="$(wslpath -u "$WINDOWS_TEMP")"
[ -d "$WSL_TEMP" ] || { echo "HATA: Windows TEMP WSL'den okunamiyor: $WSL_TEMP" >&2; exit 1; }
STAGE="$(mktemp -d "$WSL_TEMP/b2lusb-inf2cat.XXXXXX")"
PENDING=""
cleanup() {
  rm -rf "$STAGE"
  [ -z "$PENDING" ] || rm -f "$PENDING"
}
trap cleanup EXIT
WINDOWS_STAGE="$(wslpath -w "$STAGE")"
[[ "$WINDOWS_STAGE" =~ ^[A-Za-z]:[\\/] ]] || {
  echo "HATA: Inf2Cat paket dizini yerel Windows yolu degil: $WINDOWS_STAGE" >&2; exit 1;
}

# Exact distribution payload. Do not glob *.inf or copy the output tree:
# check-winpe-runtime.py intentionally preserves original inbox INF evidence.
PAYLOAD=(b2lusb.inf b2lusb-host.inf b2lusb-function.inf b2lmax77705.inf \
            b2lusb.sys b2lusbmon.sys b2lmax77705.sys)
if [ "$#" -eq 3 ]; then
  # An explicit manifest selects this payload; a stale SYS in OUT cannot do so.
  python3 - "$OUT" "$3" <<'PYVERIFY'
import hashlib, json, sys
from pathlib import Path
out, manifest = map(Path, sys.argv[1:])
record = json.loads(manifest.read_text())
if record['profile'] != 'experimental-ufx-overlay-v2':
    raise SystemExit('HATA: kaynak SYS icin profil v2 gerekli')
if not (out/'b2lusb-function.inf').read_text().startswith('; B2L_PACKAGED_SYNOPSYS=1\n'):
    raise SystemExit('HATA: kaynak SYS function INF tarafindan sahiplenilmiyor')
if hashlib.sha256((out/'ufxsynopsys.sys').read_bytes()).hexdigest() != record['files']['/Windows/System32/drivers/ufxsynopsys.sys']:
    raise SystemExit('HATA: kaynak Synopsys SYS hash farkli')
PYVERIFY
  PAYLOAD+=(ufxsynopsys.sys)
fi
for Item in "${PAYLOAD[@]}"; do
  [ -s "$OUT/$Item" ] || { echo "HATA: Katalog paket dosyasi yok/bos: $Item" >&2; exit 1; }
  cp "$OUT/$Item" "$STAGE/$Item"
  cmp -s "$OUT/$Item" "$STAGE/$Item" || {
    echo "HATA: Windows TEMP kopyasi farkli: $Item" >&2; exit 1;
  }
done

echo "Inf2Cat: yalniz B2LUSB paketi (${#PAYLOAD[@]} dosya)"
echo "Inf2Cat yerel Windows dizini: $WINDOWS_STAGE"
# Use the local staging directory as the process working directory as well.
(cd "$STAGE" && "$INF2CAT" /driver:"$WINDOWS_STAGE" /os:10_GE_ARM64)
[ -s "$STAGE/b2lusb.cat" ] || { echo "HATA: Inf2Cat b2lusb.cat uretmedi" >&2; exit 1; }
PENDING="$(mktemp "$OUT/.b2lusb.cat.XXXXXX")"
cp "$STAGE/b2lusb.cat" "$PENDING"
if ! cmp -s "$STAGE/b2lusb.cat" "$PENDING"; then
  echo "HATA: Katalog geri kopyasi farkli" >&2
  exit 1
fi
mv "$PENDING" "$OUT/b2lusb.cat"
