#!/usr/bin/env bash
#
# WinPE'nin ACILIS SURUCULERINI listeler ve kapatir.
#
#   bash tools_winpe_surucu.sh liste            <- once bunu calistir
#   bash tools_winpe_surucu.sh kapat a b c ...  <- adi verilenleri devre disi birak
#   bash tools_winpe_surucu.sh ac    a b c ...  <- geri ac
#   bash tools_winpe_surucu.sh hepsi-kapat      <- korumali olmayan hepsi
#   bash tools_winpe_surucu.sh sadece-kapat X   <- hepsini ac, SADECE X kapali
#   bash tools_winpe_surucu.sh hepsi-ac         <- temel duruma don
#   bash tools_winpe_surucu.sh aralik A B      <- SADECE A..B arasi kapali
#   bash tools_winpe_surucu.sh sadece a b c    <- SADECE adi verilenler kapali
#
# "sadece" KUME BISECT ICIN. Korlemesine ikiye bolmek bagimliliklari
# kopariyor: NDIS kapali + Tcpip acik birakilinca NDIS.SYS 0xD1 ile cokuyor
# ve TIK cok kucuk kaliyor - yani asil olum noktasina VARMADAN bugcheck
# uretiliyor ve sonuc "suclu bulundu" diye yanlis okunabiliyor.
#
# "sadece" adi verilenleri kapatir, diger butun adaylari acar. Idempotent:
# onceki durumdan bagimsiz, her cagri tam olarak istenen durumu kurar.
#
# "aralik" IKILI ARAMA ICIN. Sinyal artik kesin ayriliyor:
#     surucular KAPALI -> Windows kullanici moduna variyor, 0xc000021a,
#                         yesil cubuk, BUG=E
#     surucular ACIK   -> ~10.7 saniyede sessiz sifirlama, BUG=H
# Yani suclu kapatilan kumedeyse BUG=E, aciktaysa BUG=H. Alti yedi acilista
# 54 adaydan tekine inilir.
#
# Liste sirasi KARARLI (hive'daki ls sirasi) ve "liste" komutu numaralari
# yaziyor; elle isim kopyalamaya gerek yok.
#
# ---------------------------------------------------------------------------
# FIKIR NEREDEN GELDI
# ---------------------------------------------------------------------------
# "Boot zincirindeki Windows asamalarini tek tek azaltsak sorunu neyin
# cikardigina gitmez miyiz?" - dogru soru. bootmgfw -> winload -> ntoskrnl
# zincirinin ilk ucu atlanamaz, biri olmadan digeri calismaz. Ama winload'un
# ntoskrnl'i baslatirken yaptigi bir is var ki tam supheli pencereye denk
# geliyor: ACILIS SURUCULERINI YUKLEMEK.
#
# SYSTEM hive'inda ControlSet001\Services altinda her surucunun bir "Start"
# degeri var:
#   0 = BOOT     winload yukluyor, cekirdekten once
#   1 = SYSTEM   cekirdek acilirken
#   2 = AUTO     oturum acilirken
#   3 = DEMAND   istege bagli
#   4 = DISABLED hic yuklenmiyor
#
# Sifir olanlari kapatarak ikiye bolebiliriz. Desen de bunu destekliyor:
# daire ciktiktan sonraki ~12 saniye hicbir seyle degismiyor (WIM icerigi,
# WinPE surumu, cekirdek sayisi, PSCI, storufs) - yani sabit bir is yapiliyor
# ve o isin icinde bir sey oluyor. PnP'nin aygitlari tarayip surucu bagladigi
# pencere tam orasi.
#
# ---------------------------------------------------------------------------
# DIKKAT: HEPSINI KAPATAMAZSIN
# ---------------------------------------------------------------------------
# Bazi suruculer olmadan Windows hic acilmaz ve elimizde "acilmadi" disinda
# bilgi kalmaz. Asagidaki liste korumali; "kapat" komutu bunlara dokunmuyor.
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIM="$HERE/esp/sources/boot.wim"

KOMUT="${1:-liste}"; shift || true

