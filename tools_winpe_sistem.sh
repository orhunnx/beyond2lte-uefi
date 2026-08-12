#!/usr/bin/env bash
#
# WinPE'deki SISTEM surucilerini (Start=1) listeler ve kapatir.
#
#   bash tools_winpe_sistem.sh liste
#   bash tools_winpe_sistem.sh kapat     <- hepsini Start=4 yapar
#   bash tools_winpe_sistem.sh ac        <- hepsini Start=1'e geri alir
#   bash tools_winpe_sistem.sh tek AD    <- YALNIZCA o servisi Start=4 yapar
#
# "tek" modu bisect icin: b2lfb artik olurken yuklenen imajin adini
# kaydediyor (IMG=npsvctrig.sys, CNT=00000010). Hepsini birden kapatmak
# "durdu mu" sorusunu cevapliyor ama HANGISI sorusunu cevaplamiyor; teker
# teker kapatip IMG'nin nereye kaydigina bakmak cevapliyor.
#
# ---------------------------------------------------------------------------
# NEDEN - HIC DOKUNULMAMIS BIR EKSEN
# ---------------------------------------------------------------------------
# Sureli blokaj olcumu su sayiyi verdi:
#
#     REL=0053   TIK=0063     ->  birakildiktan sonra TAM 10 SANIYE
#
# Yani tetikleyiciye ulasmak icin acilis surucileri bittikten sonra on
# saniyelik GERCEK IS gerekiyor. Neredeyse bos bir ACPI ad alanini taramak
# saniyeler surmez; on saniye ya bir zaman asimi ya da SIKISTIRILMIS WIM'den
# surucu yuklemek demektir.
#
# Ve burada bir bosluk var: tools_winpe_surucu.sh yalnizca ACILIS
# surucilerini (Start=0) kapatiyor - o liste ilk "liste" ciktisindaki 54
# isimden kurulmustu. START=1 SURUCULERINE HIC DOKUNMADIK.
#
# Ustelik Start=1 blokaj denemesinde surucumuz Group="" ile o grubun da EN
# SONUNDAYDI, yani ondan onceki Start=1 surucileri yuklenirken olmus
# olabiliriz - ve bant hic cikmadigi icin bunu "sifirlama daha once oldu"
# diye okumustuk.
#
# ---------------------------------------------------------------------------
# WinPE BOZULURSA SORUN DEGIL
# ---------------------------------------------------------------------------
# Amac WinPE'yi acmak degil, SIFIRLAMANIN DURUP DURMADIGINI olcmek. Kalp
# atisi Start=0 surucumuzden geliyor ve Start=1 surucileri kapali olsa da
# calisiyor. Seritler 10'u geciyorsa suclu o listede.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$HERE/esp/sources/boot.wim"
KOMUT="${1:-liste}"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -s "$WIM" ] || { echo "boot.wim yok."; exit 1; }
for t in wimlib-imagex hivexsh hivexregedit hivexget; do
  command -v $t >/dev/null || { echo "$t yok"; exit 1; }
done

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

say "SYSTEM hive cikariliyor"
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$W" --no-acls >/dev/null

printf 'cd ControlSet001\\Services\nls\nquit\n' | hivexsh "$W/SYSTEM" \
  | sed 's/\r$//' | grep -v '^$' > "$W/hepsi.txt"

#
# BIZIM SURUCUMUZ HARIC. b2lfb Start=0 ama listeye karismasin diye acikca
# atlanıyor - kalp atisi olcumun kendisi, onu kaybedemeyiz.
#
say "Start=1 servisleri araniyor"
: > "$W/sistem.txt"
while IFS= read -r SRV; do
  [ -n "$SRV" ] || continue
  [ "$SRV" = "b2lfb" ] && continue
  V="$(hivexget "$W/SYSTEM" "ControlSet001\\Services\\$SRV" Start 2>/dev/null || echo "")"
  [ "$V" = "1" ] && echo "$SRV" >> "$W/sistem.txt"
done < "$W/hepsi.txt"
ADET=$(wc -l < "$W/sistem.txt")
echo "  $ADET adet Start=1 servisi"

if [ "$KOMUT" = "liste" ]; then
  echo
  cat "$W/sistem.txt" | sed 's/^/  /'
  echo
  echo "Kapatmak icin: bash tools_winpe_sistem.sh kapat"
  exit 0
fi

case "$KOMUT" in
  kapat) HEDEF=4 ;;
  ac)    HEDEF=1 ;;
  tek)
    HEDEF=4
    AD="${2:-}"
    [ -n "$AD" ] || { echo "Kullanim: bash tools_winpe_sistem.sh tek <servis>"; exit 1; }
    #
    # Listeyi tek isme indiriyoruz. Isim gercekten Start=1 listesinde
    # degilse duruyoruz: olmayan bir servisi "kapattik" sanip sonucu
    # yorumlamak, bu projede birkac kez yaptigimiz hatanin aynisi olur.
    #
    grep -qx "$AD" "$W/sistem.txt" || {
      echo "'$AD' Start=1 listesinde yok. Liste:"
      sed 's/^/  /' "$W/sistem.txt"
      exit 1
    }
    echo "$AD" > "$W/sistem.txt"
    ADET=1
    ;;
  *)     echo "Bilinmeyen komut: $KOMUT (liste|kapat|ac|tek)"; exit 1 ;;
esac

[ "$ADET" -gt 0 ] || { echo "Islenecek servis yok."; exit 1; }

#
# Tek .reg, tek merge. hivexsh'in setval'i KULLANILMIYOR: o komut dugumdeki
# diger butun degerleri siliyor ve bu daha once bes olcumu gecersiz kilmisti.
#
say "Start=$HEDEF yaziliyor"
: > "$W/yaz.reg"
while IFS= read -r SRV; do
  [ -n "$SRV" ] || continue
  printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"Start"=dword:0000000%s\n\n' \
    "$SRV" "$HEDEF" >> "$W/yaz.reg"
done < "$W/sistem.txt"
hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$W/SYSTEM" "$W/yaz.reg"

#
# GERI OKU. Yazip dogrulamamak, yazdigini varsaymaktir.
#
say "Dogrulaniyor"
HATA=0
while IFS= read -r SRV; do
  [ -n "$SRV" ] || continue
  V="$(hivexget "$W/SYSTEM" "ControlSet001\\Services\\$SRV" Start 2>/dev/null || echo "?")"
  [ "$V" = "$HEDEF" ] || { echo "  ! $SRV -> $V (beklenen $HEDEF)"; HATA=1; }
done < <(head -5 "$W/sistem.txt")
[ "$HATA" = 0 ] && echo "  ornek servisler dogru" || exit 1

say "Hive WIM'e geri yaziliyor"
wimlib-imagex update "$WIM" 1 \
  --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
wimlib-imagex update "$WIM" 1 \
  --command="add '$W/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
echo "  $ADET servis Start=$HEDEF"

cat <<'SON'

SIRA:
  bash tools_b2lfb.sh          <- b2lfb'nin kaydini geri yazar
  bash cache_esp.sh --win --system

BEKLENEN OKUMA:
  Seritler 10'u geciyor  -> Suclu Start=1 listesinde. Ikili aramayla bulunur.
  Yine ~10 serit         -> Start=1 de degil; geriye cekirdegin kendi
                            Faz 1 isleri kaliyor.
SON
