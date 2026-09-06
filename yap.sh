#!/usr/bin/env bash
#
# beyond2lte UEFI — tek komutluk derleme.
#
# WSL'de calistir. Kendi konumunu bulur, hicbir yol elle yazilmaz:
#   bash /mnt/c/.../UEFI_beyond2lte/yap.sh
#
# Kullanim:
#   yap.sh                 DEBUG derle
#   yap.sh -r RELEASE      RELEASE derle
#   yap.sh --acpi          ACPI tablolarini derle + etkinlestir
#   yap.sh --touch         deneysel UEFI dokunmatik + zorunlu ACPI yenileme
#   yap.sh --touch-test    kareli UEFI dokunmatik testi + Windows'a devam
#   yap.sh --usb-test      USB tanisi (tek basina; --ufs-boot ile devre disi)
#   yap.sh --ufs-boot      B2LUFS ARM64 paketini WinPE'ye offline boot-start kur
#   yap.sh --psci-hvc      deneysel: Windows PSCI'sini EL2 HVC koprusune yonlendir
#   (Odin .tar ve .img her zaman uretilir)
#   yap.sh --clean         temiz derleme (-c)
#
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MU="${MU_SILICIUM:-$HOME/Mu-Silicium}"
REL="DEBUG"; CLEAN=""; DO_ACPI=0; DO_TAR=0; PSCI_HVC=0; TOUCH=0; TOUCH_TEST=0; USB_TEST=0; UFS_BOOT=0

while [ $# -gt 0 ]; do
  case "$1" in
    -r|--release) REL="$2"; shift 2 ;;
    --acpi)  DO_ACPI=1; shift ;;
    --touch) TOUCH=1; DO_ACPI=1; shift ;;
    --touch-test) TOUCH=1; TOUCH_TEST=1; DO_ACPI=1; shift ;;
    --usb-test) USB_TEST=1; DO_ACPI=1; shift ;;
    --ufs-boot) UFS_BOOT=1; DO_ACPI=1; shift ;;
    --psci-hvc) PSCI_HVC=1; DO_ACPI=1; shift ;;
    --tar)   shift ;;   # geriye donuk uyumluluk, artik varsayilan
    --clean) CLEAN="-c"; shift ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "Bilinmeyen secenek: $1"; exit 1 ;;
  esac
done

# Disk/UFS boot profili, sahada calismayan deneysel USB paketini boot zincirine
# sokmaz. Eski komut satirlari --usb-test icerse bile UFS turu guvenli profile
# indirgenir; bagimsiz USB gelistirmesi icin --usb-test tek basina kullanilabilir.
if [ "$UFS_BOOT" = 1 ] && [ "$USB_TEST" = 1 ]; then
  echo "UYARI: --ufs-boot profilinde --usb-test devre disi; B2LUSB paketlenmeyecek."
  USB_TEST=0
fi

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

[ -d "$MU" ] || { echo "Mu-Silicium bulunamadi: $MU"; echo "MU_SILICIUM=... ile yol verebilirsin."; exit 1; }

say "Proje: $PROJ"
say "Mu-Silicium: $MU"

# BootShim, configteki FD_SIZE kadar veriyi FD_BASE'e kopyalar. UEFI FD
# descriptoru veya CPU stack bu aralikla cakisirsa firmware kendi kodunu
# ezebilir; derlemeden (ve dolayisiyla flashtan) once bunu kesin denetle.
say "UEFI bellek yerlesimi dogrulaniyor"
python3 "$PROJ/tools_uefi_layout_check.py"
say "Windows korumali RAM denetleniyor"
python3 "$PROJ/tools_reserved_memory_audit.py" --check

