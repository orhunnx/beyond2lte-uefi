#!/usr/bin/env bash
#
# Projeyi commit'leyip push'lar. Mac'te calistir.
#
#   bash kaydet.sh "mesaj"      belirtilen mesajla
#   bash kaydet.sh              tarih damgali varsayilan mesaj
#
# Ilk kullanimdan once GitHub uzak deposunu bagla:
#   gh repo create beyond2lte-uefi --private --source=. --push
#
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

MSG="${1:-durum: $(date '+%Y-%m-%d %H:%M')}"

# Sandbox'tan kalmis olabilecek kilit
rm -f .git/index.lock 2>/dev/null || true

[ -d .git ] || { git init -q; echo "git deposu olusturuldu"; }

git add -A

if git diff --cached --quiet; then
  echo "Degisiklik yok."
  exit 0
fi

echo "--- commit'lenecek:"
git diff --cached --name-status
echo

git commit -q -m "$MSG"
echo "commit: $MSG"

if git remote get-url origin >/dev/null 2>&1; then
  BR="$(git rev-parse --abbrev-ref HEAD)"
  git push -u origin "$BR"
  echo "push edildi -> origin/$BR"
else
  echo
  echo "Uzak depo bagli degil. Bir kez calistir:"
  echo "  gh repo create beyond2lte-uefi --private --source=. --push"
fi
