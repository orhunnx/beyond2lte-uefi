#!/usr/bin/env bash
#
# WinPE'deki SISTEM surucilerini (Start=1) listeler ve kapatir.
#
#   bash tools_winpe_sistem.sh liste
#   bash tools_winpe_sistem.sh kapat     <- hepsini Start=4 yapar
#   bash tools_winpe_sistem.sh ac        <- hepsini Start=1'e geri alir
#   bash tools_winpe_sistem.sh tek AD    <- YALNIZCA o servisi Start=4 yapar
#   bash tools_winpe_sistem.sh sadece a b c  <- adi verilenler KAPALI,
#                                               kumedeki digerleri ACIK
#   bash tools_winpe_sistem.sh birak a b c   <- adi verilenler ACIK,
#                                               kumedeki digerleri KAPALI
#
# "sadece" ve "birak" idempotent: onceki durumdan bagimsiz, her cagri tam
# olarak istenen durumu kurar. Kume bisect'i icin gerekli - "kapat" ve "ac"
# birikimli calisiyor ve iki tur sonra neyin acik oldugunu kimse bilmiyor.
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
# Amac tek bir servisi "suclu" diye etiketlemek degil, baslangic zincirindeki
# zorunlu bagimliliklari kontrollu olarak ayirmak. Kalp atisi Start=0
# surucumuzden geliyor. Npfs deneyi, bir Start=1 servisinin hem hata turunu
# hem de omru 8.6 saniyeden 63.9 saniyeye degistirebildigini gosterdi;
# serit sayisi tek basina artik bir hizmeti elemez.
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
#
# HEDEF LISTE SABIT - DURUMDAN OKUNMUYOR.
#
# Ilk surum "su an Start=1 olanlar" diye ariyordu. Bir kez kapatinca hepsi
# Start=4 oluyor, liste bosaliyor ve "ac" komutu hicbir seyi geri
# acamiyor - "0 adet Start=1 servisi" deyip cikiyordu.
#
# Ayni hatayi surucu aracindaki adaylar() fonksiyonunda da yapmistim.
# Bir aracin hedefi, o aracin degistirdigi seye gore belirlenemez.
#
# Liste, cihazdan alinan ilk "liste" ciktisindaki 14 Start=1 servisi.
#
SISTEM_KUME="AFD CimFS Dfsc Msfs mssmbios NetBIOS NetBT Npfs npsvctrig \
nsiproxy Null rdbss tdx uiomap"

say "Start=1 servisleri"
: > "$W/sistem.txt"
for SRV in $SISTEM_KUME; do
  hivexget "$W/SYSTEM" "ControlSet001\\Services\\$SRV" Start >/dev/null 2>&1 \
    && echo "$SRV" >> "$W/sistem.txt"
done
ADET=$(wc -l < "$W/sistem.txt")
echo "  $ADET adet Start=1 servisi"

if [ "$KOMUT" = "liste" ]; then
  echo
  cat "$W/sistem.txt" | sed 's/^/  /'
  echo
  echo "Kapatmak icin: bash tools_winpe_sistem.sh kapat"
  exit 0
fi

if [ "$KOMUT" = "sadece" ] || [ "$KOMUT" = "birak" ]; then
  shift
  HEDEFLER="$*"
  [ -n "$HEDEFLER" ] || { echo "Kullanim: bash $0 $KOMUT <isim> [isim...]"; exit 1; }

  for H in $HEDEFLER; do
    grep -qix "$H" "$W/sistem.txt" \
      || { echo "'$H' kumede yok. Kume:"; sed 's/^/  /' "$W/sistem.txt"; exit 1; }
  done

  if [ "$KOMUT" = "sadece" ]; then
    say "Kapatilacak: $HEDEFLER"
  else
    say "Acik birakilacak: $HEDEFLER"
  fi
  : > "$W/yaz.reg"
  while IFS= read -r SRV; do
    [ -n "$SRV" ] || continue
    if [ "$KOMUT" = "sadece" ]; then D=1; else D=4; fi
    for H in $HEDEFLER; do
      if [ "$(echo "$SRV" | tr 'A-Z' 'a-z')" = "$(echo "$H" | tr 'A-Z' 'a-z')" ]; then
        if [ "$KOMUT" = "sadece" ]; then D=4; else D=1; fi
      fi
    done
    [ "$D" = 4 ] && printf '  %-16s KAPALI\n' "$SRV" || printf '  %-16s acik\n' "$SRV"
    printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"Start"=dword:0000000%s\n\n' \
      "$SRV" "$D" >> "$W/yaz.reg"
  done < "$W/sistem.txt"

  hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$W/SYSTEM" "$W/yaz.reg"

  HATA=0
  while IFS= read -r SRV; do
    [ -n "$SRV" ] || continue
    if [ "$KOMUT" = "sadece" ]; then BEK=1; else BEK=4; fi
    for H in $HEDEFLER; do
      if [ "$(echo "$SRV" | tr 'A-Z' 'a-z')" = "$(echo "$H" | tr 'A-Z' 'a-z')" ]; then
        if [ "$KOMUT" = "sadece" ]; then BEK=4; else BEK=1; fi
      fi
    done
    V="$(hivexget "$W/SYSTEM" "ControlSet001\\Services\\$SRV" Start 2>/dev/null || echo '?')"
    [ "$V" = "$BEK" ] || { echo "  ! $SRV -> $V, beklenen $BEK"; HATA=1; }
  done < "$W/sistem.txt"
  [ "$HATA" = 0 ] || { echo "Dogrulama basarisiz, hive yazilmadi."; exit 1; }
  echo "  hepsi dogrulandi"

  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$W/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
  echo
  echo "Simdi: bash cache_esp.sh --win --system"
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
  *)     echo "Bilinmeyen komut: $KOMUT (liste|kapat|ac|tek|sadece|birak)"; exit 1 ;;
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

OKUMA:
  BUG/TIK/QPD birlikte kaydedilir. Npfs, 0xc000021a'nin hizmet eksikliginden
  geldigi ve hizmet durumunun omru de degistirebildigi icin, tek acilisin
  suresiyle ikili arama yapilmaz. "birak" yalniz gerekli kullanici-modu
  bagimliliklarini geri koymak icin kullanilir.
SON
