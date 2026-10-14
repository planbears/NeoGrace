#!/bin/bash
# Проверка совместимости с настоящим Grace: каждый пример пересохраняется
# через NeoGrace и рисуется gracebat; картинки сравниваются с исходником.
#   tests/grace_compat.sh путь/к/NeoGrace   (нужны gracebat и python3 + Pillow)
set -e
NG="${1:-build/NeoGrace}"
DATA="$(cd "$(dirname "$0")/data" && pwd)"
OUT="$(mktemp -d)"
for f in "$DATA"/*.agr; do
  b=$(basename "$f" .agr)
  gracebat -hdevice PNG -printfile "$OUT/$b.orig.png" "$f" >/dev/null 2>&1
  "$NG" --export "$OUT/$b.ng.agr" "$f" >/dev/null 2>&1
  gracebat -hdevice PNG -printfile "$OUT/$b.ng.png" "$OUT/$b.ng.agr" >/dev/null 2>&1
done
python3 - "$OUT" <<'PY'
import sys, glob
from PIL import Image, ImageChops
out = sys.argv[1]; same = 0; files = sorted(glob.glob(out + '/*.orig.png'))
for o in files:
    b = o[:-9]
    a = Image.open(o).convert('RGB'); n = Image.open(b + '.ng.png').convert('RGB')
    d = ImageChops.difference(a, n).convert('L').point(lambda v: 255 if v > 40 else 0)
    cnt = d.histogram()[255] if a.size == n.size else -1
    if cnt == 0: same += 1
    else: print(b.split('/')[-1], 'различий:', cnt)
print(f'совпадают попиксельно: {same} из {len(files)}')
PY