# ---------------------------------------------------------------- ACPI
if [ "$DO_ACPI" = 1 ]; then
  say "ACPI tablolari derleniyor"
  command -v iasl >/dev/null || { echo "iasl yok -> sudo apt install -y acpica-tools"; exit 1; }
  # PPTT iasl ile degil Python ile uretiliyor (tools_pptt_gen.py'deki nedene bak).
  # iasl'den ONCE calismali ki SoCs kopyalanirken PPTT.aml yerinde olsun.
  python3 "$PROJ/tools_pptt_gen.py"
  cp -r "$PROJ/silicium-acpi-overlay/SoCs" "$MU/Silicium-ACPI/"
  ( cd "$MU/Silicium-ACPI/SoCs/Samsung/S5E9820" && for f in *.dsl; do iasl -ve "$f"; done )
  echo "ACPI tablolari derlendi."
fi

# ---------------------------------------------------------------- PSCI conduit
#
# Windows'a PSCI beyan eden FADT, hangi konduit kullanilacagini ARM Flags
# bit 1 ile secir. Varsayilan SMC'dir ve derlemeden sonra da zorla SMC'ye
# geri konur: bir onceki --psci-hvc deneyinin FACP.aml'i Mu-Silicium'da
# kalip sonraki "bash yap.sh" derlemesini sessizce degistiremez.
#
# --psci-hvc ayrica --acpi'yi zorunlu kilar; FACP.aml'in bu turda sifirdan
# derlendigi gorulmeden ikili tablo yamalanmaz.
#
FADT_AML="$MU/Silicium-ACPI/SoCs/Samsung/S5E9820/FACP.aml"
if [ "$PSCI_HVC" = 1 ]; then
  [ -f "$FADT_AML" ] || {
    echo "HATA: --psci-hvc icin FACP.aml derlenemedi: $FADT_AML"
    exit 1
  }
  say "PSCI HVC konduiti etkinlestiriliyor (deneysel)"
  python3 "$PROJ/tools_fadt_psci.py" "$FADT_AML" --hvc
elif [ -f "$FADT_AML" ]; then
  python3 "$PROJ/tools_fadt_psci.py" "$FADT_AML" --smc
fi

# ---------------------------------------------------------------- overlay
#
# ONCE UPSTREAM DOSYALARI GERI AL.
#
# tools_ufs_patch.py yamayi $MU icindeki UfsDxe.c'ye KALICI olarak
# uyguluyor. Scripti cagirmayi birakmak yamayi geri ALMIYOR - dosya orada
# yamali duruyor ve "B2L UFS:" satirlari cikmaya devam ediyor. Bilinen iyi
# noktaya donerken tam olarak bu oldu ve geri donusu eksik birakti.
#
# Overlay o dosyayi kopyalamadigi icin (sadece .inf var) tek yol upstream
# surumu git'ten geri cagirmak.
#
if [ -d "$MU/.git" ]; then
  if git -C "$MU" checkout -- Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.c 2>/dev/null; then
    echo "  UfsDxe.c upstream haline alindi (yama temizlendi)"
  fi
fi
#
# WatchdogTimer.c BURADA GERI CAGRILMIYOR.
#
# O dosya bir ALT MODULDE (Project Mu MdeModulePkg'i ayri depoda tutuyor),
# yani "git -C $MU checkout" ona ulasmiyor. Ilk surumde sabit yol yazmistim
# ve dosya hic bulunamadi.
#
# Bunun yerine tools_wdt_kapat.py dosyayi ARIYOR ve yamayi IDEMPOTENT
# uyguluyor: imza zaten varsa dokunmuyor. Birikmeli hasar olusmuyor,
# geri cagirmaya gerek kalmiyor.
#

say "Overlay kopyalaniyor"
cp -r "$PROJ/mu-silicium-overlay/." "$MU/"
python3 "$PROJ/tools_windows_boot_logo.py" "$MU"
echo "  Platforms/Samsung/beyond2ltePkg"
echo "  Silicon/Samsung/S5E9820Pkg"
echo "  Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.inf  (upstream yamasi)"
echo "  Resources/{Configs,DTBs}/beyond2lte.*"
echo "  Windows logosu: firmware BGRT aktarimi kapali"

