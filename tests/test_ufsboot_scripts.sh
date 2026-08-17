#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREPARE="$ROOT/tools_ufsboot_prepare.sh"
REG="$ROOT/windows/b2lufs/b2lufs-boot.reg"

for script in \
  "$ROOT/tools_ufsboot.sh" \
  "$ROOT/tools_winboot.sh" \
  "$ROOT/tools_b2lufs.sh" \
  "$ROOT/tools_ufsboot_prepare.sh" \
  "$ROOT/tools_windisk.sh" \
  "$ROOT/cache_esp.sh"; do
  bash -n "$script"
done

template_sets="$(grep -oE 'ControlSet[0-9]{3}' "$REG" | sort -u)"
[[ "$template_sets" == "ControlSet001" ]] || {
  echo "HATA: registry dosyasi tek ControlSet001 sablonu degil." >&2
  exit 1
}

for control_set in ControlSet001 ControlSet002 ControlSet007; do
  rendered="$(sed "s/ControlSet001/$control_set/g" "$REG")"
  rendered_sets="$(grep -oE 'ControlSet[0-9]{3}' <<<"$rendered" | sort -u)"
  [[ "$rendered_sets" == "$control_set" ]] || {
    echo "HATA: registry sablonu $control_set icin dogru uretilmedi." >&2
    exit 1
  }
done

list_function="$(awk '
  /^list_control_sets\(\) \{/ { in_function=1 }
  in_function { print }
  in_function && /^}/ { exit }
' "$PREPARE")"
eval "$list_function"
hivexsh() {
  printf '%s\n' Select ControlSet007 MountedDevices ControlSet001
}
discovered="$(list_control_sets unused-hive)"
[[ "$discovered" == $'ControlSet001\nControlSet007' ]] || {
  echo "HATA: hive control-set kesfi beklenen sonucu vermedi." >&2
  exit 1
}

grep -Fq "printf 'ls\\n' | hivexsh" "$PREPARE"
grep -Fq "add %s\\ncommit\\n" "$PREPARE"
if grep -Fq 'for cs in ControlSet001 ControlSet002' "$PREPARE"; then
  echo "HATA: sabit control-set varsayimi geri gelmis." >&2
  exit 1
fi

echo "UFS boot script regresyon testleri BASARILI."
