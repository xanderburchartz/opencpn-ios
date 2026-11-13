#!/usr/bin/env bash
# Bouwt externe afhankelijkheden van OpenCPN die de iOS-SDK niet levert, statisch.
# OpenSSL (TLS) en libcurl met OpenSSL; certificaten controleert curl met de
# vertrouwensopslag van iOS (USE_APPLE_SECTRUST), dus geen eigen CA-bundel.
#
# Gebruik: scripts/build-deps-ios.sh [sim|device]
# Uitvoer: build/<sim|device>/deps-install (include/, lib/), logs in build/logs/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-sim}"
case "$TARGET" in
  sim) SYSROOT=iphonesimulator; OSSL_TARGET=iossimulator-arm64-xcrun; OSSL_MIN=-mios-simulator-version-min=18.0 ;;
  device) SYSROOT=iphoneos; OSSL_TARGET=ios64-xcrun; OSSL_MIN=-mios-version-min=18.0 ;;
  *) echo "onbekend doel: $TARGET" >&2; exit 1 ;;
esac

INST="$ROOT/build/$TARGET/deps-install"
mkdir -p "$INST" "$ROOT/build/logs"

# --- OpenSSL -----------------------------------------------------------------
OSRC="$ROOT/upstream/openssl"
OBLD="$ROOT/build/$TARGET/openssl"
[ -f "$OSRC/Configure" ] || { echo "upstream/openssl ontbreekt (scripts/fetch-upstream.sh openssl)" >&2; exit 1; }
if [ ! -f "$INST/lib/libssl.a" ] || [ ! -f "$INST/lib/libcrypto.a" ]; then
  mkdir -p "$OBLD"
  (cd "$OBLD" && perl "$OSRC/Configure" "$OSSL_TARGET" no-shared no-tests no-apps no-docs \
      no-module no-engine "$OSSL_MIN" --prefix="$INST" --libdir=lib \
    && make -j"$(sysctl -n hw.ncpu)" build_libs && make install_dev)
fi

# --- libcurl -----------------------------------------------------------------
SRC="$ROOT/upstream/curl"
BLD="$ROOT/build/$TARGET/curl"
[ -f "$SRC/CMakeLists.txt" ] || { echo "upstream/curl ontbreekt (scripts/fetch-upstream.sh curl)" >&2; exit 1; }
cmake -S "$SRC" -B "$BLD" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$SYSROOT" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INST" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_STATIC_LIBS=ON \
  -DBUILD_CURL_EXE=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_LIBCURL_DOCS=OFF \
  -DBUILD_MISC_DOCS=OFF \
  -DENABLE_CURL_MANUAL=OFF \
  -DCURL_ENABLE_SSL=ON \
  -DCURL_USE_OPENSSL=ON \
  -DOPENSSL_ROOT_DIR="$INST" \
  -DOPENSSL_INCLUDE_DIR="$INST/include" \
  -DOPENSSL_SSL_LIBRARY="$INST/lib/libssl.a" \
  -DOPENSSL_CRYPTO_LIBRARY="$INST/lib/libcrypto.a" \
  -DOPENSSL_USE_STATIC_LIBS=ON \
  -DUSE_APPLE_SECTRUST=ON \
  -DCURL_CA_BUNDLE=none \
  -DCURL_CA_PATH=none \
  -DCURL_USE_LIBPSL=OFF \
  -DCURL_USE_LIBSSH2=OFF \
  -DUSE_LIBIDN2=OFF \
  -DUSE_NGHTTP2=OFF \
  -DCURL_BROTLI=OFF \
  -DCURL_ZSTD=OFF \
  -DCURL_ZLIB=ON
cmake --build "$BLD"
cmake --install "$BLD"
