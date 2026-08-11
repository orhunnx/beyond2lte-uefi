##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = beyond2lte
  PLATFORM_GUID                  = 556BDA6E-EBC8-4D8F-8EA6-DAA2D7AD7A68
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/beyond2ltePkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = beyond2ltePkg/beyond2lte.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include S5E9820Pkg/S5E9820Pkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # BEYAN EDILEN RAM 3 GB'A INDIRILDI - TEK DEGISKENLI DENEY.
  #
  # EbsTest bir sinifi tamamen eledi: ExitBootServices'ten sonra hicbir sey
  # kurmayan bir dongu 30+ saniye yasiyor. Yani servis edilmeyi bekleyen bir
  # sey (watchdog, EL3 kilitlenme dedektoru, periyodik ping) yok. Sifirlamaya
  # yol acan sey Windows'un YAPMADIGI degil, YAPTIGI bir sey.
  #
  # EbsTest'in yapmayip Windows'un yaptigi seylerden biri: giderek daha fazla
  # RAM'e yazmak. Beyan edilen bellegin bir kismi gercekte erisilebilir
  # degilse ya da TZASC korumaliysa, ayirici oraya vardigi anda veri yolu
  # hatasi olur - Windows'a haber verilmeden, bugcheck uretilmeden, tam
  # gordugumuz imzayla. Zamanlamasi da oturuyor: ~10 saniye, ayiricinin
  # yukari dogru yayilmasi kadar.
  #
  # 3 GB secildi cunku daha asagisi baska bir sebeple bozardi: 23H2'nin
  # boot.wim'i acilmis halde 1650 MiB ve WinPE onu RAM diskine aliyor.
  # 1.5 GB'ta bellek yetmezliginden coker ve deneyi confound ederdi.
  #
  # Sonuc 10. saniyede degismezse bellek aklanir ve sira GIC/zamanlayici/PSCI
  # tarafina gelir.
  #
  gArmTokenSpaceGuid.PcdSystemMemorySize|0xC0000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xC9200000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Samsung"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Galaxy S10+"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"beyond2lte"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Galaxy_S10+_beyond2lte"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Galaxy S10+"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1440
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|3040
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Ekrandaki debug log'unun akis hizi (satir basina gecikme, mikrosaniye).
  # SiliciumPkg varsayilani 10000 (10 ms). Fotograflayabilmek icin yavaslatildi.
  # Fotograf cekmen bitince 10000'e geri al - yoksa boot cok uzun surer.
  #
  #
  # 0 = her DEBUG satiri icin bekleme yok. Varsayilan 100000 (100 ms/satir)
  # acilisi dakikalara cikariyor. Bu, geri alinan tek sey degil - bilerek
  # korunuyor cunku Windows'un gordugu hicbir seyi degistirmiyor.
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferDelay|0
  #
  # SiliciumPkg.dsc.inc DEBUG build'de 0x8007EE0F veriyor; bu maskede
  # DEBUG_INFO (0x40) YOK. Bizim tani satirlarimiz ve UfsDxe'nin
  # "UFS Well known lun[%d] %llu MB" kapasite satiri EFI_D_INFO seviyesinde,
  # yani ekrana hic dusmuyordu. INFO bitini ekliyoruz.
  #
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8007EE4F


  #
  # BDS'in otomatik olusturdugu "Internal UEFI Shell" boot secenegi bu GUID'i
  # ariyor (MsBootOptionsLib.c -> RegisterFvBootOption(PcdShellFile, ...)).
  # Varsayilani C57AD6B7-... ; biz FV'ye ShellPkg'in kendi GUID'iyle koydugumuz
  # icin PCD'yi ona esitliyoruz.
  #
  # 7C04A583-9E3E-4F1C-AD65-E05268D0B4D1 (gUefiShellFileGuid), little-endian bayt dizisi.
  # {GUID("...")} sozdizimi bu EDK2 surumunde sessizce yanlis sonuc verebiliyor;
  # PcBdsPkg.dec varsayilaniyla ayni formati kullaniyoruz.
  gPcBdsPkgTokenSpaceGuid.PcdShellFile|{ 0x83, 0xA5, 0x04, 0x7C, 0x3E, 0x9E, 0x1C, 0x4F, 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }


[LibraryClasses]
  #
  # UEFI Shell
  #
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
  HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
  BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf
  AcpiViewCommandLib|ShellPkg/Library/UefiShellAcpiViewCommandLib/UefiShellAcpiViewCommandLib.inf
  OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf

  #
  # Memory Libraries
  #
  MemoryMapLib|beyond2ltePkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Input Libraries
  #
  KeypadDeviceLib|beyond2ltePkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

[Components]
  #
  # UEFI Shell (firmware volume'a gomulu - USB ya da ESP gerektirmez)
  #
  ShellPkg/Application/Shell/Shell.inf {
    <LibraryClasses>
      NULL|ShellPkg/Library/UefiShellLevel1CommandsLib/UefiShellLevel1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel2CommandsLib/UefiShellLevel2CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel3CommandsLib/UefiShellLevel3CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDriver1CommandsLib/UefiShellDriver1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDebug1CommandsLib/UefiShellDebug1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellInstall1CommandsLib/UefiShellInstall1CommandsLib.inf
      # acpiview: ACPI tablolarini cihazda listeler ve spec'e karsi dogrular.
      # Windows denemeden once tablolarimizi test etmenin en dogrudan yolu.
      NULL|ShellPkg/Library/UefiShellAcpiViewCommandLib/UefiShellAcpiViewCommandLib.inf
      HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
      PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
      BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf
    <PcdsFixedAtBuild>
      gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE
      gEfiMdePkgTokenSpaceGuid.PcdUefiLibMaxPrintBufferSize|8000
  }

  #
  # EbsTest - ExitBootServices sonrasi kalp atisi.
  #
  # SADECE DERLENIYOR, FDF'ye EKLENMIYOR: firmware'e gomulmedigi icin
  # normal acilis yolunu hic etkilemiyor. Uretilen .efi out/ altina
  # kopyalaniyor; test etmek isteyince esp/ icine elle konuyor,
  # startup.nsh varsa calistiriyor, yoksa Windows'a geciyor.
  #
  beyond2ltePkg/Applications/EbsTest/EbsTest.inf

  #
  # B2LGoster - tani kayitlarini buyuk harflerle cizer. EbsTest gibi FDF'ye
  # girmiyor; out/ altina cikip esp/ icine elle konuyor.
  #
  beyond2ltePkg/Applications/B2LGoster/B2LGoster.inf
  beyond2ltePkg/Drivers/B2LRtHookDxe/B2LRtHookDxe.inf
  beyond2ltePkg/Drivers/B2LSehDxe/B2LSehDxe.inf

  #
  # EL2 yuku yukleyici (bkz. Drivers/B2LEl2Dxe/B2LEl2Payload.S)
  #
  # DERLEMEDEN CIKARILDI. Asil kaldirma DXE.inc'te (firmware imajina girmiyor);
  # burada da kapatiliyor ki "derleniyor ama yuklenmiyor" gibi kafa karistirici
  # bir ara durum kalmasin. Gerekce icin DXE.inc'teki uzun nota bak: sessiz
  # kalan bir SMC, TrustZone'un istegi reddettigi anlamina gelmiyor.
  #
  # beyond2ltePkg/Drivers/B2LEl2Dxe/B2LEl2Dxe.inf

  #
  # Input
  #
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
