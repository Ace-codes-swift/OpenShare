# OpenShare (Linux)

Copyright (C) 2026 Ace Jones / ATech. Licensed under the GNU GPL v3;
see [LICENSE.md](LICENSE.md).

Both platforms speak the **same UDP wire protocol** (fragmented datagrams,
H.264 Annex-B video, HOTP rolling codes, macOS virtual keycodes on the wire),
so:

| Viewer | Companion |
|--------|-----------|
| Linux | Linux |
| Linux | macOS |
| macOS | Linux |

## Apps

| Binary | Role |
|--------|------|
| `OpenShare` | Viewer — shows remote screen, sends mouse/keyboard |
| `OpenShareCompanion` | Host — captures screen (X11), injects input (XTest), streams H.264 |

Requires an **X11 or XWayland** session. Pure Wayland capture is not supported yet.

## Dependencies (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake pkg-config git \
  libx11-dev libxext-dev libxfixes-dev libxtst-dev libxinerama-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libx264-dev \
  libssl-dev \
  libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
  libwayland-dev libxkbcommon-dev \
  zenity
```

`zenity` is optional (pairing falls back to the terminal).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Binaries: `build/OpenShare`, `build/OpenShareCompanion`.

## Pairing

1. On the **host**, run `./build/OpenShareCompanion` — note the address and manager code.
2. On the **viewer**, run `./build/OpenShare` — enter that address and code.

Pairing files and HOTP counters live under `~/.openshare/`. Manual companion
launches regenerate a fresh manager code; service mode keeps it stable.

## Background companion (systemd --user)

```bash
./scripts/install-companion-service.sh
./scripts/uninstall-companion-service.sh
```

## Config (`.env`)

```bash
cp .env.example .env
```

| Variable | Meaning |
|----------|---------|
| `OPENSHARE_HOST` | Optional viewer override for companion address |
| `OPENSHARE_PORT` | UDP port (default 9000) |
| `OPENSHARE_VIDEO_WIDTH` / `HEIGHT` | Stream size (`0` / unset = native capture) |
| `OPENSHARE_TARGET_FPS` | Capture/encode target |
| `OPENSHARE_COUNTER_WINDOW` | HOTP look-ahead for resync |

## Client library

`OpenShareClient` (`libopenshare_client.a`) exposes `openshare::connect(...)` —
same API as on macOS. See `include/openshare/client.hpp` and
`examples/client_example.cpp`.

## License

OpenShare is licensed under the [GNU General Public License v3.0](LICENSE.md).
