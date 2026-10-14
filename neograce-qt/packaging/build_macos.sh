#!/bin/bash
# Сборка NeoGrace.app и NeoGrace.dmg для Mac на Apple Silicon (arm64).
#
#   ./packaging/build_macos.sh
#
# Нужны Xcode Command Line Tools (xcode-select --install) и Homebrew (https://brew.sh).
# Qt 6 и CMake скрипт поставит сам.  Результат: dist/NeoGrace.app и dist/NeoGrace.dmg
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Darwin" ]; then
  echo "Этот скрипт — для macOS. На Linux: cmake -B build && cmake --build build"; exit 1
fi
if [ "$(uname -m)" != "arm64" ]; then
  echo "Внимание: терминал работает под Rosetta (x86_64). Откройте Терминал без Rosetta,"
  echo "иначе получится программа для Intel."; exit 1
fi
if ! command -v brew >/dev/null; then
  echo "Нужен Homebrew: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""; exit 1
fi
for pkg in qt cmake; do
  brew list "$pkg" >/dev/null 2>&1 || brew install "$pkg"
done
QT_PREFIX="$(brew --prefix qt)"
JOBS="$(sysctl -n hw.ncpu)"

rm -rf build-mac dist
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-mac -j "$JOBS"

echo "== Тесты"
(cd build-mac && QT_QPA_PLATFORM=offscreen ctest --output-on-failure) || { echo "Тесты не прошли — сборка остановлена"; exit 1; }

mkdir -p dist
cp -R build-mac/NeoGrace.app dist/
# Русские подписи стандартных кнопок Qt
mkdir -p dist/NeoGrace.app/Contents/Resources/translations
cp "$QT_PREFIX"/share/qt/translations/qtbase_ru.qm dist/NeoGrace.app/Contents/Resources/translations/ 2>/dev/null || true
"$QT_PREFIX/bin/macdeployqt" dist/NeoGrace.app -always-overwrite

# Подпись «для себя» (ad-hoc).  Для раздачи другим нужен Developer ID:
#   NEOGRACE_SIGN_ID="Developer ID Application: ..." ./packaging/build_macos.sh
codesign --force --deep --sign "${NEOGRACE_SIGN_ID:--}" dist/NeoGrace.app

echo "== Архитектура:"
lipo -archs dist/NeoGrace.app/Contents/MacOS/NeoGrace

STAGE="$(mktemp -d)"
cp -R dist/NeoGrace.app "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname NeoGrace -srcfolder "$STAGE" -ov -format UDZO dist/NeoGrace.dmg >/dev/null
rm -rf "$STAGE"
echo "Готово: dist/NeoGrace.app и dist/NeoGrace.dmg"
echo "Установка: откройте dmg и перетащите NeoGrace в «Программы»."
