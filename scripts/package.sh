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

# Build OpenShare and package the two apps into distributable DMGs:
#   dist/OpenShare-Viewer.dmg      -> OpenShare.app          (control your other Mac)
#   dist/OpenShare-Companion.dmg   -> OpenShareCompanion.app (the Mac being controlled)
#
# Both apps are self-contained (SDL is statically linked; only system
# frameworks are used), so they run on a Mac with nothing else installed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-release"
DIST_DIR="$ROOT/dist"

VIEWER_APP="$BUILD_DIR/OpenShare.app"
COMPANION_APP="$BUILD_DIR/OpenShareCompanion.app"

echo "==> Configuring (Release)"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null

echo "==> Building"
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu)"

if [[ ! -d "$VIEWER_APP" || ! -d "$COMPANION_APP" ]]; then
  echo "Build did not produce the expected .app bundles." >&2
  exit 1
fi

# Sign with a stable identity so TCC permissions (Screen Recording,
# Accessibility) and firewall/Keychain approvals survive rebuilds.
# Ad-hoc signatures change on every build, which makes macOS treat each
# build as a brand-new app and re-prompt for everything.
SIGN_IDENTITY="OpenShare Dev Signing"
if ! security find-identity -v -p codesigning 2>/dev/null | grep -q "$SIGN_IDENTITY"; then
  echo "==> No '$SIGN_IDENTITY' identity found, falling back to ad-hoc signing"
  echo "    (permissions will NOT persist across rebuilds; run"
  echo "     scripts/create-signing-identity.sh to fix this)"
  SIGN_IDENTITY="-"
fi
echo "==> Code signing with identity: $SIGN_IDENTITY"
codesign --force --deep --sign "$SIGN_IDENTITY" "$VIEWER_APP"
codesign --force --deep --sign "$SIGN_IDENTITY" "$COMPANION_APP"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

make_dmg() {
  local app="$1"        # path to .app
  local volname="$2"    # DMG volume / title
  local out="$3"        # output .dmg path
  local readme="$4"     # README text

  local stage
  stage="$(mktemp -d)"
  cp -R "$app" "$stage/"
  ln -s /Applications "$stage/Applications"
  printf '%s\n' "$readme" > "$stage/READ ME FIRST.txt"

  rm -f "$out"
  hdiutil create -quiet -volname "$volname" -srcfolder "$stage" \
    -ov -format UDZO "$out"
  rm -rf "$stage"
  echo "    created $out"
}

echo "==> Creating DMGs"

make_dmg "$COMPANION_APP" "OpenShare Companion" \
  "$DIST_DIR/OpenShare-Companion.dmg" \
"OpenShare Companion (the Mac you want to control)

1. Drag OpenShareCompanion into Applications.
2. Launch it. On first run it shows a manager code and this Mac's address.
3. Approve Screen Recording and Accessibility when macOS asks
   (System Settings > Privacy & Security).
4. Keep it running, or install it as a login service (see the project README).

Give the address and code to the OpenShare (Viewer) Mac."

make_dmg "$VIEWER_APP" "OpenShare Viewer" \
  "$DIST_DIR/OpenShare-Viewer.dmg" \
"OpenShare Viewer (the Mac you control from)

1. Drag OpenShare into Applications.
2. Launch it. On first run, enter the worker Mac's address and the
   manager code shown by OpenShareCompanion.
That's it - the remote screen appears and your mouse/keyboard control it."

echo
echo "Done. Distributables in: $DIST_DIR"
ls -1 "$DIST_DIR"