#
# KORUMALI KUME - kapatmak bilgi degil sessizlik uretir.
#
# ILK LISTE EKSIKTI VE OLCUMU BOZDU: hepsi kapatilinca sure 15 -> 5 saniyeye
# DUSTU. Daha erken olmek "suclu bulundu" demek degil; Windows acilis birimini
# hic goremeden pes etti demek.
#
# Sebep: WinPE RAM diskteki bir WIM'den calisiyor ve o WIM'i baglayan
# suruculeri de kapatmisiz.
#
#   WimFsf    WIM dosya sistemi filtresi - acilis birimi bununla goruluyor
#   FBWF      File-Based Write Filter, WinPE'nin RAM uzeri yazma katmani
#   Wof       Windows Overlay Filter, WIM destekli dosyalar
#   Fs_Rec    dosya sistemi tanimlayici
#   Mup       G/C yigininin ust katmani
#   FileInfo  filtre yoneticisi baglantisi
#   CLFS      cekirdegin gunluk altyapisi
#   KSecDD    cekirdek guvenlik surucusu - CNG'nin karsiligi
#   KSecPkg   ayni ailenin paketi
#   pcw       performans sayaclari, cekirdek erken istiyor
#
# Bunlar eklendikten sonra geriye SADECE DONANIM surucileri kaliyor - asil
# supheliler zaten onlar. Kume boylece hem daha kucuk hem daha anlamli.
#
#
# ORIJINALDE ACILIS SURUCUSU OLANLAR (ilk "liste" ciktisindan, 54 isim).
#
# BU LISTE BIR HATADAN DOGDU. "sadece-kapat" ilk surumu "korumali olmayan ve
# Start degeri 0 YA DA 4 olan her seyi ac" diyordu. Ama WinPE'de cdfs, udfs
# ve ws2ifsl MICROSOFT TARAFINDAN ZATEN KAPALI gelir - hic acilis surucusu
# degillerdir. Onlari acinca cihaz sunu verdi:
#
#   File: \windows\System32\Drivers\ws2ifsl.sys
#   Status: 0xc0000428  (STATUS_INVALID_IMAGE_HASH)
#   "the digital signature of a file or one of its dependencies couldn't
#    be verified"
#
# Yani Microsoft'un kapatmasinin sebebi var: o dosya WinPE'de acilis
# suruculeri icin gereken imza zincirine sahip degil.
#
# ONEMLI YAN KAZANC: bu ekran otuz turdur goremedigimiz seydi. Windows'un
# hata bildirme yolu BOZUK DEGILMIS - winload asamasinda bir sorun olunca
# gayet duzgun rapor ediyor. Demek ki sessiz 15 saniyelik cokus, winload'un
# hata yakalama alaninin DISINDA, daha ileride oluyor.
#
# Artik yalnizca bu listedeki isimlere dokunuyoruz. Listede olmayan hicbir
# servisin Start degeri degistirilmiyor.
#
TEMEL_ACIK="ACPI acpiex atapi cdrom CLFS CNG disk DXGKrnl EhStorClass EhStorTcgDrv FBWF FileInfo FltMgr Fs_Rec fvevol GenPass intelide KSecDD KSecPkg mountmgr Mup NDIS nvdimm nvmedisk partmgr pci pciide pcw pdc pmem PRM qcgpio_i QcTrEE_i sacdrv scmbus spaceport storahci stornvme storufs storvsc Tcpip TPM vdrvroot vmbus volmgr volmgrx volsnap volume vpci Wdf01000 WimFsf WindowsTrustedRT WindowsTrustedRTProxy Wof"

#
# b2lfb LISTEYE EKLENDI - OLCUM ARACININ KENDISI.
#
# "hepsi-kapat" korumali olmayan her acilis surucusunu devre disi birakiyor
# ve b2lfb de Start=0. Kapatilsaydi kalp atisi, TIK, SPN, MAX ve bugcheck
# dedektoru birden kaybolurdu; ekranda "hicbir sey olmadi" gorunurdu ve bunu
# "sifirlama durdu" diye okumak isten bile degildi.
#
# Olcum aracini olcumun icine katmamak gerekiyor.
#
KORUMALI="b2lfb ACPI acpiex CNG Wdf01000 WudfPf FltMgr Null volmgr partmgr disk mountmgr msisadrv pdc vdrvroot spaceport volume volsnap WimFsf FBWF Wof Fs_Rec Mup FileInfo CLFS KSecDD KSecPkg pcw"