# ---------------------------------------------------------------- UEFI touch
#
# Dokunmatik normal firmware yolunu degistirmez. --touch verilirse yalnizca
# Mu-Silicium'daki gecici overlay kopyasinda surucu FV'ye ve gercek HSI2C
# kutuphanesi bagina eklenir. Sonraki normal derleme overlay'i tekrar
# kopyalayarak iki satiri da kapatir. --touch ayrica ACPI'yi bu turda zorunlu
# olarak yeniden derler; onceki deneylerden kalan ACPI kullanilmaz.
#
if [ "$TOUCH" = 1 ] || [ "$USB_TEST" = 1 ]; then
  TOUCH_SOC_DSC="$MU/Silicon/Samsung/S5E9820Pkg/S5E9820Pkg.dsc.inc"
  sed -i 's@^  # HsI2cLib|S5E9820Pkg/Library/HsI2cLib/HsI2cLib.inf$@  HsI2cLib|S5E9820Pkg/Library/HsI2cLib/HsI2cLib.inf@' "$TOUCH_SOC_DSC"
fi

if [ "$TOUCH" = 1 ]; then
  TOUCH_DXE_INC="$MU/Platforms/Samsung/beyond2ltePkg/Include/DXE.inc"
  sed -i 's@^  # INF beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf$@  INF beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf@' "$TOUCH_DXE_INC"
  if grep -qx '  INF beyond2ltePkg/Drivers/B2LTouchDxe/B2LTouchDxe.inf' "$TOUCH_DXE_INC" \
     && grep -qx '  HsI2cLib|S5E9820Pkg/Library/HsI2cLib/HsI2cLib.inf' "$TOUCH_SOC_DSC"; then
    echo "  B2LTouchDxe: DENEYSEL PROFIL ACIK (ACPI yeniden derlendi)"
  else
    echo "HATA: B2LTouchDxe veya HsI2cLib etkinlestirilemedi"
    exit 1
  fi
else
  echo "  B2LTouchDxe: KAPALI"
fi

if [ "$USB_TEST" = 1 ]; then
  MAX77705_DXE_INC="$MU/Platforms/Samsung/beyond2ltePkg/Include/DXE.inc"
  MAX77705_DSC="$MU/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
  sed -i 's@^  # INF beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf$@  INF beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf@' "$MAX77705_DXE_INC"
  if grep -qx '  INF beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf' "$MAX77705_DXE_INC" \
     && grep -qx '  beyond2ltePkg/Drivers/B2LMax77705Dxe/B2LMax77705Dxe.inf' "$MAX77705_DSC" \
     && grep -qx '  HsI2cLib|S5E9820Pkg/Library/HsI2cLib/HsI2cLib.inf' "$TOUCH_SOC_DSC"; then
    echo "  B2LMax77705Dxe: DSC + FV + HSI2C45 HAZIRLIK ACIK"
  else
    echo "HATA: B2LMax77705Dxe DSC/FV veya HsI2cLib etkinlestirilemedi"
    exit 1
  fi
fi

if [ "$TOUCH_TEST" = 1 ]; then
  echo "  B2LTouchTest: EFI SHELL TEST UYGULAMASI HAZIRLANACAK"
fi

# ---------------------------------------------------------------- PSCI HVC bridge
#
# Varsayilan DXE.inc B2LEl2Dxe'yi yorumda birakir. HVC denemesinde bu tek
# satiri Mu-Silicium kopyasinda acariz; bir sonraki normal derlemede overlay
# yeniden kopyalandigi icin satir otomatik olarak tekrar kapali olur.
#
# Bu yol eski HCR_EL2.TSC SMC tuzagini kullanmaz. FADT HVC bitinin Windows'un
# PSCI cagrilarini HVC ile gondermesini sagladigi dar yolu kullanir.
#
if [ "$PSCI_HVC" = 1 ]; then
  DXE_INC="$MU/Platforms/Samsung/beyond2ltePkg/Include/DXE.inc"
  DSC="$MU/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc"
  sed -i 's|^  # INF beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf$|  INF beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf|' "$DXE_INC"
  sed -i 's|^  # beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf$|  beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf|' "$DSC"
  if grep -qx '  INF beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf' "$DXE_INC" \
     && grep -qx '  beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf' "$DSC"; then
    echo "  B2LEl2Dxe: HVC koprusu DERLEME VE FV'DE"
  else
    echo "HATA: B2LEl2Dxe DSC/DXE.inc icinde etkinlestirilemedi"
    exit 1
  fi
