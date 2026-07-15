#!/usr/bin/env bash
# Remove the OpenShareCompanion LaunchAgent.
set -euo pipefail

LABEL="com.atech.OpenShareCompanion"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl unload "$PLIST" 2>/dev/null || true
rm -f "$PLIST"

echo "Removed LaunchAgent $LABEL"