#
# ===========================================================================
# DEGER YAZMA - hivexsh'in setval'i KULLANILMIYOR
# ===========================================================================
# BU BIR HATAYDI VE BES OLCUMU GECERSIZ KILDI.
#
# hivexsh'in "setval" komutu bir dugumde deger atarken O DUGUMDEKI DIGER
# BUTUN DEGERLERI SILIYOR. Yani her kapat/ac cagrisinda ilgili servisin
# ImagePath, Type, Group, ErrorControl alanlarini yok etmisiz; geriye
# yalnizca Start kalmis.
#
# Kapattigimiz suruciler icin zararsiz - zaten yuklenmiyorlar. Ama
# ACTIKLARIMIZ icin felaket: ImagePath'i olmayan bir surucuyu Windows
# yukleyemez. "sadece-kapat DXGKrnl" otuz kusur surucuyu "acti" ama hepsini
# kullanilamaz hale getirdi.
#
# Iki olcumun ayni cikmasi da bunu dogruluyordu: "hepsi kapali" 6 saniye,
# "sadece DXGKrnl kapali" yine 6 saniye. Ayni hasar, ayni sonuc.
#
# hivexregedit --merge yalnizca .reg dosyasindaki degerleri yaziyor,
# digerlerine dokunmuyor. Dogru arac bu.
#
STARTYAZ() {   # $1 = servis adi, $2 = 0 ya da 4
  local SRV="$1" DEG="$2" REGF="$W/set_$1.reg"
  printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"Start"=dword:0000000%s\n' "$SRV" "$DEG" > "$REGF"
  hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$HV/SYSTEM" "$REGF" 2>/dev/null
}

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -s "$WIM" ] || { echo "esp/sources/boot.wim yok. Once tools_winboot.sh calistir."; exit 1; }
command -v wimlib-imagex >/dev/null || { echo "wimlib yok -> sudo apt install -y wimtools"; exit 1; }
command -v hivexsh      >/dev/null || { echo "hivexsh yok -> sudo apt install -y libhivex-bin"; exit 1; }
#
# hivexregedit ARTIK ZORUNLU. hivexsh'in setval'i diger degerleri siliyor
# (yukaridaki aciklama); onsuz dogru yazma yapamayiz.
#
command -v hivexregedit >/dev/null || {
  echo
  echo "  ###############################################################"
  echo "  # hivexregedit YOK -> sudo apt install -y libwin-hivex-perl"
  echo "  #"
  echo "  # DIKKAT: bu paket libhivex-bin DEGIL, ayri bir pakettir."
  echo "  #"
  echo "  # Onsuz devam edilemez: hivexsh'in setval komutu servis"
  echo "  # dugumundeki ImagePath/Type/Group degerlerini SILIYOR ve"
  echo "  # acilan surucileri kullanilamaz hale getiriyor. Bes olcum bu"
  echo "  # yuzden gecersiz cikti."
  echo "  ###############################################################"
  echo
  exit 1
}

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
HV="$W/hive"; mkdir -p "$HV"

say "SYSTEM hive cikariliyor"
wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
  --dest-dir="$HV" --no-acls >/dev/null
echo "  $(stat -c%s "$HV/SYSTEM") bayt"

#
# Servis listesi. hivexsh'in "ls" ciktisi tek sutun; her servis icin Start
# degerini hivexget ile okuyoruz. Yavas ama okunakli ve bagimliligi az -
# hivexregedit (libwin-hivex-perl) her kurulumda olmayabiliyor.
#
servisler() {
  printf 'cd \\ControlSet001\\Services\nls\n' | hivexsh "$HV/SYSTEM" 2>/dev/null | sed '/^$/d'
}

