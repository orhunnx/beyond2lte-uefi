#!/usr/bin/env python3
"""Static gates for the full Windows Desktop UFS pipeline."""

from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class WindowsDesktopPipelineTests(unittest.TestCase):
    def test_desktop_bcd_is_not_winpe(self) -> None:
        bcd = read("windows/bcd_olustur.cmd")
        self.assertIn('if /i "%VAR%"=="desktop" set "DESKTOPMOD=1"', bcd)
        self.assertIn("beyond2lte Windows Desktop", bcd)
        self.assertIn(
            'bcdedit /store "%BCD%" /set %G% path \\windows\\system32\\winload.efi',
            bcd,
        )
        self.assertIn("if not defined DESKTOPMOD", bcd)
        self.assertIn("tam Windows yukleyicisi, WinPE bayragi YOK", bcd)

    def test_winboot_rejects_winpe_bcd_for_desktop(self) -> None:
        winboot = read("tools_winboot.sh")
        self.assertIn("--desktop)    DISK_MODU=1; DESKTOP_MODU=1", winboot)
        self.assertIn("--desktop BCD hala winpe=Yes tasiyor", winboot)
        self.assertIn("BCD Desktop profili dogrulandi", winboot)

    def test_full_image_gets_storage_touch_and_framebuffer_offline(self) -> None:
        desktop = read("tools_windesktop.sh")
        for gate in (
            "-Package TouchUfs -DisableUsb -IncludeFramebuffer",
            "-PrepareDesktopOobe",
            "B2L_OFFLINE_PACKAGE=TOUCHUFS",
            "B2L_BOOT_START=1",
            "B2L_FRAMEBUFFER_BOOT_START=1",
            'wimlib-imagex apply "$DESKTOP_WIM" 1 "$IMAGE"',
            "B2L_COMPACT=NONE",
            "B2L_OOBE_CHILD_COMPLETION=3",
            "B2L_OOBE_TABTIP_QUARANTINE=1",
            "Total Bytes:",
            'ntfscat "$IMAGE" "$file"',
            "/Windows/explorer.exe",
            "ntfsls -a -p /Windows/System32",
            "SYSTEM bolumu tam 5700 MiB",
            "--target userdata",
            'if [ "$INDEX" = list ]',
        ):
            self.assertIn(gate, desktop)
        self.assertNotIn("--compact=XPRESS16K", desktop)
        verify_start = desktop.index('say "Uygulanan Desktop dosyalari dogrulaniyor"')
        verify_end = desktop.index('MARKER="$OUT/windows-desktop-target.txt"')
        self.assertNotIn('rm -f "$IMAGE"', desktop[verify_start:verify_end])

        stage = read("windows/stage-winpe-driverstore.ps1")
        self.assertIn("[switch]$IncludeFramebuffer", stage)
        self.assertIn("function Set-B2LFramebufferOfflineService", stage)
        self.assertIn('$service.SetValue("Start", 0', stage)
        self.assertIn('$service.SetValue("ErrorControl", 3', stage)
        self.assertIn('"B2L_FRAMEBUFFER_BOOT_START=1"', stage)
        self.assertIn("Dismount-B2LRegistryHive -HiveName $hiveName", stage)
        self.assertIn("function Set-B2LDesktopOobeState", stage)
        self.assertIn('$childCompletion.SetValue("setup.exe", 3', stage)
        self.assertIn('"B2L_CHILD_COMPLETION_SETUP=3"', stage)
        self.assertIn('"UnattendFile"', stage)
        self.assertIn('"B2L_OOBE_UNATTEND=1"', stage)
        self.assertIn('"TabletInputService", "TextInputManagementService"', stage)
        self.assertIn('$service.SetValue("Start", 4', stage)
        self.assertIn('"B2L_OOBE_TABTIP_QUARANTINE=1"', stage)
        self.assertIn('Join-Path $ProjectRoot "windows\\desktop-unattend.xml"', stage)

    def test_existing_desktop_image_has_transactional_oobe_repair(self) -> None:
        repair = read("tools_windesktop_oobe_repair.sh")
        for gate in (
            "ntfscat",
            "ntfscp -f",
            "hivexregedit --merge --prefix",
            "Setup\\Status\\ChildCompletion",
            '"setup.exe"=dword:00000003',
            "SYSTEM-before-oobe",
            "B2L_OOBE_CHILD_COMPLETION=3",
            "desktop-unattend.xml",
            "/Windows/System32/Sysprep/Unattend.xml",
            "B2L_OOBE_UNATTEND=1",
            "B2L_OOBE_LOCAL_USER=b2l",
            "B2L_OOBE_REBOOT_REQUIRED=0",
            "B2L_OOBE_TABTIP_QUARANTINE=1",
            "TabletInputService TextInputManagementService",
        ):
            self.assertIn(gate, repair)

        bundle = read("tools_odin_winpe_bundle.sh")
        self.assertIn("B2L_OOBE_CHILD_COMPLETION=3", bundle)
        self.assertIn("B2L_OOBE_TABTIP_QUARANTINE=1", bundle)
        self.assertIn("tools_windesktop_oobe_repair.sh", bundle)

    def test_keyboardless_oobe_answer_file_is_arm64_and_bounded(self) -> None:
        answer = ROOT / "windows/desktop-unattend.xml"
        tree = ET.parse(answer)
        ns = {"u": "urn:schemas-microsoft-com:unattend"}
        settings = tree.findall("u:settings", ns)
        self.assertEqual([node.attrib["pass"] for node in settings], ["oobeSystem"])
        components = settings[0].findall("u:component", ns)
        self.assertTrue(components)
        self.assertTrue(all(node.attrib["processorArchitecture"] == "arm64" for node in components))
        text = answer.read_text(encoding="utf-8")
        for gate in (
            "<HideOnlineAccountScreens>true</HideOnlineAccountScreens>",
            "<HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>",
            "<Name>b2l</Name>",
            "<Group>Administrators</Group>",
            "<LogonCount>1</LogonCount>",
            "B2L remove temporary OOBE credentials",
            "EnableDesktopModeAutoInvoke",
            'delete "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon" /v DefaultPassword',
            'del /f /q "C:\\Windows\\System32\\Sysprep\\Unattend.xml"',
        ):
            self.assertIn(gate, text)
        self.assertNotIn("SkipMachineOOBE", text)
        self.assertNotIn("SkipUserOOBE", text)
        self.assertNotIn("shutdown.exe", text.lower())
        self.assertNotIn("restart-computer", text.lower())
        self.assertNotIn("<restart>", text.lower())

    def test_windows_logo_patch_disables_only_bgrt_fv_entry(self) -> None:
        helper = ROOT / "tools_windows_boot_logo.py"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fdf = root / "Silicon/Silicium/SiliciumPkg/Extra.fdf.inc"
            fdf.parent.mkdir(parents=True)
            original = (
                "[FV.DXEFV]\n"
                "  INF MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/"
                "BootGraphicsResourceTableDxe.inf\n"
                "  INF MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf\n"
            )
            fdf.write_text(original, encoding="utf-8")
            subprocess.run(["python3", str(helper), str(root)], check=True, capture_output=True)
            subprocess.run(["python3", str(helper), str(root)], check=True, capture_output=True)
            patched = fdf.read_text(encoding="utf-8")
            self.assertIn("B2L_WINDOWS_BOOT_LOGO", patched)
            self.assertIn("SetupBrowserDxe.inf", patched)
            self.assertNotIn("\n  INF MdeModulePkg/Universal/Acpi/BootGraphics", patched)

        build = read("yap.sh")
        self.assertIn('python3 "$PROJ/tools_windows_boot_logo.py" "$MU"', build)

    def test_desktop_bundle_tracks_exact_partition_target(self) -> None:
        bundle = read("tools_odin_winpe_bundle.sh")
        self.assertIn("--desktop) DISK_MODE=1; DESKTOP_MODE=1", bundle)
        self.assertIn('TARGET_ENTRY="$TARGET.img"', bundle)
        self.assertIn('grep -Fqx "B2L_TARGET=$TARGET"', bundle)
        self.assertIn("beyond2lte-windows-desktop-$TARGET.tar", bundle)
        self.assertIn("cache.img bilerek paketlenmedi", bundle)
        self.assertIn("CACHE flash'i reddedildi", bundle)
        self.assertIn("boot.img system.img userdata.img.lz4", bundle)
        self.assertIn('ARCHIVE_TARGET_ENTRY="$TARGET_ENTRY.lz4"', bundle)
        self.assertIn("lz4 -12 -B6 --content-size", bundle)
        self.assertIn('HEADER" = "04224d186c60"', bundle)
        self.assertIn('require_samsung_lz4 "$CHECK/$ARCHIVE_TARGET_ENTRY"', bundle)
        self.assertIn("USTAR_MAX=8589934591", bundle)
        self.assertIn("LZ4_TAR_ROUNDTRIP_SHA", bundle)
        self.assertIn("Bu arsiv USERDATA bolumundeki mevcut veriyi siler", bundle)

    def test_keyboardless_transition_never_packages_cache(self) -> None:
        transition = read("tools_cache_desktop_transition.sh")
        startnet = read("windows/startnet-cache-desktop.cmd")
        self.assertIn("tar -H ustar -cf \"$TAR\" system.img", transition)
        self.assertIn('= system.img ]', transition)
        self.assertNotIn("cache.img", transition)
        self.assertIn("beyond2lte-winpe-ufs-disk.tar", transition)
        self.assertIn("tar -xOf \"$RECOVERY_ARCHIVE\" system.img", transition)
        self.assertIn("simg2img \"$RECOVERED_SPARSE\" \"$RECOVERED_RAW\"", transition)
        self.assertIn("system_is_ntfs \"$RECOVERED_RAW\"", transition)
        self.assertIn("B2LUFS offline boot-start isareti yok", transition)
        self.assertIn("select partition 28", startnet)
        self.assertIn('find.exe /i "CACHE"', startnet)
        self.assertIn('find.exe /i "UEFIESP"', startnet)
        self.assertIn('if not defined ESP_LABEL goto :label_fail', startnet)
        self.assertIn("BCD.winpe.bak", startnet)
        self.assertIn("/deletevalue {default} winpe", startnet)
        self.assertIn("/set {default} path \\Windows\\System32\\winload.efi", startnet)
        self.assertIn("B2L_CACHE_FLASHED=0", startnet)
        self.assertIn("wpeutil shutdown", startnet)


if __name__ == "__main__":
    unittest.main()
