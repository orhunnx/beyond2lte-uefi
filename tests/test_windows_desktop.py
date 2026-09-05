#!/usr/bin/env python3
"""Static gates for the full Windows Desktop UFS pipeline."""

from pathlib import Path
import unittest


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
            "B2L_OFFLINE_PACKAGE=TOUCHUFS",
            "B2L_BOOT_START=1",
            "B2L_FRAMEBUFFER_BOOT_START=1",
            'wimlib-imagex apply "$DESKTOP_WIM" 1 "$IMAGE"',
            "B2L_COMPACT=NONE",
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

    def test_desktop_bundle_tracks_exact_partition_target(self) -> None:
        bundle = read("tools_odin_winpe_bundle.sh")
        self.assertIn("--desktop) DISK_MODE=1; DESKTOP_MODE=1", bundle)
        self.assertIn('TARGET_ENTRY="$TARGET.img"', bundle)
        self.assertIn('grep -Fqx "B2L_TARGET=$TARGET"', bundle)
        self.assertIn("beyond2lte-windows-desktop-$TARGET.tar", bundle)
        self.assertIn("cache.img bilerek paketlenmedi", bundle)
        self.assertIn("CACHE flash'i reddedildi", bundle)
        self.assertIn("boot.img system.img userdata.img", bundle)
        self.assertIn("Bu arsiv USERDATA bolumundeki mevcut veriyi siler", bundle)


if __name__ == "__main__":
    unittest.main()