#
# Korumali olmayan, numaralandirilabilir aday listesi.
#
# "aralik" bunun uzerinden calisiyor. Korumalilar listeye HIC girmiyor,
# boylece numaralar korumali kume degistiginde kaymiyor.
#
#
# ADAY KUMESI SABIT - DURUMA GORE DEGISMEZ.
#
# Ilk surum "su an Start=0 olanlar" diyordu ve bu bisect'i imkansiz kildi:
# bir aralik kapatilinca o isimler listeden dusuyor, bir sonraki cagrida
# liste 27'den 6'ya iniyordu. Her adimda zemin kayiyordu.
#
# Liste, cihazda alinan "liste" ciktisindaki KORUMASIZ VE O AN ACIK olan
# 30 surucuden olusuyor. O anda kapali olan 19 servis (aslen Start=1)
# kumeye HIC girmiyor: onlari Start=0 yapmak SYSTEM surucusunu acilis
# surucusune cevirmek olurdu ve aradigimizla ilgisi olmayan bozulmalar
# uretirdi.
#
ADAY_KUME="atapi cdrom DXGKrnl EhStorClass EhStorTcgDrv fvevol GenPass \
intelide NDIS nvdimm nvmedisk pci pciide pmem PRM qcgpio_i QcTrEE_i \
sacdrv scmbus storahci stornvme storufs storvsc Tcpip TPM vmbus volmgrx \
vpci WindowsTrustedRT WindowsTrustedRTProxy"

#
# HER ZAMAN KAPALI TUTULACAKLAR.
#
# Bunlari bir tur ADAY_KUME'ye eklemistim ve Start=0 yapmislardi. Kumeden
# CIKARMAK yetmedi: cikarmak "dokunma" demek, dokunulmayinca da Start=0
# olarak KALDILAR ve cihaz ayni hatayi tekrarladi:
#     File: \windows\system32\DRIVERS\udfs.sys   Status: 0xc0000428
#
# Bir degeri geri almak icin onu acikca YAZMAK gerekiyor. Listeden silmek
# yalnizca "artik degistirme" demek, "eski haline don" demek degil.
#
# Dordu de stok WinPE'de devre disi; bulundugumuz calisir durum buydu.
#
KAPALI_KUME="BasicDisplay BasicRender cdfs udfs ws2ifsl"

#
# BasicDisplay/BasicRender/cdfs/udfs/ws2ifsl BILEREK DISARIDA.
#
# Bir tur bunlari kumeye ekledim: "liste" ciktisinda KAPALI gorunuyorlardi
# ve arac yalnizca Start=0 ile Start=4'u listeledigi icin "demek ki aslen
# Start=0'dilar, bir hepsi-kapat kapatmis" diye cikardim.
#
# YANLISTI. Cihaz sunu verdi:
#     File: \windows\system32\DRIVERS\udfs.sys   Status: 0xc0000428
#
# udfs/cdfs/ws2ifsl stok WinPE'de zaten devre disi. Onlari acilis
# surucusune cevirince winload acilis zamani imza politikasiyla dogruluyor
# ve gecemiyorlar - acilis hic baslamiyor.
#
# Ders: bir aracin FILTRESINDEN geriye donuk durum cikarmak olcum degil
# tahmindir. Orijinal Start degerleri yalnizca dokunulmamis bir boot.wim'den
# okunabilir; gerektiginde ISO'daki kopyadan alinacak.

#
# Hive'da gercekten var olanlari suz. Bir isim WinPE surumune gore
# olmayabilir; olmayani "kapattik" saymak sessiz bir yanilgi olurdu.
#
adaylar() {
  for S in $ADAY_KUME; do
    hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start >/dev/null 2>&1 && echo "$S"
  done
}

case "$KOMUT" in

