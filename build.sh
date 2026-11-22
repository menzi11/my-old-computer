#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
APP="$ROOT/build/My Old Computer.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -O -swift-version 5 -target "$(uname -m)-apple-macos13.0" \
  -o "$APP/Contents/MacOS/MyOldComputer" \
  $(find "$ROOT/Sources" -name '*.swift' | sort)
# 图标（已提交到仓库；缺失时现场生成）
if [ ! -f "$ROOT/Resources/AppIcon.icns" ]; then
  swiftc -O "$ROOT/scripts/make_icon.swift" -o /tmp/moc_make_icon
  (cd "$ROOT" && /tmp/moc_make_icon)
fi
cp "$ROOT/Resources/Info.plist" "$APP/Contents/Info.plist"
cp "$ROOT/Resources/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"
# 用固定的自签名证书签名：TCC（桌面/文稿/下载）授权才能跨重建保留。
# 没有该身份时退回 ad-hoc（此时每次重建都会被系统当成新 App，重新询问）。
IDENTITY="${WIN98_SIGN_IDENTITY:-Win98 Explorer Dev}"
KC="$HOME/Library/Keychains/win98-signing.keychain-db"
if [ -f "$KC" ]; then security unlock-keychain -p win98dev "$KC" 2>/dev/null || true; fi
if security find-identity -v -p codesigning 2>/dev/null | grep -q "$IDENTITY"; then
  codesign --force --sign "$IDENTITY" "$APP" >/dev/null 2>&1 || true
else
  codesign --force --sign - "$APP" >/dev/null 2>&1 || true
fi
echo "built: $APP"