fi

#
# UfsDxe.c yamasi - YAZMA YOLU.
#
# UEFI'den UFS'e yazma calismiyor (iki bagimsiz kanit: EFI Shell yonlendirmesi
# ve bootmgr'in bootstat.dat'i). Okuma sorunsuz. Yama hem onbellek bakimini
# ekliyor hem de yazma yolunu enstrümante ediyor.
#
# Dosyanin tamamini overlay'e kopyalamiyoruz (~1000 satir, upstream
# duzeltmelerini kaybederdik); sadece dokundugumuz satirlar yamada.
#
#
# UFS YAMASI GECICI OLARAK KAPALI - BILINEN IYI NOKTAYA DONUS
#
# tools_ufs_patch.py, upstream UfsDxe.c'ye onbellek bakimi (WriteBack/
# Invalidate) ve "B2L UFS:" enstrumantasyonu ekliyor. Ikisi de UEFI
# TARAFINI degistiriyor - yani Windows devraldiginda UFS denetleyicisinin
# BULUNDUGU DURUMU degistirebilir.
#
# Bu yama, ekranda animasyonlu sonmeyi gordugumuz noktadan (43ef06c^) SONRA
# eklendi. Bilinen iyi noktaya donerken bunu birakmak, "her seyi geri aldik"
# demeyi yanlis kilardi.
#
# Geri acmak icin asagidaki satirin yorumunu kaldir. Enstrumantasyon
# ciktilarina (WriteBlocks/OCS satirlari) tekrar ihtiyac olursa gerekli.
#
# python3 "$PROJ/tools_ufs_patch.py" "$MU"
echo "  UFS enstrumantasyon yamasi ATLANDI"

#
# PWM GEAR 1 YAMASI - VARSAYILAN OLARAK KAPALI
#
#   B2L_UFS_PWM=1 bash yap.sh   -> yamayi uygula
#
# NEDEN KONULMUSTU: storufs.sys jenerik; UEFI linki HS'e cikarirsa
# devralamiyor. PWM G1 zaten UniPro'nun LINKSTARTUP sonrasi varsayilan
# durumu, yani vendor kalibrasyonu gerektirmiyor.
#
# NEDEN KAPATILDI: o kapi YAPISAL olarak kapandi (bkz. STATUS.md,
# "storufs: kapanan kapi"). HCE reseti Exynos'ta UniPro'nun yerel
# niteliklerini siliyor ve hicbir ACPI nesnesi jenerik bir surucuye Samsung
# PHY kalibrasyonu yaptiramaz. Artik RAM diskten aciliyoruz ve storufs'a
# hic ihtiyacimiz yok.
#
# BEDELI COK AGIRDI: PWM G1 tek serit ~3 MB/s. Her acilista bootmgfw 742
# MB'lik boot.wim'i o linkten RAM diske okuyor - dakikalar. Ustelik hicbir
# karsiligi olmadan. HS Gear 3'e donmek gelistirme dongusunu elli kat
# hizlandiriyor ve kaybettigimiz hicbir sey yok.
#
#
# WATCHDOG ETKISIZLESTIRME
#
# Gerekce tools_wdt_kapat.py basinda. Kisaca: bes bagimsiz olcum
# "bootmgfw'nin kurdugu bir UEFI watchdog'u suresi dolunca cihazi
# sifirliyor" aciklamasina oturuyor.
#
# Kapatmak icin:  B2L_WDT=0 bash yap.sh
#
if [ "${B2L_WDT:-1}" = "1" ]; then
  python3 "$PROJ/tools_wdt_kapat.py" "$MU"
else
  echo "  Watchdog yamasi ATLANDI (B2L_WDT=0)"
fi

if [ "${B2L_UFS_PWM:-0}" = "1" ]; then
  python3 "$PROJ/tools_ufs_pwm.py" "$MU"
