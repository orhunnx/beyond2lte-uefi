#!/usr/bin/env bash
#
# WinPE'deki BUTUN servislerin ErrorControl degerini 0 (IGNORE) yapar.
#
#   bash tools_winpe_hatakapat.sh
#
# ---------------------------------------------------------------------------
# NEDEN
# ---------------------------------------------------------------------------
# Elimizdeki tablo cok dar ve tek bir mekanizmaya isaret ediyor:
#
#   - Cekirdek ~10 saniye saglikli calisiyor (kalp atisi son ana kadar duzenli)
#   - Sonra TEMIZ bir yazilim sifirlamasi (RST_STAT 0x20000000)
#   - Bugcheck YOK (dogrulanmis geri cagri, hem olumlu hem olumsuz yonde)
#   - Kullanici moduna hic cikilmiyor (wpeutil shutdown calismiyor)
#   - Depolama hizi, menu beklemesi, startnet icerigi, watchdog, LPS0 -
#     hicbiri sureyi degistirmiyor
#
# Cekirdek baslatmasinda bir sey ters giderse Windows normalde BUGCHECK verir.
# Yeniden baslatmasi icin birinin ISTEMESI gerekiyor. Ve Windows'un tam bu
# noktada, bugcheck olmadan yeniden baslatmasini saglayan bilinen bir
# mekanizma var: SERVIS ErrorControl DEGERI.
#
#   0 IGNORE   surucu yuklenemezse devam et
#   1 NORMAL   gunluge yaz, devam et
#   2 SEVERE   LastKnownGood'a don ve YENIDEN BASLAT
#   3 CRITICAL LKG'ye don ve yeniden baslat; zaten LKG ise bugcheck
#
# SEVERE tam olarak gordugumuz seyi uretiyor: bugcheck olmadan temiz bir
# yeniden baslatma, kullanici modundan once, acilis surucileri
# baslatilirken.
#
# Bu telefonda WinPE'nin bekledigi donanimlarin cogu yok. Bir acilis
# surucusunun duşmesi hic sasirtici degil - sasirtici olan, dusmesinin
# SESSIZ bir yeniden baslatma uretmesi.
#
# ---------------------------------------------------------------------------
# NE YAPIYOR
# ---------------------------------------------------------------------------
# Butun servislerin ErrorControl'unu 0 yapiyor. Hangi surucunun dustugunu
# aramiyoruz - once "dusmek sifirlamaya sebep olmasin" diyoruz. Sifirlama
# durursa mekanizma dogrulanmis olur ve SONRA suclu aranir.
#
# Kendi surucumuz b2lfb ISTISNA: onun ErrorControl'u 3 kalmali, cunku
# yuklenemezse bunu EKRANDA gormek istiyoruz. tools_b2lfb.sh bu script'ten
# SONRA calisip degeri geri yaziyor.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$HERE/esp/sources/boot.wim"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -s "$WIM" ] || { echo "boot.wim yok - once tools_winboot.sh calistir."; exit 1; }
command -v wimlib-imagex >/dev/null || { echo "wimlib yok"; exit 1; }
command -v hivexsh       >/dev/null || { echo "hivexsh yok -> sudo apt install -y libhivex-bin"; exit 1; }
command -v hivexregedit  >/dev/null || { echo "hivexregedit yok -> sudo apt install -y libwin-hivex-perl"; exit 1; }

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

say "SYSTEM hive cikariliyor"
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$W" --no-acls >/dev/null
echo "  $(stat -c%s "$W/SYSTEM") bayt"

say "Servis listesi aliniyor"
printf 'cd ControlSet001\\Services\nls\nquit\n' | hivexsh "$W/SYSTEM" \
  | sed 's/\r$//' | grep -v '^$' > "$W/servisler.txt"
ADET=$(wc -l < "$W/servisler.txt")
echo "  $ADET servis"

[ "$ADET" -gt 10 ] || { echo "Servis listesi supheli derecede kisa - durduruluyor."; exit 1; }

#
# TEK .reg DOSYASI, TEK MERGE.
#
# Servis basina ayri cagri yuzlerce hivexregedit calistirmak demekti.
# hivexregedit birden fazla anahtari tek dosyada kabul ediyor.
#
# hivexsh'in setval'i KULLANILMIYOR: o komut dugumdeki DIGER butun degerleri
# siliyor ve bu daha once bes olcumu gecersiz kilmisti.
#
say "ErrorControl=0 yaziliyor"
: > "$W/hepsi.reg"
while IFS= read -r SRV; do
  [ -n "$SRV" ] || continue
  printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"ErrorControl"=dword:00000000\n\n' \
    "$SRV" >> "$W/hepsi.reg"
done < "$W/servisler.txt"
hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$W/SYSTEM" "$W/hepsi.reg"
echo "  $ADET servis islendi"

#
# GERI OKU. Yazip dogrulamamak, yazdigini varsaymaktir - bu proje o dersi
# uc kez aldi.
#
say "Dogrulaniyor"
ORNEK="$(head -5 "$W/servisler.txt")"
HATA=0
while IFS= read -r SRV; do
  [ -n "$SRV" ] || continue
  V="$(hivexget "$W/SYSTEM" "ControlSet001\\Services\\$SRV" ErrorControl 2>/dev/null || echo "?")"
  printf '  %-20s ErrorControl=%s\n' "$SRV" "$V"
  [ "$V" = "0" ] || HATA=1
done <<< "$ORNEK"
if [ "$HATA" = 1 ]; then
  echo "  ! Ornek servislerde ErrorControl 0 degil - yama tutmamis."
  exit 1
fi

#
# Once sil sonra ekle: "add" mevcut dosyanin uzerine yazmiyor ve bu bir
# tura mal olmustu.
#
say "Hive WIM'e geri yaziliyor"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$W/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
echo "  yazildi"

cat <<'SON'

SIRA:
  bash tools_b2lfb.sh          <- b2lfb'nin ErrorControl=3 degerini geri yazar
  bash cache_esp.sh --win --system

BEKLENEN OKUMA:
  Seritler 10'u geciyor  -> Mekanizma DOGRULANDI. Bir acilis surucusu
                            duşuyordu ve ErrorControl=SEVERE sessiz yeniden
                            baslatma uretiyordu. Sirada suclu aranir.
  Yine ~10 serit         -> ErrorControl degil. Bu eksen de kapanir.
SON
