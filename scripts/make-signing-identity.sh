#!/bin/bash
# 生成一个稳定的自签名代码签名证书（一次即可）。
# 目的：ad-hoc 签名的“指定要求”是 cdhash，每次重新编译都变，
# macOS 会把它当成新 App，于是桌面/文稿/下载的授权每次都要重新问。
# 换成固定证书后，指定要求变成 “identifier + certificate leaf”，跨重建不变。
set -euo pipefail
DIR="$HOME/Library/Application Support/win98-explorer/signing"
KC="$HOME/Library/Keychains/win98-signing.keychain-db"
PASS="win98dev"
NAME="Win98 Explorer Dev"

mkdir -p "$DIR"
if [ ! -f "$DIR/cert.pem" ]; then
  openssl req -x509 -newkey rsa:2048 -keyout "$DIR/key.pem" -out "$DIR/cert.pem" -days 3650 -nodes \
    -subj "/CN=$NAME" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" \
    -addext "basicConstraints=critical,CA:false" >/dev/null 2>&1
fi
openssl pkcs12 -export -out "$DIR/id.p12" -inkey "$DIR/key.pem" -in "$DIR/cert.pem" \
  -passout "pass:$PASS" -macalg sha1 -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES >/dev/null 2>&1

if [ ! -f "$KC" ]; then
  security create-keychain -p "$PASS" "$KC"
  security set-keychain-settings -lut 86400 "$KC"
fi
security unlock-keychain -p "$PASS" "$KC"
security import "$DIR/id.p12" -k "$KC" -P "$PASS" -T /usr/bin/codesign -T /usr/bin/security >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PASS" "$KC" >/dev/null

# 信任这张证书，否则 codesign 不认它是有效身份
if ! security find-identity -v -p codesigning | grep -q "$NAME"; then
  security add-trusted-cert -r trustRoot -k "$KC" "$DIR/cert.pem" >/dev/null 2>&1 || true
fi

# 把签名钥匙串挂进搜索列表（保留 login）
CUR=$(security list-keychains -d user | tr -d ' "')
if ! echo "$CUR" | grep -q "win98-signing"; then
  security list-keychains -d user -s "$HOME/Library/Keychains/login.keychain-db" "$KC"
fi

security find-identity -v -p codesigning | grep "$NAME" && echo "OK: 签名身份就绪"
