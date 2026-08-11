/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20230628 (32-bit version)
 * Copyright (c) 2000 - 2023 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of DSDT_minimal.aml
 *
 * Original Table Header:
 *     Signature        "DSDT"
 *     Length           0x00000161 (353)
 *     Revision         0x02
 *     Checksum         0x6E
 *     OEM ID           "SAMSUN"
 *     OEM Table ID     "S5E9820 "
 *     OEM Revision     0x00000003 (3)
 *     Compiler ID      "INTL"
 *     Compiler Version 0x20230628 (539166248)
 */
DefinitionBlock ("", "DSDT", 2, "SAMSUN", "S5E9820 ", 0x00000006)
{
    Scope (_SB)
    {
        Device (CPU0)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, Zero)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU1)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, One)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU2)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x02)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU3)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x03)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU4)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x04)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU5)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x05)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU6)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x06)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        Device (CPU7)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x07)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }

        //
        // UFS denetleyicisi.
        //
        // Adresler ve kesme dogrudan stock DTB'nin ufs@0x13D60000 node'undan:
        //     reg = <0x13d60000 0x200    HCI (standart UFSHCI)
        //            0x13d61100 0x200    VS_HCI (Samsung vendor)
        //            0x13d50000 0x8000   UNIPRO
        //            0x13d70000 0x100>   UFSP (kullanmiyoruz)
        //     ufs-phy reg = <0x13d64000 0x800>
        //     interrupts = <0 0xf9 0>  ->  SPI 249  ->  GSIV 249+32 = 281 = 0x119
        //
        // BAGLANMA NASIL OLUYOR (storufs.inf'ten cikarildi, windows/storufs.inf):
        //
        //   [Generic.NTarm64]
        //   %ACPI\CC_010901.DeviceDesc%=UfsInstall, ACPI\CC_010901
        //
        // Yani Windows'un kutudan cikan UFS surucusunun JENERIK bir ARM64
        // girdisi var, Qualcomm'a ozel degil. "CC_010901" bir satici kimligi
        // degil, PCI sinif kodu: 01 yigin depolama / 09 UFS / 01 UFSHCI.
        //
        // Bu kimlik _HID'den gelmiyor - gelemez de, ACPI kimlikleri 4 harf +
        // 4 onaltilik hane olmak zorunda ve "CC_010901" dokuz karakter.
        // Windows bunu ACPI 6.0'la gelen _CLS nesnesinden turetiyor ve
        // "ACPI\CC_<sinif><altsinif><arayuz>" biciminde bir UYUMLULUK kimligi
        // uretiyor. O yuzden _HID satici kimligi olarak kaliyor, esleme _CLS
        // uzerinden oluyor.
        //
        Device (UFS0)
        {
            //
            // Satici kimligi. Gecerli ACPI bicimi (4 harf + 4 hane).
            // Eslemeyi bu degil, asagidaki _CLS sagliyor.
            //
            Name (_HID, "SMSN9820")
            Name (_UID, Zero)
            //
            // _CLS GECICI OLARAK KAPALI. SIRADAKI ADIM: GERI AC.
            //
            // ------------------------------------------------------------
            // SIRA
            // ------------------------------------------------------------
            // 1) ADK'nin ciplak WinPE'si, _CLS KAPALI  <- once bu olculecek
            // 2) ayni WinPE, _CLS ACIK                 <- sonra bu
            // Ikisini ayni turda degistirmiyoruz; hangisinin etki ettigini
            // bilemezdik. 1'in suresini not et, 2'de karsilastir.
            //
            // ------------------------------------------------------------
            // STORUFS NEDEN BOZUYOR VE GERCEK COZUM NE
            // ------------------------------------------------------------
            // storufs.sys UFSHCI spec'ine gore denetleyiciyi HCE ile
            // resetliyor ve UniPro linkini DME_LINKSTARTUP ile yeniden
            // kuruyor. HCE, PHY'yi (PMA, 0x13D64000) resetlemiyor - bozulan
            // sey UNIPRO LINKI. Exynos'ta link kalibrasyonu vendor'a ozel
            // DME nitelikleriyle yapiliyor; bizim UfsCal.c tam olarak bunu
            // yapiyor, jenerik surucu ise bilmiyor.
            //
            // GERCEK COZUM (ileri adim): UEFI, kontrolu birakmadan once
            // PHY'yi MUHAFAZAKAR VE STANDART bir moda cekerse - PWM Gear 1,
            // tek serit - jenerik DME_LINKSTARTUP calisir. Yavas olur ama
            // ilk acilis icin yavas fazlasiyla yeterli. Bu, jenerik
            // suruculeri vendor donaniminda calistirmanin bilinen yolu.
            //
            // Su anki test bunun on olcumu: hafif WinPE ile _CLS acikken
            // sure ne oluyor? 5-6 saniyede kaliyorsa link kopmasi
            // dogrulanir ve PWM-G1 isine girmeye deger.
            //
            // ESKI NOT (hala gecerli):
            //
            // Bu nesne Windows'un storufs.sys'i UFS0'a baglamasini sagliyor
            // (ACPI\CC_010901). Ama baglanan surucu UFSHCI spec'ine gore
            // denetleyiciyi RESETLIYOR (HCE) ve jenerik kodla link kurmaya
            // calisiyor - Exynos'un PHY kalibrasyonunu bilmeden.
            //
            // Cihazda gordugumuz tablo bununla birebir ortusuyor: WinPE
            // cekirdegi basliyor, 5-6 saniye calisiyor, sonra sessizce
            // sifirlaniyor. Surucu yukleme penceresi ve zaman asimi suresi.
            //
            // _CLS eklendiginde henuz WIM_NOT_BOOTABLE'da takiliydik, yani
            // WinPE hicbir zaman _CLS'siz calismadi. Bu test onu kapatiyor.
            //
            // SONUC NE OLURSA OLSUN ONEMLI:
            //   Windows daha ileri giderse -> supheli dogrulandi, storufs'un
            //     Exynos'ta calismasi icin PHY/CAL isi gerekiyor demektir
            //   Ayni kalirsa -> storufs elenir, baska yere bakariz
            //
            // Geri acmak icin asagidaki blogun yorumunu kaldir.
            //
            Name (_CLS, Package (0x03)
            {
                0x01,   // sinif    : yigin depolama
                0x09,   // alt sinif: UFS
                0x01    // arayuz   : UFSHCI
            })
            //
            // _CCA ARM64'te DMA yapan her aygit icin zorunlu. DTB'de
            // "ufs-io-coherency" node'u var ve UEFI (BoardInit.c) SYSREG'deki
            // koherans bitlerini set ediyor, yani aygit gercekten koherent.
            //
            Name (_CCA, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
            //
            // _CRS SADELESTIRILDI: TEK ARALIK.
            //
            // ONCEKI HALI dort aralik bildiriyordu:
            //     0x13D60000 / 0x200    HCI
            //     0x13D61100 / 0x200    VS_HCI  (Samsung vendor)
            //     0x13D50000 / 0x8000   UNIPRO
            //     0x13D64000 / 0x800    PHY PMA
            //
            // NEDEN SORUN: jenerik bir surucu _CRS kaynaklarini SIRAYLA
            // haritalar ve storufs.sys ilk bellek kaynaginin STANDART
            // UFSHCI register blogu olmasini bekler. Arkasindan gelen uc
            // aralik onun sozlugunde yok - en iyi ihtimalle yok sayilir,
            // en kotusunde kaynak eslemesini kaydirir. Ustelik ilk araligin
            // uzunlugu 0x200'du; standart register seti icin sinirda.
            // Qualcomm'un ACPI'si tek aralik bildiriyor.
            //
            // BU SURUM: tek aralik, 0x1000. VS_HCI 0x13D61100'de basliyor,
            // yani 0x1000 onu KAPSAMIYOR - kasitli. Vendor bloguna jenerik
            // surucunun isi yok; oraya UEFI tarafinda UfsDxe erisiyor ve
            // _CRS'ten etkilenmiyor.
            //
            // Onceki _CLS acik olcumu (5-6 saniye) DORT arali]kliydi. Bu
            // turda sure degisirse _CRS'in etkisi oldugunu bilecegiz.
            //
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x13D60000, 0x00001000)  // UFSHCI
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    0x00000119   // SPI 249 -> GSIV 281
                }
            })
        }
    }
}

