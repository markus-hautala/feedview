#!/usr/bin/env bash
# Packages a Release build into the archive end users download.
#   scripts/package.sh <build-dir> <ndi-runtime-dir> <dist-dir>
# Linux -> <dist>/FeedView-linux-x86_64.tar.gz   (FeedView + lib/libndi.so.6)
# macOS -> <dist>/FeedView-macos-universal.zip    (FeedView.app with libndi.dylib inside)
set -euo pipefail

BUILD="${1:?build dir}"
RUNTIME="${2:?ndi runtime dir}"
DIST="${3:?dist dir}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$DIST"

case "$(uname -s)" in
  Linux)
    NAME="FeedView-linux-$(uname -m)"
    STAGE="$DIST/$NAME"
    rm -rf "$STAGE"
    mkdir -p "$STAGE/lib"
    cp "$BUILD/FeedView" "$STAGE/"
    [ -f "$BUILD/feedview-test-sender" ] && cp "$BUILD/feedview-test-sender" "$STAGE/"
    cp "$RUNTIME/libndi.so.6" "$STAGE/lib/"
    find "$RUNTIME" -maxdepth 1 -name 'NDI-*' -exec cp {} "$STAGE/" \;
    cp "$ROOT/packaging/README-dist.txt" "$STAGE/README.txt"
    cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/"
    cp "$ROOT/packaging/feedview.desktop" "$STAGE/"
    strip "$STAGE/FeedView" || true
    tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
    echo "$DIST/$NAME.tar.gz"
    ;;
  Darwin)
    APP="$BUILD/FeedView.app"
    [ -d "$APP" ] || { echo "$APP not found" >&2; exit 1; }
    mkdir -p "$APP/Contents/Frameworks"
    cp "$RUNTIME/libndi.dylib" "$APP/Contents/Frameworks/"
    # The test sender finds the runtime via ../Frameworks when it lives in Contents/MacOS.
    [ -f "$BUILD/feedview-test-sender" ] && cp "$BUILD/feedview-test-sender" "$APP/Contents/MacOS/"
    mkdir -p "$APP/Contents/Resources"
    find "$RUNTIME" -maxdepth 1 -name 'NDI-*' -exec cp {} "$APP/Contents/Resources/" \;
    cp "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/Contents/Resources/"
    # Ad-hoc signature: required for Apple Silicon to run the app at all. Not notarized.
    codesign --force --deep --sign - "$APP"
    codesign --verify --deep --strict "$APP"
    lipo -info "$APP/Contents/MacOS/FeedView"
    NAME="FeedView-macos-universal"
    rm -f "$DIST/$NAME.zip"
    ditto -c -k --keepParent "$APP" "$DIST/$NAME.zip"
    echo "$DIST/$NAME.zip"
    ;;
  *)
    echo "Windows packaging is done in the workflow (PowerShell)" >&2
    exit 1
    ;;
esac