else
  echo "  UFS PWM G1 yamasi ATLANDI (HS Gear 3) - hizlandirma icin"
  echo "    geri acmak istersen:  B2L_UFS_PWM=1 bash yap.sh"
fi

#
# ACPI.inc'i ETKINLESTIR - ama $MU icindeki kopyada, depoda degil.
#
# Onceden bu sed depodaki dosyaya uygulaniyordu. Sonuc: her --acpi derlemesi
# takip edilen iki dosyayi degistiriyor ve sonraki "git pull" su hatayla
# duruyordu:
#   error: Your local changes to the following files would be overwritten by merge
#
# $MU bir git deposu degil, orada degistirmek serbest. Depo temiz kaliyor.
#
if [ "$DO_ACPI" = 1 ]; then
  INC="$MU/Platforms/Samsung/beyond2ltePkg/Include/ACPI.inc"
  sed -i 's|^#\(FILE FREEFORM = 7E374E25\)|\1|; s|^#\(    SECTION \)|\1|; s|^#\(}\)|\1|' "$INC"
  echo "  ACPI.inc etkinlestirildi ($(grep -c '^    SECTION RAW' "$INC") tablo)"
fi

# ---------------------------------------------------------------- derle
say "Derleniyor ($REL)"
cd "$MU"
[ -d .venv ] && source .venv/bin/activate || true

# Eski ciktiyi sil ki "yeni uretildi mi" sorusu belirsiz kalmasin
rm -f "$MU"/Mu-beyond2lte*.img "$MU"/uefi-boot.tar

set +e
python3 build_uefi.py -d beyond2lte -r "$REL" $CLEAN
RC=$?
set -e
if [ $RC -ne 0 ]; then
  echo
  echo "DERLEME BASARISIZ (cikis kodu $RC). Cikti uretilmedi."
  echo "Ayrintili log: $MU/Build/BUILDLOG_beyond2ltePkg.txt"
  exit $RC
fi

# ---------------------------------------------------------------- rapor
# build_uefi.py model son ekiyle "Mu-beyond2lte-0.img" de uretebiliyor,
# o yuzden isme sabitlemiyoruz: en yeni eslesmeyi aliyoruz.
IMG="$(ls -t "$MU"/Mu-beyond2lte*.img 2>/dev/null | head -1)"
say "Sonuc"
if [ -n "$IMG" ] && [ -f "$IMG" ]; then
  echo "Uretilen imaj: $(basename "$IMG")"
  ls -lh "$IMG"
  RPT="$MU/Build/beyond2ltePkg/${REL}_CLANGPDB/BUILD_REPORT.TXT"
  if [ -f "$RPT" ]; then
    echo "--- FV kullanimi:"
    grep -A6 -i "FV Space Information" "$RPT" | head -12 || true
    echo "--- Shell FV'de mi:"
    if grep -qi '7C04A583' "$RPT"; then echo "  EVET (7C04A583 bulundu)"; else echo "  HAYIR"; fi
  fi
  # Odin tar'ini her zaman uret - flash icin gereken bu.
  ( cd "$MU" && cp -f "$IMG" boot.img \
      && tar -H ustar -cf uefi-boot.tar boot.img && rm -f boot.img )

  # Masaustunu bul. OneDrive yonlendirmesi ve Turkce klasor adi oncelikli
  # (ORTAM.md: C:\Users\orhun\OneDrive\Masaustu).
  DESK=""
  if [ -n "${OUTDIR:-}" ]; then
    DESK="$OUTDIR"
  else
    for p in /mnt/c/Users/*/OneDrive/Masa*st* \
             /mnt/c/Users/*/OneDrive/Desktop \
             /mnt/c/Users/*/Masa*st* \
             /mnt/c/Users/*/Desktop; do
      [ -d "$p" ] && { DESK="$p"; break; }
    done
  fi

  # Proje icindeki out/ klasorune de koy (Mac'e senkronlamak icin sabit yol)
  mkdir -p "$PROJ/out"
  cp -f "$IMG" "$MU/uefi-boot.tar" "$PROJ/out/" 2>/dev/null || true

  if [ -n "$DESK" ]; then
    mkdir -p "$DESK/beyond2lte-flash"
    cp -f "$IMG" "$DESK/beyond2lte-flash/"
    cp -f "$MU/uefi-boot.tar" "$DESK/beyond2lte-flash/"
    echo "Flash dosyalari: $DESK/beyond2lte-flash"
    echo "  uefi-boot.tar        -> Odin, AP slotu  (Re-Partition ISARETSIZ)"
    echo "  Mu-beyond2lte.img    -> heimdall flash --BOOT Mu-beyond2lte.img"
  else
    echo "Masaustu bulunamadi. OUTDIR=/mnt/c/... ile yol verebilirsin."
    echo "  $IMG"
    echo "  $MU/uefi-boot.tar"
  fi
