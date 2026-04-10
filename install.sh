#!/bin/bash
# Native installation script for Fedora/Linux
# Installs Port Monitor to system locations

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

INSTALL_DIR="/usr/local/bin"
ICON_DIR="/usr/local/share/icons/hicolor/256x256/apps"
DESKTOP_DIR="/usr/local/share/applications"
BINARY="build/PortMonitor"
ICON="resources/icon.png"
DESKTOP_FILE="port-monitor.desktop"

echo "=== Port Monitor - Native Installer ==="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (use sudo)"
    exit 1
fi

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "Building Port Monitor..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
fi

# Create directories
mkdir -p "$INSTALL_DIR"
mkdir -p "$ICON_DIR"
mkdir -p "$DESKTOP_DIR"

# Install binary
echo "Installing binary to $INSTALL_DIR/PortMonitor"
cp "$BINARY" "$INSTALL_DIR/PortMonitor"
chmod +x "$INSTALL_DIR/PortMonitor"

# Install icon
echo "Installing icon to $ICON_DIR/port-monitor.png"
cp "$ICON" "$ICON_DIR/port-monitor.png"

# Install desktop entry
echo "Installing desktop entry to $DESKTOP_DIR"
cp "$DESKTOP_FILE" "$DESKTOP_DIR/port-monitor.desktop"
chmod 644 "$DESKTOP_DIR/port-monitor.desktop"

# Update desktop database
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
gtk-update-icon-cache -f "$ICON_DIR/../.." 2>/dev/null || true

echo ""
echo "=== Installation Complete ==="
echo ""
echo "You can now:"
echo "  1. Launch from application menu: Search 'Port Monitor'"
echo "  2. Launch from terminal: PortMonitor"
echo "  3. Uninstall with: sudo ./uninstall.sh"
echo ""