sadece)
  HEDEFLER="$*"
  [ -n "$HEDEFLER" ] || { echo "Kullanim: bash $0 sadece <isim> [isim...]"; exit 1; }

  LST="$(adaylar)"
  TOP=$(printf '%s\n' "$LST" | sed '/^$/d' | wc -l)

  #
  # ISIMLER LISTEDE VAR MI. Olmayan bir ismi "kapattik" sanip sonucu
  # yorumlamak, bu projede birkac kez odedigimiz bedel.
  #
  for H in $HEDEFLER; do
    printf '%s\n' "$LST" | grep -qix "$H" \
      || { echo "'$H' aday listesinde yok. Liste:"; printf '%s\n' "$LST" | sed 's/^/  /'; exit 1; }
  done

  say "Aday: $TOP   kapatilacak: $HEDEFLER"

  : > "$HV/yaz.reg"

  #
  # Once her zaman kapali tutulacaklar. Bunlar ADAY_KUME'de degil ama
  # DEGERLERI acikca yaziliyor - "dokunmamak" onlari eski haline
  # dondurmuyor.
  #
  for S in $KAPALI_KUME; do
    hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start >/dev/null 2>&1 || continue
    printf '  %-24s KAPALI (sabit)\n' "$S"
    printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"Start"=dword:00000004\n\n' \
      "$S" >> "$HV/yaz.reg"
  done

  while IFS= read -r S; do
    [ -n "$S" ] || continue
    D=0
    for H in $HEDEFLER; do
      [ "$(echo "$S" | tr 'A-Z' 'a-z')" = "$(echo "$H" | tr 'A-Z' 'a-z')" ] && D=4
    done
    [ "$D" = 4 ] && printf '  %-24s KAPALI\n' "$S" || printf '  %-24s acik\n' "$S"
    printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"Start"=dword:0000000%s\n\n' \
      "$S" "$D" >> "$HV/yaz.reg"
  done <<EOF4
$LST
EOF4

  hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$HV/SYSTEM" "$HV/yaz.reg"

  HATA=0
  for S in $KAPALI_KUME; do
    hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start >/dev/null 2>&1 || continue
    V="$(hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || echo '?')"
    [ "$V" = "4" ] || { echo "  ! $S -> $V, beklenen 4"; HATA=1; }
  done

  while IFS= read -r S; do
    [ -n "$S" ] || continue
    BEK=0
    for H in $HEDEFLER; do
      [ "$(echo "$S" | tr 'A-Z' 'a-z')" = "$(echo "$H" | tr 'A-Z' 'a-z')" ] && BEK=4
    done
    V="$(hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || echo '?')"
    [ "$V" = "$BEK" ] || { echo "  ! $S -> $V, beklenen $BEK"; HATA=1; }
  done <<EOF5
$LST
EOF5
  [ "$HATA" = 0 ] || { echo "Dogrulama basarisiz, hive yazilmadi."; exit 1; }
  echo "  hepsi dogrulandi"

  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$HV/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
  echo
  echo "Simdi: bash cache_esp.sh --win --system"
  exit 0
  ;;

aralik)
  #
  # ARGUMANLAR $1 VE $2 - "shift" YUZUNDEN.
  #
  # Betik yukarida "KOMUT=$1; shift" yapiyor, yani case'e gelindiginde
  # sayilar $1 ve $2'de. Ilk surum $2/$3 okuyordu ve B bos kaliyordu.
  #
  # Ayrica bosluk denetimi de kusurluydu: "$A$B" birlesimi "1" olunca
  # hepsi rakam sayilip geciyordu. Ikisi AYRI AYRI denetleniyor.
  #
  A="${1:-}"; B="${2:-}"
  case "$A" in *[!0-9]*|"") A="" ;; esac
  case "$B" in *[!0-9]*|"") B="" ;; esac
  if [ -z "$A" ] || [ -z "$B" ]; then
    echo "Kullanim: bash $0 aralik <ilk> <son>   ornek: aralik 1 14"
    exit 1
  fi

  LST="$(adaylar)"
  TOP=$(printf '%s\n' "$LST" | sed '/^$/d' | wc -l)
  [ "$B" -le "$TOP" ] || { echo "Listede $TOP aday var, $B istendi."; exit 1; }

  say "Aday sayisi: $TOP   kapatilacak: $A..$B"

  #
  # HANGI ISIMLER KAPANIYOR - NUMARALARIYLA.
  #
  # Ilk surumde yalnizca sayilar yaziliyordu ve hive'in "ls" sirasinin
  # alfabetik oldugunu VARSAYDIM. Varsayimla bisect yapmak, yanlis araligi
  # elemeye ve butun aramayi cope atmaya yol acar. Isimler ekranda olsun.
  #
  N=0
  while IFS= read -r S; do
    [ -n "$S" ] || continue
    N=$((N+1))
    if [ "$N" -ge "$A" ] && [ "$N" -le "$B" ]; then
      printf '  %2d  %-24s KAPALI\n' "$N" "$S"
    else
      printf '  %2d  %-24s acik\n' "$N" "$S"
    fi
  done <<EOF1
