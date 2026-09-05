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
                Return (Zero)
            }
        }

        Device (CPU2)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x02)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (Zero)
            }
        }

        Device (CPU3)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x03)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (Zero)
            }
        }

        Device (CPU4)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x04)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (Zero)
            }
        }

        Device (CPU5)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x05)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (Zero)
            }
        }

        Device (CPU6)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x06)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (Zero)
            }
        }

        Device (CPU7)
        {
            Name (_HID, "ACPI0007" /* Processor Device */)  // _HID: Hardware ID
            Name (_UID, 0x07)  // _UID: Unique ID
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (Zero)
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
        // Onceki _CLS varsayimi YANLISTI. WinPE'deki PnP raporu bunun
        // sonucunu dogrudan olctu: _CLS = { 01, 09, 01 } su kimlikleri
        // uretiyor:
        //
        //   ACPI\CLS_0001&SUBCLS_0009&PI_01
        //
        // Bu, storufs.inf'teki ACPI\CC_010901 ile eslesmiyor; bu nedenle
        // Code 28, denetleyici MMIO'suna ulasilmadan once goruldu.
        //
        // ACPI spec, PCI sinif bilgisini _CID ile PCI\CC_ccsspp biciminde
        // vermeyi tanimliyor. Etkin tanim bu standart uyumlu kimligi ilan
        // eder; vendor _HID korunur.
        //
        //
        // ==================================================================
        // UFS0 YENIDEN ETKIN - WinPE'de disk numaralandirma turu
        // ==================================================================
        //
        // Blokaj merdiveni tetikleyiciyi su pencereye sikistirdi: acilis
        // surucileri bittikten SONRA, Start=1 surucileri yuklenmeden ONCE.
        // Orada olan tek sey PnP'nin ACPI ad alanini sayip aygitlari
        // baslatmasi.
        //
        // _CLS'i kapatmak yetmedi: surucu baglanmasa da PnP o dugum icin bir
        // aygit olusturuyor ve _CRS'teki dort MMIO araligini ve 281 numarali
        // kesmeyi tahsis ediyor. Ustelik UEFI, UFS denetleyicisini CALISIR
        // durumda birakiyor - kesmesi acik bir denetleyici, ISR'i olmayan bir
        // kesme hattina baglanmis olabilir.
        //
        // Aygiti tamamen cikarmak bu ihtimallerin hepsini birden eliyor.
        // Ad alaninda geriye yalnizca CPU0 kaliyor, yani PnP'nin sayacagi
        // neredeyse hicbir sey yok.
        //
        //   Seritler 10'u geciyorsa -> suclu UFS0 dugumu. Sirada hangi parcasi
        //                              (kesme mi, MMIO tahsisi mi) daraltilir.
        //   Yine ~10 serit          -> ad alani masum; tetikleyici PnP'nin
        //                              KOK aygitlarinda ya da Faz 1'in ACPI
        //                              disi islerinde.
        //
        // Bu temel olcum artik tamamlandi: WinPE kabugu kararlı, DiskPart
        // "no fixed disks" diyor. Bu, UFS donaniminin Windows'a ilan
        // edilmedigini kanitlar; sonraki tek degisken aygiti geri eklemektir.
        //
        // Eski yorumlu blok tarihsel not olarak asagida kalir. Etkin tanim
        // onun onundedir; yalniz standart UFSHCI penceresini verir.
        //
        Device (UFS0)
        {
            // Vendor HID gecerlidir ve korunur. WinPE olcumu, _CLS'in tek
            // basina storufs.inf ile eslesmedigini kanitladi.
            Name (_HID, "SMSN9820")
            Name (_UID, Zero)
            // Bu testte UFSHCI'yi yalniz bizim StorPort miniportumuz alir.
            // PCI\\CC_010901 kaldirilirsa inbox storufs.sys ayni HBA'ya
            // baglanamaz; vendor _HID ise sabit kalir ve tani komutlari
            // ACPI\\SMSN9820\\0 yolunu kullanmaya devam eder.
            //
            // BYND9820, bu deney icin ozel compatible kimliktir.
            // b2lufs.inf yalniz buna eslesir; fiziksel _HID degismez.
            Name (_CID, "BYND9820")

            // Stok DTB'deki ufs-io-coherency nedeniyle cihaz koherent.
            Name (_CCA, One)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
            Name (_CRS, ResourceTemplate ()
            {
                // Bu alti pencere yalniz b2lufs'a tahsis edilir. Miniport
                // tabana gore eksiksizligini dogrular; ilk bagli turda
                // HCI/PHY/UniPro yazmaclarina hicbir yazma yapmaz.
                Memory32Fixed (ReadWrite, 0x13D60000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x13D61100, 0x00000200)
                Memory32Fixed (ReadWrite, 0x13D50000, 0x00008000)
                Memory32Fixed (ReadWrite, 0x13D64000, 0x00000800)
                Memory32Fixed (ReadWrite, 0x13C10700, 0x00000004)
                Memory32Fixed (ReadWrite, 0x15860724, 0x00000004)
                // DTB SPI 249 -> GIC GSIV 249 + 32 = 281.
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    0x00000119
                }
            })

            // Windows on ARM referans platformlarinda sabit dahili medya,
            // host denetleyicinin ACPI child'i olarak ve adres 8 ile tanitilir:
            // Lumia 950 SDC1.EMMC ile Surface Duo UFS0.DEV0 ayni deseni
            // kullanir. Miniportun STOR_DEVICE_CAPABILITIES_EX.Address alani
            // da 8 donerek bus PDO'sunu bu ACPI companion dugumune esler.
            Device (DEV0)
            {
                Method (_ADR, 0, NotSerialized)
                {
                    Return (0x08)
                }

                Method (_RMV, 0, NotSerialized)
                {
                    Return (Zero)
                }
            }
        }

        // Samsung sec_ts touchscreen on HSI2C10.  The controller does not
        // implement HID-over-I2C, so ACPI exposes a vendor function device
        // and b2ltouch.sys converts sec_ts events through Windows VHF.
        //
        // Safety boundary: these are the two device-MMIO pages verified by
        // the on-device UEFI test.  No DRAM/reserved-memory range, PMIC,
        // GPIO bank, other HSI2C controller or interrupt is claimed here.
        Device (TSP0)
        {
            Name (_HID, "BYN0001")
            Name (_UID, Zero)
            Name (_CCA, Zero)

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x104B0000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x10421000, 0x00001000)
            })
        }

        // MAX77705 Type-C/charger MFD on stock HSI2C45.  Samsung Linux
        // creates logical TOP, USBC and charger clients at 0x66/0x25/0x69
        // on this single controller.  b2lmax77705.sys follows that ownership
        // model. HSI2C45 transfer completion uses its original SPI 82.
        // MAX GPA1-6 uses a separate level-low EINT -> SPI 19 / GSIV 51.
        Device (USBC)
        {
            Name (_HID, "BYN0004")
            Name (_UID, Zero)
            Name (_CCA, Zero)

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                // CMGP3 rate/QCH registers and GPM12/GPM13 pinctrl are part
                // of the stock hsi2c_45 platform contract.  Expose them to
                // the same BYN0004 owner; no second Windows driver may map
                // or mutate these ranges while MAX77705 is active.
                Memory32Fixed (ReadWrite, 0x15C00000, 0x00008000)
                Memory32Fixed (ReadWrite, 0x15D60000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x15C22000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x15C30000, 0x00001000)
                // hsi2c@15D60000: DT SPI 82 -> GIC GSIV 114 (0x72).
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    0x00000072
                }
                // ALIVE owns GPA1-6 pin mux and its wakeup EINT registers.
                // GPIO input is level-low; the EINT controller's GIC output
                // is level-high. The two interrupt objects stay in this order.
                Memory32Fixed (ReadWrite, 0x15850000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    0x00000033
                }
            })
        }

        // Exynos9820 DWC_usb31: UCM reports the real MAX77705 Type-C role.
        // URS owns the shared controller and enumerates exactly one role child.
        Device (USBP)
        {
            Name (_HID, "BYN0002")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Name (_DEP, Package (0x01) { \_SB.USBC })
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x10C00000, 0x00010000)
                Memory32Fixed (ReadWrite, 0x10B00000, 0x00000200)
                Memory32Fixed (ReadWrite, 0x1586072C, 0x00000004)
                // Original FSYS0A genpd and CCF USB consumer resources.
                // These stay on the parent; role children receive DWC3 only.
                Memory32Fixed (ReadWrite, 0x15862200, 0x00000008)
                Memory32Fixed (ReadWrite, 0x10A00000, 0x00008000)
                Memory32Fixed (ReadWrite, 0x13000120, 0x00000004)
                Memory32Fixed (ReadWrite, 0x13020104, 0x00000004)
                // No ID-pin interrupt: MAX77705 owns Type-C GSIV51.
            })
            Device (USB0)
            {
                Name (_ADR, Zero)
                Name (_CCA, Zero)
                Name (_CRS, ResourceTemplate ()
                {
                    // Original DWC3 SPI241 + 32, owned by the active child.
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 0x00000111 }
                })
            }
            Device (UFN0)
            {
                Name (_ADR, One)
                Name (_CCA, Zero)
                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                    { 0x00000111 }
                })
            }
        }

        // Device (UFS0)
        // {
        // //
        // // Satici kimligi. Gecerli ACPI bicimi (4 harf + 4 hane).
        // // Eslemeyi bu degil, asagidaki _CLS sagliyor.
        // //
        // Name (_HID, "SMSN9820")
        // Name (_UID, Zero)
        // //
        // // Windows'un storufs.sys'e baglanmasini saglayan sey.
        // // Package { BaseClass, SubClass, ProgrammingInterface }
        // //   0x01 Mass Storage / 0x09 UFS / 0x01 UFSHCI  ->  ACPI\CC_010901
        // //
        // //
        // // _CLS GECICI OLARAK KAPALI - storufs BAGLANMASIN.
        // //
        // // Neden simdi: acilisi DriverEntry'de sonsuza kadar durdurunca
        // // sifirlama HIC OLMADI (25+ serit ve devam ediyordu). Yani sayac
        // // mutlak degil; sifirlama Windows'un bizden SONRAKI ilerlemesine
        // // bagli. UEFI/TrustZone/PMIC eksenleri kapandi.
        // //
        // // Bizden sonra ne oluyor? Kalan acilis surucileri, sonra PnP
        // // ACPI aygitlarini sayip suruculerini basliyor. Ve DSDT'de tek
        // // aygit var: UFS0.
        // //
        // // Eski bir olcum de tam buraya isaret ediyordu: _CLS acikken
        // // 5-6 saniye, kapaliyken 12 saniye. O zaman guvenilir bir
        // // olcum aletimiz yoktu - simdi var.
        // //
        // // _CLS olmadan Windows bu dugume hicbir surucu baglamiyor;
        // // aygit gorunur ama olu kaliyor.
        // //
        // // Name (_CLS, Package (0x03)
        // // {
        // //     0x01,
        // //     0x09,
        // //     0x01
        // // })
        // //
        // // _CCA ARM64'te DMA yapan her aygit icin zorunlu.
        // //
        // // BIR (KOHERENT) IDI, SIFIRA CEKILDI. Gerekce:
        // //
        // // DTB'de "ufs-io-coherency" node'u var ve UEFI (BoardInit.c)
        // // SYSREG'deki koherans bitlerini set ediyor. Ama koherans Exynos'ta
        // // IKI PARCALI: SYSREG biti yolun bir ucunu aciyor, digeri surucunun
        // // UTRD tanimlayicilarindaki paylasim/onbellek alanlarini dogru
        // // isaretlemesi. Samsung'un kendi surucusu ikisini de yapiyor;
        // // Windows'un JENERIK storufs.sys'i ikincisini bilmiyor.
        // //
        // // _CCA=1 derken Windows'a "onbellek bakimi yapma, tanimlayicilari
        // // dogrudan yaz" diyoruz. Koherans gercekte tam degilse tanimlayicilar
        // // CPU onbelleginde kalir, denetleyici bayat veri okur, hicbir komut
        // // tamamlanmaz ve storufs HwInitialize'da pes eder.
        // //
        // // _CCA=0 daha guvenli tarafta duruyor: Windows DMA tamponlarini
        // // onbelleklenmemis ayirir ve gereken temizleme/gecersiz kilma
        // // islemlerini kendisi yapar. Koherans gercekten calisiyorsa
        // // sadece biraz yavaslariz; calismiyorsa ise yarar.
        // //
        // // OLCUM: cekirdek disk acilisinda ~2 saniyede oluyor. Bu sure
        // // StorPort komut zaman asimi icin (30-60 s) cok kisa, ama
        // // "aygit baslatma basarisiz" icin tam yerinde. Yani storufs
        // // ILK ADIMDA dusuyor - NOP OUT / denetleyici init. Tanimlayici
        // // gorunurlugu tam o adimda belirleyici.
        // //
        // // GERI ALINDI: _CCA=0 hicbir seyi degistirmedi (yine 1-2 saniye)
        // // ve temel cizgiyi geri kurarken tek UEFI degisikligi buydu.
        // // Ise yaramayan bir degisikligi tasimak, sonraki her olcumu
        // // kirletir.
        // Name (_CCA, One)
        // Method (_STA, 0, NotSerialized)
        // {
        // Return (0x0F)
        // }
        // Name (_CRS, ResourceTemplate ()
        // {
        // Memory32Fixed (ReadWrite, 0x13D60000, 0x00000200)  // HCI
        // Memory32Fixed (ReadWrite, 0x13D61100, 0x00000200)  // VS_HCI
        // Memory32Fixed (ReadWrite, 0x13D50000, 0x00008000)  // UNIPRO
        // Memory32Fixed (ReadWrite, 0x13D64000, 0x00000800)  // PHY PMA
        // Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
        // {
        // 0x00000119
        // }
        // })
        // }
        //
    }
}
