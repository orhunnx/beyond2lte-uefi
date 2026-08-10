#!/usr/bin/env bash
# S5E9820 ACPI tablolarini .dsl -> .aml derler. WSL'de calistir.
set -e
command -v iasl >/dev/null || { echo "iasl yok. Kur: sudo apt install -y acpica-tools"; exit 1; }
cd "$(dirname "$0")/SoCs/Samsung/S5E9820"
for f in *.dsl; do echo "==> $f"; iasl -ve "$f"; done
ls -l *.aml
echo
echo "Simdi bunlari Silicium-ACPI'ye kopyala ve ACPI.inc'teki blogu ac."
