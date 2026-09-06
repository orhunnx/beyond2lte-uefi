#!/usr/bin/env python3
"""Disable the firmware BGRT handoff so Windows draws its own boot logo."""

from pathlib import Path
import argparse


BGRT_INF = (
    "  INF MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/"
    "BootGraphicsResourceTableDxe.inf"
)
DISABLED_INF = "  # B2L_WINDOWS_BOOT_LOGO: " + BGRT_INF.strip()


def patch_mu_tree(root: Path) -> Path:
    fdf = root / "Silicon/Silicium/SiliciumPkg/Extra.fdf.inc"
    if not fdf.is_file():
        raise SystemExit(f"HATA: Mu-Silicium BGRT FDF bulunamadi: {fdf}")

    text = fdf.read_text(encoding="utf-8")
    active_count = sum(line.rstrip("\r") == BGRT_INF for line in text.splitlines())
    disabled_count = sum(line.rstrip("\r") == DISABLED_INF for line in text.splitlines())
    if active_count == 1 and disabled_count == 0:
        text = text.replace(BGRT_INF, DISABLED_INF, 1)
        fdf.write_text(text, encoding="utf-8")
    elif active_count == 0 and disabled_count == 1:
        pass
    else:
        raise SystemExit(
            "HATA: Mu-Silicium BGRT satiri beklenmeyen durumda "
            f"(aktif={active_count}, kapali={disabled_count}): {fdf}"
        )

    verified = fdf.read_text(encoding="utf-8").splitlines()
    if BGRT_INF in verified or verified.count(DISABLED_INF) != 1:
        raise SystemExit("HATA: BGRT firmware volume kapisi dogrulanamadi")
    return fdf


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mu_root", type=Path)
    args = parser.parse_args()
    patched = patch_mu_tree(args.mu_root.resolve())
    print(f"  BGRT kapali: {patched}")


if __name__ == "__main__":
    main()
