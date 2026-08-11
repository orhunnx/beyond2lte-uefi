#!/usr/bin/env bash
#
# WinPE'nin ACILIS SURUCULERINI listeler ve kapatir.
#
#   bash tools_winpe_surucu.sh liste            <- once bunu calistir
#   bash tools_winpe_surucu.sh kapat a b c ...  <- adi verilenleri devre disi birak
#   bash tools_winpe_surucu.sh ac    a b c ...  <- geri ac
#   bash tools_winpe_surucu.sh hepsi-kapat      <- korumali olmayan hepsi
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
KORUMALI="ACPI acpiex CNG Wdf01000 WudfPf FltMgr Null volmgr partmgr disk mountmgr msisadrv pdc vdrvroot spaceport volume volsnap WimFsf FBWF Wof Fs_Rec Mup FileInfo CLFS KSecDD KSecPkg pcw"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -s "$WIM" ] || { echo "esp/sources/boot.wim yok. Once tools_winboot.sh calistir."; exit 1; }
command -v wimlib-imagex >/dev/null || { echo "wimlib yok -> sudo apt install -y wimtools"; exit 1; }
command -v hivexsh      >/dev/null || { echo "hivexsh yok -> sudo apt install -y libhivex-bin"; exit 1; }

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

case "$KOMUT" in

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
  # KORUMALI OLMAYAN BUTUN ACILIS SURUCULERINI KAPAT.
  #
  # NEDEN TEK TEK DEGIL: gruplari ayri ayri denemek her biri icin bir Odin
  # flasi demek. Hepsini birden kapatmak tek hamlede ya butun kategoriyi
  # ELER ya da elimize hedef verir:
  #
  #   sure DEGISIRSE  -> suclu acilis suruculerinin arasinda, ikiye bolerek
  #                      birkac flasta tek isme inilir
  #   15 saniye AYNI  -> acilis surucileri MASUM. Cokme winload'un surucu
  #                      yukleme asamasinda degil, ntoskrnl'in kendi ic
  #                      isleyisinde. Butun kategori tek flasla kapanir.
  #
  # Korumali liste disarida: onlarsiz cekirdek hic baslamaz ve elimizde
  # "acilmadi"dan baska bilgi kalmaz. Bilgi uretmeyen deney yapmiyoruz.
  #
  say "Korumali olmayan TUM acilis surucileri kapatiliyor"
  LISTE=""
  while IFS= read -r S; do
    [ -n "$S" ] || continue
    ST="$(hivexget "$HV/SYSTEM" "ControlSet001\\Services\\$S" Start 2>/dev/null || true)"
    case "$ST" in 0|0x0|0x00000000) ;; *) continue ;; esac
    echo " $KORUMALI " | grep -qi " $S " && continue
    LISTE="$LISTE $S"
  done < <(servisler)
  echo "  hedef:$LISTE"
  echo
  exec bash "$0" kapat $LISTE
  ;;

kapat|ac)
  [ "$#" -gt 0 ] || { echo "En az bir servis adi ver."; exit 1; }
  if [ "$KOMUT" = "kapat" ]; then DEGER="dword:4"; SOZ="DEVRE DISI"; else DEGER="dword:0"; SOZ="ACILIS"; fi

  say "$SOZ yapiliyor: $*"
  for S in "$@"; do
    if echo " $KORUMALI " | grep -qi " $S " && [ "$KOMUT" = "kapat" ]; then
      echo "  ! $S KORUMALI, atlandi (onsuz cekirdek hic baslamaz)"
      continue
    fi
    if hivexsh -w "$HV/SYSTEM" >/dev/null 2>&1 <<HSH
cd \\ControlSet001\\Services\\$S
setval 1
Start
$DEGER
commit
HSH
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
  echo "  liste | hepsi-kapat | kapat <isim>... | ac <isim>..."
  exit 1
  ;;
esac
