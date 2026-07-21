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

# Install OpenShareCompanion as a per-user systemd service (runs at login).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT="$UNIT_DIR/openshare-companion.service"
LOG_DIR="$HOME/.local/share/openshare/logs"

CANDIDATES=(
  "/usr/local/bin/OpenShareCompanion"
  "$HOME/.local/bin/OpenShareCompanion"
  "$ROOT/build-release/OpenShareCompanion"
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
  echo "Could not find OpenShareCompanion. Build with:"
  echo "  cmake -S . -B build && cmake --build build"
  exit 1
fi

mkdir -p "$UNIT_DIR" "$LOG_DIR"

systemctl --user stop openshare-companion.service 2>/dev/null || true

cat > "$UNIT" <<EOF
[Unit]
Description=OpenShare companion (screen share worker)
After=graphical-session.target

[Service]
Type=simple
ExecStart=$BIN
WorkingDirectory=$ROOT
Environment=OPENSHARE_SERVICE=1
PassEnvironment=DISPLAY XAUTHORITY
Restart=on-failure
RestartSec=3
StandardOutput=append:$LOG_DIR/companion.out.log
StandardError=append:$LOG_DIR/companion.err.log

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable openshare-companion.service
systemctl --user import-environment DISPLAY XAUTHORITY
systemctl --user start openshare-companion.service

echo "Installed user service: $UNIT"
echo "Companion starts at login and restarts on failure."
echo "Logs: $LOG_DIR/"
echo
echo "Uninstall with: $ROOT/scripts/uninstall-companion-service.sh"
