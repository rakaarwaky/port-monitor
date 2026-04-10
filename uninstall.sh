#!/bin/bash
# Uninstall script for Port Monitor

set -e

INSTALL_DIR="/usr/local/bin"
ICON_DIR="/usr/local/share/icons/hicolor/256x256/apps"
DESKTOP_DIR="/usr/local/share/applications"

echo "=== Port Monitor - Uninstaller ==="
echo ""

if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (use sudo)"
    exit 1
fi

# Remove binary
if [ -f "$INSTALL_DIR/PortMonitor" ]; then
    echo "Removing $INSTALL_DIR/PortMonitor"
    rm "$INSTALL_DIR/PortMonitor"
fi

# Remove icon
if [ -f "$ICON_DIR/port-monitor.png" ]; then
    echo "Removing $ICON_DIR/port-monitor.png"
    rm "$ICON_DIR/port-monitor.png"
fi

# Remove desktop entry
if [ -f "$DESKTOP_DIR/port-monitor.desktop" ]; then
    echo "Removing $DESKTOP_DIR/port-monitor.desktop"
    rm "$DESKTOP_DIR/port-monitor.desktop"
fi

# Update caches
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
gtk-update-icon-cache -f "$ICON_DIR/../.." 2>/dev/null || true

echo ""
echo "=== Uninstallation Complete ==="
