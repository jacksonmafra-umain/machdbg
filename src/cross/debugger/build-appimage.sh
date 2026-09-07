#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CROSS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$CROSS_DIR/../.." && pwd)"
BUILD_DIR="$CROSS_DIR/build-linux"

# Build
cmake -G Ninja -B "$BUILD_DIR" -S "$CROSS_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target debugger

# Create AppDir structure inside build directory
APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Copy the debugger binary
cp "$BUILD_DIR/debugger" "$APPDIR/usr/bin/"

# Copy the icon
cp "$REPO_ROOT/src/bug_black_outline.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/x64dbg.png"

# Create .desktop file
cat > "$APPDIR/usr/share/applications/x64dbg.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=x64dbg
Exec=debugger
Icon=x64dbg
Categories=Development;Debugger;
Comment=Open-source debugger for Linux
EOF

# Build the AppImage
export QMAKE=/usr/bin/qmake
export OUTPUT="$BUILD_DIR/x64dbg.AppImage"
linuxdeploy --appdir "$APPDIR" \
    --plugin qt \
    --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/x64dbg.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/x64dbg.png" \
    --executable "$APPDIR/usr/bin/debugger"

echo ""
echo "Built: $OUTPUT"
