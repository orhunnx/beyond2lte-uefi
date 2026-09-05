#!/usr/bin/env python3
"""Static safety gates for the B2LUFS boot-start and disk-boot pipeline."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class B2LUfsBootPipelineTests(unittest.TestCase):
    def test_inf_is_boot_critical_scsi_miniport(self) -> None:
        inf = read("windows/b2lufs/b2lufs.inf.in")
        service = inf.split("[B2LUfs_Service]", 1)[1].split("[Strings]", 1)[0]
        self.assertRegex(service, r"(?m)^ServiceType=1$")
        self.assertRegex(service, r"(?m)^StartType=0$")
        self.assertRegex(service, r"(?m)^ErrorControl=3$")
        self.assertRegex(service, r'(?m)^LoadOrderGroup="SCSI Miniport"$')
        self.assertRegex(service, r"(?m)^ServiceBinary=%12%\\b2lufs\.sys$")

    def test_offline_stage_validates_service_hive_and_exact_sys(self) -> None:
        stage = read("windows/stage-winpe-driverstore.ps1")
        self.assertIn(
            'ValidateSet("Usb", "Touch", "UsbTouch", "Ufs", "TouchUfs")',
            stage,
        )
        self.assertIn('Add-WindowsDriver -Path $mountRoot -Driver $infPath', stage)
        self.assertIn('Test-B2LUfsOfflineBootService -MountPath $mountRoot', stage)
        for gate in (
            "$start -ne 0",
            "$errorControl -ne 3",
            '$group -cne "SCSI Miniport"',
            "B2LUFS boot SYS hash'i paket girdisinden farkli",
            'Get-B2LRequiredRegistryValue -Key $selectKey -Name "Default"',
            '$selectKey.GetValueNames() -contains "LastKnownGood"',
            '"B2L_BOOT_START=1"',
        ):
            self.assertIn(gate, stage)
        self.assertIn("Dismount-B2LRegistryHive", stage)
        self.assertIn("[Microsoft.Win32.RegistryKey]::OpenBaseKey", stage)
        self.assertNotIn("Get-ItemProperty -LiteralPath $servicePath", stage)

    def test_packager_runs_dism_ufs_stage_and_checks_marker(self) -> None:
        packager = read("tools_b2lufs.sh")
        self.assertRegex(packager, r'(?s)OFFLINE_PACKAGE="TouchUfs".*-DisableUsb')
        for marker in (
            "B2L_OFFLINE_PACKAGE=$EXPECTED_OFFLINE_PACKAGE",
            "B2L_BOOT_START=1",
            "B2L_LOAD_ORDER_GROUP=SCSI Miniport",
            "B2L_ERROR_CONTROL=3",
            "B2L_USB_TESTS=DISABLED",
        ):
            self.assertIn(marker, packager)

        build = read("yap.sh")
        self.assertIn("--ufs-boot) UFS_BOOT=1", build)
        self.assertIn('bash "$PROJ/tools_b2lufs.sh"', build)
        self.assertLess(
            build.index('"$PROJ/tools_b2lusb.sh"'),
            build.index('bash "$PROJ/tools_b2lufs.sh"'),
        )
        self.assertIn('if [ "$UFS_BOOT" = 1 ] && [ "$USB_TEST" = 1 ]', build)
        self.assertIn("USB_TEST=0", build)
        self.assertIn('B2L_STAGE_TOUCH_WITH_UFS="$WINDOWS_TOUCH_DEFERRED"', build)

        self.assertIn('$SelectedPackage -eq "TouchUfs"', read("windows/stage-winpe-driverstore.ps1"))
        self.assertIn("B2LUSB DriverStore paketleri", read("windows/stage-winpe-driverstore.ps1"))

    def test_winpe_never_runtime_installs_b2lufs(self) -> None:
        startnet = read("windows/startnet.cmd")
        match = re.search(
            r"(?ms)^:B2LUfsVerifyBootStart\n(?P<body>.*?)(?=^:B2LUfsMountCache\n)",
            startnet,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertNotIn("pnputil /add-driver", body.lower())
        self.assertIn('if /i not "%B2LUFSSTART%"=="0x0"', body)
        self.assertIn('if /i not "%B2LUFSGROUP%"=="SCSI Miniport"', body)
        self.assertIn('reg.exe query "HKLM\\SYSTEM\\CurrentControlSet\\Services\\b2lufs"', body)

    def test_disk_image_and_esp_reject_ram_boot_mix(self) -> None:
        windisk = read("tools_windisk.sh")
        cache = read("cache_esp.sh")
        winboot = read("tools_winboot.sh")
        bundle = read("tools_odin_winpe_bundle.sh")
        self.assertIn("/Windows/B2LUFS/offline-driverstore.txt", windisk)
        self.assertIn("B2L_BOOT_START=1", windisk)
        self.assertIn("B2L_USB_TESTS=DISABLED", windisk)
        self.assertIn("grep -Eq '^B2L_OFFLINE_PACKAGE=(UFS|TOUCHUFS)$'", windisk)
        self.assertNotIn("(UFS|TOUCHUFS)\\r?$", windisk)
        self.assertIn('DISKBOOT=1', cache)
        self.assertIn("ESP hala boot.wim tasiyor", cache)
        self.assertIn("BCD disk profili dogrulandi", winboot)
        self.assertIn("[IO.Path]::GetTempPath()", winboot)
        self.assertIn('BCD_LOCAL="$(mktemp "$WIN_TEMP_WSL/B2L-BCD.XXXXXX")"', winboot)
        self.assertIn('bcdedit.exe /store "$(wslpath -w "$BCD_LOCAL")"', winboot)
        self.assertNotIn('bcdedit.exe /store "$(wslpath -w "$OWNBCD")"', winboot)
        self.assertIn("--disk SYSTEM imaji NTFS degil", bundle)
        self.assertIn("--disk CACHE imajinda EFI/Microsoft/Boot/BCD yok", bundle)
        self.assertIn("RAM disk imaji reddedildi", bundle)


if __name__ == "__main__":
    unittest.main()
