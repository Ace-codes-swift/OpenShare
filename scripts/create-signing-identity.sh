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

# Create a persistent self-signed code-signing identity ("OpenShare Dev
# Signing") in the login keychain. Signing every build with this one
# identity gives the apps a stable identity, so macOS remembers Screen
# Recording / Accessibility / firewall / Keychain approvals across rebuilds.
#
# Run this ONCE on the machine that builds the apps. You may be asked for
# your login password when the certificate trust setting is saved.
set -euo pipefail

IDENTITY_NAME="OpenShare Dev Signing"
DIR="$HOME/.openshare/signing"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

if security find-identity -v -p codesigning 2>/dev/null | grep -q "$IDENTITY_NAME"; then
  echo "'$IDENTITY_NAME' already exists and is valid. Nothing to do."
  exit 0
fi

mkdir -p "$DIR"
cd "$DIR"

if [[ ! -f cert.pem ]]; then
  echo "==> Generating self-signed certificate"
  openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 3650 -nodes \
    -subj "/CN=$IDENTITY_NAME" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" \
    -addext "basicConstraints=critical,CA:true"
  openssl pkcs12 -export -legacy -out identity.p12 -inkey key.pem -in cert.pem \
    -passout pass:openshare 2>/dev/null ||
    openssl pkcs12 -export -out identity.p12 -inkey key.pem -in cert.pem \
      -passout pass:openshare
fi

echo "==> Importing into login keychain"
security import identity.p12 -k "$KEYCHAIN" -P openshare -T /usr/bin/codesign || true

echo "==> Trusting certificate for code signing"
security add-trusted-cert -p codeSign -k "$KEYCHAIN" cert.pem

security find-identity -v -p codesigning
echo "Done. scripts/package.sh will now sign with '$IDENTITY_NAME'."