$LST
EOF1
  echo

  : > "$HV/yaz.reg"
  N=0
  KAPANAN=0
  while IFS= read -r S; do
    [ -n "$S" ] || continue
    N=$((N+1))
    if [ "$N" -ge "$A" ] && [ "$N" -le "$B" ]; then
      D=4; KAPANAN=$((KAPANAN+1))
    else
      D=0
    fi
    printf '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\%s]\n"Start"=dword:0000000%s\n\n' \
      "$S" "$D" >> "$HV/yaz.reg"
  done <<EOF2
$LST
EOF2

  hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SYSTEM' "$HV/SYSTEM" "$HV/yaz.reg"

  #
  # GERI OKU. Sinirlardaki uc servisi dogruluyoruz: A-1 acik, A kapali,
  # B kapali, B+1 acik olmali. Yanlis araligi "denendi" sanmak, ikili
  # aramanin tamamini gecersiz kilar.
  #
  N=0; HATA=0
  while IFS= read -r S; do
    [ -n "$S" ] || continue
    N=$((N+1))
    case "$N" in
      "$A"|"$B") BEK=4 ;;
      *) [ "$N" -gt "$A" ] && [ "$N" -lt "$B" ] && BEK=4 || BEK=0 ;;
    esac
    V="$(hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || echo '?')"
    [ "$V" = "$BEK" ] || { echo "  ! $S (#$N) -> $V, beklenen $BEK"; HATA=1; }
  done <<EOF3
$LST
EOF3
  [ "$HATA" = 0 ] || { echo "Dogrulama basarisiz, hive yazilmadi."; exit 1; }
  echo "  $KAPANAN servis kapatildi, kalani acik - hepsi dogrulandi"

  wimlib-imagex update "$WIM" 1 \
    --command="delete --force /Windows/System32/config/SYSTEM" >/dev/null 2>&1 || true
  wimlib-imagex update "$WIM" 1 \
    --command="add '$HV/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
  echo
  echo "Simdi: bash cache_esp.sh --win --system"
  echo "OKUMA: BUG=E -> suclu $A..$B arasinda    BUG=H -> disinda"
  exit 0
  ;;

liste)
  #
  # ACIK OLANLARI DA KAPALI OLANLARI DA GOSTER.
  #
  # Onceki surum yalnizca Start=0 olanlari listeliyordu ve bu bizi kor
  # birakti: "hepsi-kapat" ikinci kez calistirilinca "hedef:" bos cikti,
  # cunku hedefler zaten kapaliydi ve listede hic gorunmuyorlardi. Neyin
  # kapali oldugunu gormeden ikiye bolme yapilamaz.
  #
  say "Acilis surucileri ve simdiki durumlari"
  echo
  A=0; K=0
  while IFS= read -r S; do
    [ -n "$S" ] || continue
    ST="$(hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || true)"
    case "$ST" in
      0|0x0|0x00000000) D="ACIK  "; A=$((A+1)) ;;
      4|0x4|0x00000004) D="KAPALI"; K=$((K+1)) ;;
      *) continue ;;
    esac
    if echo " $KORUMALI " | grep -qi " $S "; then
      printf '  %-28s %s  korumali\n' "$S" "$D"
    else
      printf '  %-28s %s\n' "$S" "$D"
    fi
  done < <(servisler)
  echo
  echo "  acik: $A   kapali: $K"
  echo
  echo "Ikiye bolme: KAPALI olanlarin yarisini geri ac, sureyi olc."
  echo "  bash tools_winpe_surucu.sh ac <isim> <isim> ..."
  ;;

