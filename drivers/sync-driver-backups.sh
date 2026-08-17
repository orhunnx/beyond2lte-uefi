#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$ROOT/drivers/MIRROR_MANIFEST.tsv"
MODE="${1:-check}"

if [[ "$MODE" != "check" && "$MODE" != "update" ]]; then
  echo "Kullanim: $0 [check|update]" >&2
  exit 2
fi

failures=0
copied=0

while IFS=$'\t' read -r source destination; do
  [[ -z "${source:-}" || "$source" == \#* ]] && continue

  source_abs="$ROOT/$source"
  destination_abs="$ROOT/$destination"

  if [[ ! -d "$source_abs" ]]; then
    echo "HATA: canli kaynak bulunamadi: $source" >&2
    failures=$((failures + 1))
    continue
  fi

  if [[ "$destination" != drivers/* ]]; then
    echo "HATA: yedek hedefi drivers/ disinda: $destination" >&2
    failures=$((failures + 1))
    continue
  fi

  if [[ "$MODE" == "update" ]]; then
    mkdir -p "$destination_abs"
  fi

  while IFS= read -r tracked_file; do
    relative="${tracked_file#"$source"/}"
    backup_file="$destination_abs/$relative"

    if [[ "$MODE" == "update" ]]; then
      mkdir -p "$(dirname "$backup_file")"
      cp -p "$ROOT/$tracked_file" "$backup_file"
      copied=$((copied + 1))
    elif [[ ! -f "$backup_file" ]]; then
      echo "EKSIK: $backup_file" >&2
      failures=$((failures + 1))
    elif ! cmp -s "$ROOT/$tracked_file" "$backup_file"; then
      echo "FARKLI: $tracked_file -> $destination/$relative" >&2
      failures=$((failures + 1))
    fi
  done < <(git -C "$ROOT" ls-files "$source")
done < "$MANIFEST"

if [[ "$MODE" == "update" ]]; then
  echo "Driver yedekleri guncellendi: $copied dosya"
  "$0" check
elif (( failures == 0 )); then
  echo "Driver yedekleri canli kaynaklarla ayni."
else
  echo "Driver yedegi denetimi basarisiz: $failures sorun" >&2
  exit 1
fi
