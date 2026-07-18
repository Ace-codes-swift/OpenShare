#!/usr/bin/env bash

# OpenShare
# Copyright (C) 2026 Ace Jones / ATech
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Install OpenShareCompanion as a per-user LaunchAgent (runs at login, stays in background).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLIST_DIR="$HOME/Library/LaunchAgents"
PLIST="$PLIST_DIR/com.atech.OpenShareCompanion.plist"
LABEL="com.atech.OpenShareCompanion"
LOG_DIR="$HOME/Library/Logs/OpenShare"

# Prefer the installed .app, then a release build, then a plain dev build.
CANDIDATES=(
  "/Applications/OpenShareCompanion.app/Contents/MacOS/OpenShareCompanion"
  "$HOME/Applications/OpenShareCompanion.app/Contents/MacOS/OpenShareCompanion"
  "$ROOT/build-release/OpenShareCompanion.app/Contents/MacOS/OpenShareCompanion"
  "$ROOT/build/OpenShareCompanion.app/Contents/MacOS/OpenShareCompanion"
  "$ROOT/build/OpenShareCompanion"
)
BIN=""
for c in "${CANDIDATES[@]}"; do
  if [[ -x "$c" ]]; then
    BIN="$c"
    break
  fi
done

if [[ -z "$BIN" ]]; then
  echo "Could not find OpenShareCompanion. Install the app from the DMG, or build with:"
  echo "  ./scripts/package.sh   (or cmake --build build)"
  exit 1
fi

mkdir -p "$PLIST_DIR" "$LOG_DIR"

# Stop an existing agent if present
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl unload "$PLIST" 2>/dev/null || true

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$BIN</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$ROOT</string>
  <key>EnvironmentVariables</key>
  <dict>
    <!-- Suppress the interactive pairing dialog when running as a service -->
    <key>OPENSHARE_SERVICE</key>
    <string>1</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>$LOG_DIR/companion.out.log</string>
  <key>StandardErrorPath</key>
  <string>$LOG_DIR/companion.err.log</string>
  <key>ProcessType</key>
  <string>Interactive</string>
</dict>
</plist>
EOF

launchctl bootstrap "gui/$(id -u)" "$PLIST" 2>/dev/null || launchctl load "$PLIST"

echo "Installed LaunchAgent: $PLIST"
echo "Companion starts at login and restarts if it exits."
echo "Logs: $LOG_DIR/"
echo
echo "Grant Screen Recording + Accessibility to:"
echo "  $BIN"
echo "(or to Terminal if you first ran it from there)"
echo
echo "Unload with: $ROOT/scripts/uninstall-companion-service.sh"