hepsi-kapat)
  #
  # ESKI TARAMA MANTIGI KALDIRILDI.
  #
  # Onceki surum hive'i tarayip "Start degeri 0 olan her sey" diyordu; bu hem
  # ikinci calistirmada bos donuyordu (hedefler zaten kapaliydi) hem de
  # Microsoft'un bilerek kapali biraktiklarini kapsamiyordu.
  #
  # Artik TEMEL_ACIK listesinden korumali olmayanlari cikarip dogrudan
  # sadece-kapat yoluna veriyoruz - ayni kod, ayni dogrulama, tek yerde.
  #
  HEPSI=""
  for S in $TEMEL_ACIK; do
    echo " $KORUMALI " | grep -qi " $S " && continue
    HEPSI="$HEPSI $S"
  done
  say "Korumali olmayan tum donanim surucileri kapatilacak"
  echo "  hedef:$HEPSI"
  exec bash "$0" sadece-kapat $HEPSI
  ;;

sadece-kapat|hepsi-ac)
  #
  # TEK SUCLU DENEMESI: hepsini ac, SADECE adi verilenleri kapat.
  #
  # NEDEN GEREKTI: "hepsini kapat" yontemi bir tuzaga dustu. O yontem
  # "hangi surucu cokertiyor"u degil, "hangisi olmadan Windows daha erken
  # pes ediyor"u buluyor - 15 saniyeyi GECEN hicbir yapilandirma cikmadi,
  # sadece daha kotusunu bulduk (15 -> 5 -> 6 saniye).
  #
  # Dogru yon tersi: tam calisan kumeden TEK BIR surucuyu cikarmak. O zaman
  # sure uzarsa suclu odur; degismezse masumdur ve temiz elenir.
  #
  # Elle yapmak otuz uc isim yazmak demekti; hata payi yuksek ve her
  # deneme icin tekrarlanmasi gerekiyordu.
  #
  #   bash tools_winpe_surucu.sh sadece-kapat DXGKrnl
  #   bash tools_winpe_surucu.sh hepsi-ac
  #
  HEDEFLER=" $* "
  if [ "$KOMUT" = "hepsi-ac" ]; then
    HEDEFLER=" "
    say "Korumali olmayan TUM acilis surucileri ACILIYOR (temel duruma donus)"
  else
    [ "$#" -gt 0 ] || { echo "Kapatilacak en az bir isim ver."; exit 1; }
    say "Hepsi aciliyor, sadece sunlar kapali kalacak: $*"
  fi

  #
  # SADECE TEMEL LISTEDEKILERE DOKUNUYORUZ. Hive'i tarayip "0 ya da 4 olan
  # her sey" demek, Microsoft'un bilerek kapattigi surucileri de aciyordu ve
  # ws2ifsl.sys imza hatasiyla acilisi tamamen durdurdu.
  #
  KAPANAN=""; ACILAN=0
  for S in $TEMEL_ACIK; do
    echo " $KORUMALI " | grep -qi " $S " && continue

    if echo "$HEDEFLER" | grep -qi " $S "; then
      D="dword:4"; KAPANAN="$KAPANAN $S"
    else
      D="dword:0"; ACILAN=$((ACILAN+1))
    fi

    STARTYAZ "$S" "${D##*:}" || echo "  ! $S yazilamadi"
  done

  echo "  acilan: $ACILAN    kapali kalan:${KAPANAN:- yok}"

  #
  # MICROSOFT'UN KAPALI BIRAKTIKLARINI ZORLA KAPALI TUT.
  #
  # Onceki tur bunlari acti ve cihaz "ws2ifsl.sys / 0xc0000428 / imza
  # dogrulanamadi" verip acilisi durdurdu. Bir daha o duruma dusmeyelim:
  # temel liste disinda kalsalar bile burada acikca 4'e cekiliyorlar.
  #
  for S in cdfs udfs ws2ifsl; do
    STARTYAZ "$S" 4 || true
  done
  echo "  Microsoft varsayilani korundu: cdfs udfs ws2ifsl kapali"

  say "Hive WIM'e geri konuyor"
  wimlib-imagex update "$WIM" 1 \
    --command="add '$HV/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
  wimlib-imagex optimize "$WIM" >/dev/null

  #
  # GERI OKUYUP DOGRULA - bu projede uc kez "arac yaptim dedi, cihaz
  # eskisini kullandi" yasandi.
  #
  DV="$W/dv"; mkdir -p "$DV"
  wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
    --dest-dir="$DV" --no-acls >/dev/null
  say "WIM'den geri okundu: KAPALI kalanlar"
  BULUNAN=0
  while IFS= read -r S; do
    [ -n "$S" ] || continue
    ST="$(hivexget "$DV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || true)"
    case "$ST" in
      4|0x4|0x00000004) printf '  %-28s Start=4\n' "$S"; BULUNAN=$((BULUNAN+1)) ;;
    esac
  done < <(servisler)
  [ "$BULUNAN" = 0 ] && echo "  (hicbiri - hepsi acik)"

  echo
  echo "Simdi: bash cache_esp.sh --win"
  ;;