else
  echo "Derleme gecti ama Mu-beyond2lte*.img bulunamadi."
  echo "Bulunanlar:"; ls -la "$MU"/*.img 2>/dev/null || echo "  hic .img yok"
  exit 1
fi

#
# EbsTest.efi'yi disari cikar. Firmware'e gomulu degil; testi yapmak
# isteyince esp/ icine elle kopyalanacak.
#
#
# B2LGoster.efi - buyuk puntolu tani ekrani. esp/ icine SABIT konuyor
# (EbsTest gibi opsiyonel degil): her olcumde ise yariyor ve firmware'e
# gomulu olmadigi icin acilis yolunu hic etkilemiyor.
#
GOS="$(find "$MU/Build" -name 'B2LGoster.efi' 2>/dev/null | head -1)"
if [ -n "$GOS" ]; then
  mkdir -p "$PROJ/out"
  cp -f "$GOS" "$PROJ/out/B2LGoster.efi"
  cp -f "$GOS" "$PROJ/esp/B2LGOSTR.EFI"
  echo "B2LGoster hazir: esp/B2LGOSTR.EFI"
fi

EBS="$(find "$MU/Build" -name 'EbsTest.efi' 2>/dev/null | head -1)"
if [ -n "$EBS" ]; then
  mkdir -p "$PROJ/out"
  cp -f "$EBS" "$PROJ/out/EbsTest.efi"
  echo
  echo "EbsTest hazir: out/EbsTest.efi"
  echo "  Denemek icin:  cp out/EbsTest.efi esp/EBSTEST.EFI && bash cache_esp.sh --win --system"
  echo "  Geri almak icin: rm esp/EBSTEST.EFI"
fi

# --touch, Windows ARM64 VHF paketini de ayni WIM'e offline stage eder.
# Boylece ACPI\BYN0001 ilk PnP olusumunda b2ltouch + vhf stack'i hazirdir;
# startnet icinde sonradan INF baglayip Code 14 uretmez.
WINDOWS_TOUCH_DEFERRED=0
if [ "$TOUCH" = 1 ]; then
  if [ "${B2L_SKIP_WINDOWS_TOUCH_BUILD:-0}" = "1" ]; then
    echo "B2LTOUCH Windows surucusu B2L_SKIP_WINDOWS_TOUCH_BUILD=1 ile atlandi"
  else
    [ -x "$PROJ/tools_b2ltouch.sh" ] || {
      echo "HATA: tools_b2ltouch.sh yok veya calistirilabilir degil"
      exit 1
    }
    if { [ "$USB_TEST" = 1 ] && [ "${B2L_SKIP_WINDOWS_USB_BUILD:-0}" != "1" ]; } || \
       { [ "$UFS_BOOT" = 1 ] && [ "${B2L_SKIP_WINDOWS_UFS_BUILD:-0}" != "1" ]; }; then
      B2L_DEFER_OFFLINE_STAGE=1 "$PROJ/tools_b2ltouch.sh"
      WINDOWS_TOUCH_DEFERRED=1
    else
      "$PROJ/tools_b2ltouch.sh"
    fi
  fi
fi

#
# B2LUsbTest.efi - USB bring-up. R11 eski salt-okuma sayfalarini korur; son
# ayri onayda stok HS PHY + event DMA + DWC3 RUN_STOP probu yapar. FV'ye gomulmez;
# her yeni MMIO penceresinden once kullanici onayi ister ve yalniz
# --usb-test secilen turda ESP'ye konur. Secenek verilmeyen sonraki build eski
# bir USB testini sessizce yeniden paketleyemez.
#
if [ "$USB_TEST" = 1 ]; then
  USB_APP="$(find "$MU/Build" -name 'B2LUsbTest.efi' 2>/dev/null | head -1)"
  [ -n "$USB_APP" ] && [ -f "$USB_APP" ] || {
    echo "HATA: B2LUsbTest.efi derlenmedi"
    exit 1
  }
  mkdir -p "$PROJ/out"
  cp -f "$USB_APP" "$PROJ/out/B2LUsbTest.efi"
  cp -f "$USB_APP" "$PROJ/esp/B2LUSB.EFI"
  echo "B2LUsbTest hazir: esp/B2LUSB.EFI"

  # Ayni --usb-test turu Windows ARM64 statik HOST paketini de boot.wim'e koyar.
  # Boylece sonraki --baseline ESP, UEFI tani uygulamasini atlayip tam olarak
  # bu turda derlenen Windows surucusunu tasir. Yalniz UEFI tanisi gereken
  # gelistirici turlari bu adimi acik bir ortam degiskeniyle atlayabilir.
  if [ "${B2L_SKIP_WINDOWS_USB_BUILD:-0}" = "1" ]; then
    echo "B2LUSB Windows surucusu B2L_SKIP_WINDOWS_USB_BUILD=1 ile atlandi"
  else
    [ -x "$PROJ/tools_b2lusb.sh" ] || {
      echo "HATA: tools_b2lusb.sh yok veya calistirilabilir degil"
      exit 1
    }
    B2L_USB_AUTO_HOST="${B2L_USB_AUTO_HOST:-1}" \
      B2L_STAGE_TOUCH_WITH_USB="$WINDOWS_TOUCH_DEFERRED" \
      "$PROJ/tools_b2lusb.sh"
  fi
else
  rm -f "$PROJ/esp/B2LUSB.EFI"
fi

# B2LUFS en son offline-stage edilir. Touch ve USB ayni WIM'e daha once
# kurulmus olabilir; UFS stage yalniz kendi INF paketini yeniler ve mevcut
# paketleri korur. Start=0/SCSI Miniport/SYS hash kapilari tools_b2lufs.sh
# icinde DISM commit edilmeden once denetlenir.
if [ "$UFS_BOOT" = 1 ]; then
  if [ "${B2L_SKIP_WINDOWS_UFS_BUILD:-0}" = "1" ]; then
    echo "B2LUFS Windows boot paketi B2L_SKIP_WINDOWS_UFS_BUILD=1 ile atlandi"
  else
    [ -s "$PROJ/tools_b2lufs.sh" ] || {
      echo "HATA: tools_b2lufs.sh yok veya bos"
      exit 1
    }
    B2L_STAGE_TOUCH_WITH_UFS="$WINDOWS_TOUCH_DEFERRED" bash "$PROJ/tools_b2lufs.sh"
  fi
fi

if [ "$TOUCH_TEST" = 1 ]; then
  TOUCH_APP="$(find "$MU/Build" -name 'B2LTouchTest.efi' 2>/dev/null | head -1)"
  [ -n "$TOUCH_APP" ] && [ -f "$TOUCH_APP" ] || {
    echo "HATA: B2LTouchTest.efi derlenmedi"
    exit 1
  }
  mkdir -p "$PROJ/out"
  cp -f "$TOUCH_APP" "$PROJ/out/B2LTouchTest.efi"
  cp -f "$TOUCH_APP" "$PROJ/esp/B2LTOUCH.EFI"
  echo "B2LTouchTest hazir: esp/B2LTOUCH.EFI"
else
  rm -f "$PROJ/esp/B2LTOUCH.EFI"
fi
