# OpenShare

macOS remote desktop (v1): control a companion host from a viewer, with Full HD H.264 video and automatic rolling-code auth.

## Apps

| Binary | Role |
|--------|------|
| `OpenShare` | Viewer (manager side) — shows remote screen, sends mouse/keyboard |
| `OpenShareCompanion` | Host (worker) — captures screen, injects input, streams video |

## Install (recommended: prebuilt apps)

One-time on the build Mac — create a stable signing identity so macOS
remembers Screen Recording / Accessibility / firewall approvals across
rebuilds (ad-hoc signatures change every build, which makes macOS re-prompt
for everything each time):

```bash
./scripts/create-signing-identity.sh
```

Then produce two double-clickable installers with:

```bash
./scripts/package.sh
```

This writes to `dist/`:

| DMG | Install on | Contains |
|-----|-----------|----------|
| `OpenShare-Companion.dmg` | the Mac you want to control | `OpenShareCompanion.app` |
| `OpenShare-Viewer.dmg` | the Mac you control from | `OpenShare.app` |

Each app is self-contained (SDL is statically linked; only system frameworks
are used), so it runs on a Mac with nothing else installed. Copy the DMG over,
drag the app into Applications, and open it — no build tools or `.env` required.

### Pairing two computers (first run)

1. On the **controlled** Mac, open `OpenShareCompanion`. It shows a random
   **manager code** and this Mac's **address**, and approves Screen Recording +
   Accessibility when macOS prompts.
2. On the **controlling** Mac, open `OpenShare`. Enter the worker's address and
   the manager code once.

That's it. Both apps store the pairing (Keychain + `~/.openshare/`) and
reconnect automatically afterwards.

## Auth (manager + worker codes)

1. On its first launch, the companion generates a random **manager code**, stores it in macOS Keychain, and displays it in a native dialog alongside this Mac's IP address.
2. On its first launch, the viewer asks for the worker address and that code in a native dialog, storing the code in Keychain and the address under `~/.openshare/worker_host`.
3. On each later connect, the viewer derives a **worker** (rolling) code from the stored manager code + a local counter and sends it automatically.
4. The companion verifies the worker code, rejects replays, and advances its counter — the same idea as a car key fob.

Counters live under `~/.openshare/` (`hotp_counter_manager` on the viewer, `hotp_counter_worker` on the companion).
The manager code is not kept in `.env`.

## Setup

```bash
cp .env.example .env
```

## Build from source (developers)

```bash
cmake -S . -B build
cmake --build build -j
```

Produces `build/OpenShare.app` and `build/OpenShareCompanion.app`. Run them
directly (`open build/OpenShareCompanion.app`) or via their inner binaries.

### Permissions (companion Mac)

- **Screen Recording** — System Settings → Privacy & Security → Screen Recording  
- **Accessibility** — System Settings → Privacy & Security → Accessibility  

Grant both to `OpenShareCompanion` (or Terminal if you launch it from there).
After granting Screen Recording, launch the companion again — macOS only
applies that permission to a fresh launch. If the companion is missing a
permission it now shows a dialog saying exactly what to enable, with a button
that opens the right System Settings pane.

Do not run the apps from inside the mounted DMG or straight from Downloads:
macOS runs quarantined apps from a randomized read-only location
("app translocation"), so permission grants never stick. Always drag the app
into Applications first.

## Run companion as a background service (LaunchAgent)

On the **host** Mac, after installing or building the app:

```bash
chmod +x scripts/install-companion-service.sh scripts/uninstall-companion-service.sh
./scripts/install-companion-service.sh
```

The script auto-detects the installed `OpenShareCompanion.app` (or a build).

That installs `~/Library/LaunchAgents/com.atech.OpenShareCompanion.plist` so the companion:

- starts at login
- stays running (`KeepAlive`)
- logs to `~/Library/Logs/OpenShare/`

Remove it with:

```bash
./scripts/uninstall-companion-service.sh
```

## Config (`.env`)

| Variable | Meaning |
|----------|---------|
| `OPENSHARE_HOST` | Companion host (viewer) |
| `OPENSHARE_PORT` | TCP port |
| `OPENSHARE_VIDEO_WIDTH` / `HEIGHT` | Stream resolution (default 1920×1080) |
| `OPENSHARE_TARGET_FPS` | Capture/encode target |
| `OPENSHARE_COUNTER_WINDOW` | Max counter look-ahead for resync |

## Cross-network (later)

Use a TCP tunnel (e.g. Cloudflare Tunnel) to expose `OPENSHARE_PORT` on the companion, then set `OPENSHARE_HOST` on the viewer to the tunnel hostname. The protocol is plain TCP length-prefixed frames — no changes required.
