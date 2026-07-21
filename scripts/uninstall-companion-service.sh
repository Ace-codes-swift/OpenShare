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

# Remove the OpenShareCompanion user systemd service.
set -euo pipefail

UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT="$UNIT_DIR/openshare-companion.service"

systemctl --user stop openshare-companion.service 2>/dev/null || true
systemctl --user disable openshare-companion.service 2>/dev/null || true
rm -f "$UNIT"
systemctl --user daemon-reload

echo "Removed user service openshare-companion.service"