kapat|ac|zorla-kapat)
  [ "$#" -gt 0 ] || { echo "En az bir servis adi ver."; exit 1; }
  if [ "$KOMUT" = "ac" ]; then DEGER="dword:0"; SOZ="ACILIS"; else DEGER="dword:4"; SOZ="DEVRE DISI"; fi

  #
  # "zorla-kapat" KORUMALI listesini YOK SAYAR.
  #
  # Koruma listesi "onsuz WinPE hic acilmaz" diye kurulmustu ve o donemde
  # dogruydu. Ama artik amac WinPE'yi ACMAK DEGIL - sifirlamanin durup
  # durmadigini olcmek. Kalp atisi kendi Start=0 surucumuzden geliyor ve
  # WinPE bozulsa da calismaya devam ediyor.
  #
  # Ilk hedef acpi.sys: on saniyelik pencerede platformla konusan tek
  # bilesen o, ve hep korumali listede oldugu icin hic denenmedi.
  #
  say "$SOZ yapiliyor: $*"
  for S in "$@"; do
    if [ "$KOMUT" = "kapat" ] && echo " $KORUMALI " | grep -qi " $S "; then
      echo "  ! $S KORUMALI, atlandi (zorlamak icin: zorla-kapat)"
      continue
    fi
    if STARTYAZ "$S" "${DEGER##*:}"
    then echo "  + $S"
    else echo "  ! $S yazilamadi (servis yok olabilir)"
    fi
  done

  say "Dogrulama (WIM'e koymadan once)"
  for S in "$@"; do
    ST="$(hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || echo '<yok>')"
    printf '  %-28s Start=%s\n' "$S" "$ST"
  done

  say "Hive WIM'e geri konuyor"
  wimlib-imagex update "$WIM" 1 \
    --command="add '$HV/SYSTEM' /Windows/System32/config/SYSTEM" >/dev/null
  wimlib-imagex optimize "$WIM" >/dev/null

  #
  # GERI OKUYUP DOGRULA. Bu projede uc kez "arac yaptim dedi, cihaz eskisini
  # kullandi" durumu yasandi; artik her yama geri okunuyor.
  #
  DV="$W/dv"; mkdir -p "$DV"
  wimlib-imagex extract "$WIM" 1 /Windows/System32/config/SYSTEM \
    --dest-dir="$DV" --no-acls >/dev/null
  say "WIM'den geri okundu"
  for S in "$@"; do
    ST="$(hivexget "$DV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || echo '<yok>')"
    printf '  %-28s Start=%s\n' "$S" "$ST"
  done

  echo
  echo "Simdi: bash cache_esp.sh --win"
  ;;

*)
  echo "Bilinmeyen komut: $KOMUT"
  echo "  liste | hepsi-ac | hepsi-kapat | sadece-kapat <isim>... | kapat <isim>... | ac <isim>..."
  exit 1
  ;;
esac
