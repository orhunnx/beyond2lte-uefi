  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  #
  # B2LEl2Dxe DERLEMEDEN CIKARILDI.  Kaynagi duruyor, sadece yuklenmiyor.
  #
  # NE YAPIYORDU: Samsung'un TrustZone arka kapisina smc #0 / x0=0xC2000400
  # gonderip "su kodu EL2'de calistir" diyordu. Amac HCR_EL2.TSC ile EL1'den
  # gelen SMC'leri tuzaklayip Windows'un EL3'e ne sordugunu gormekti.
  #
  # NEDEN CIKTI: kendi "B2L2" imzasini hicbir zaman uretmedi, biz de
  # "calismiyor, zarari yok" diye birakmistik. BU VARSAYIM YANLISTI.
  # TrustZone'a EL2'de kod calistirmasini soylemek asla no-op degil:
  #
  #   - Yuk kabul edildiyse EL2'de YARIM CALISAN bir hipervizor oturuyor
  #   - HCR_EL2.TSC acik demek EL1'den gelen HER SMC bizim vektorumuze
  #     dusuyor demek. Windows PSCI icin SMC yapiyor.
  #   - Vektorumuz beklenmedik bir durumda donerse ya da hic donmezse
  #     sistem oracikta kilitlenir
  #
  # OLCUM BUNU DESTEKLIYOR: son denemede cihaz SIFIRLAMADI, DONDU - komple
  # kapatmadan yeniden baslatilamadi. Sonsuza kadar donen bir istisna
  # tuzaginin imzasi tam olarak budur. Ve sonme animasyonunu gordugumuzden
  # beri UEFI'ye eklenen tek bilesen buydu.
  #
  # Bellek haritasindaki "B2L EL2" bolgesi YERINDE BIRAKILDI: kod
  # yuklenmedigi icin calismiyor, ama ayrilmis RAM olarak duruyor ve
  # Windows surucusunun olum sonrasi halka tamponu icin zaten oraya
  # yazilacak (bkz. WINDOWS_SURUCU_PLANI.md, Faz 1b).
  #
  #
  # Calisma zamani servisi kancasi. GetTime ve GetVariable sarmalanip
  # giris/cikis sayaci 0xCF040000'a yaziliyor. Gerekce B2LRtHookDxe.c
  # basinda: memmap "MMIO: 0 Pages" gosteriyor, yani hicbir aygit bolgemiz
  # RUNTIME isaretli degil ve SetVirtualAddressMap sonrasi MMIO'ya dokunan
  # bir calisma zamani surucusu bosluga yaziyor.
  #
  INF beyond2ltePkg/Drivers/B2LRtHookDxe/B2LRtHookDxe.inf

  #
  # Samsung EL3 monitorunun Smart Exception Handler kancasi.
  #
  # EL3 beklenmedik bir istisna alinca cekirdegin kaydettigi adrese atliyor
  # (SMC_CMD_SET_SEH_ADDRESS = -123). Kayitli degilse atlayacak yeri yok ve
  # sifirliyor - sessizce. Windows bu SMC'yi asla yapmaz; biz yapiyoruz.
  #
  # Gerekce ve delil zinciri: EL3_SEH_PLANI.md
  #
  INF beyond2ltePkg/Drivers/B2LSehDxe/B2LSehDxe.inf

  #
  # ExitBootServices aninda GIC'teki butun SPI'lari susturuyor.
  #
  # Cihazda olculdu: UEFI kesmeleri hic acmadigi halde sekiz SPI bekliyordu
  # ve dordu ekran hattindan (decon/dsim/dpp) geliyordu. S-Boot o bloklari
  # acilis logosu icin calisir birakiyor, UEFI susturmuyor, Windows'un ise
  # surucusu yok. Gerekce B2LGicSusDxe.c basinda.
  #
  INF beyond2ltePkg/Drivers/B2LGicSusDxe/B2LGicSusDxe.inf

  INF SamsungPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF SamsungPkg/Drivers/GpioDxe/GpioDxe.inf
  INF SamsungPkg/Drivers/UsiDxe/UsiDxe.inf
  INF SamsungPkg/Drivers/SpeedyDxe/SpeedyDxe.inf
  INF SamsungPkg/Drivers/HsI2cDxe/HsI2cDxe.inf
  INF SamsungPkg/Drivers/PmicDxe/PmicDxe.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  INF SamsungPkg/Drivers/UfsDxe/UfsDxe.inf

  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf

  INF ShellPkg/Application/Shell/Shell.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 0
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif
  INF SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
  INF SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf

  INF SamsungPkg/Drivers/RngDxe/RngDxe.inf

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
